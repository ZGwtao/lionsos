
#include <microkit.h>
#include <stdarg.h>
#include <sddf/util/printf.h>
#include <libtrustedlo.h>

#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <ossvc.h>
#include <libmicrokitco.h>
#include <pc_config.h>
#include <protocon.h>

#define PROGNAME "[@monitor] "

// these memory regions are shared memory between
//    -> the monitor (container monitor)
//    -> the dynamic pds (protocon - proto containers)
// the size of these memory regions are:
//    -> PC_MONITOR_REGION_SIZE (for one dynamic pd)
//
#define PC_MONITOR_REGION_SIZE (0x800000)
#define PC_MONITOR_REGION_PROTOCON_ELF_BASE (0x10000000)
#define PC_MONITOR_REGION_TRAMPOLINE_ELF_BASE (0x30000000)
#define PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE (0x50000000)

// each elf file is of the same upper size limit
#define ELF_FILE_SIZE           0x800000
// elf files from frontend as external files...
// shared memory with the frontend PD
uintptr_t ext_protocon_elf      = 0x6000000;
uintptr_t ext_trampoline_elf    = 0x6800000;
uintptr_t ext_payload_elf       = 0x7000000;

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

co_control_t co_controller_mem;

static char monitor_costack1[0x10000];
static char monitor_costack2[0x10000];

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }


int monitor_svc_dist_map[PC_CHILD_PER_MONITOR_MAX_NUM][SVC_TYPE_MAX_NUM];

tsldr_context_t protocon_ctx_db[PC_CHILD_PER_MONITOR_MAX_NUM];

protocon_lifecycle_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];

#define SMALL_PAGE_SIZE     (0x1000)

#define TSLDR_CONTEXT_BASE  (0xff40000)
#define TSLDR_CONTEXT_SIZE  SMALL_PAGE_SIZE

#define TSLDR_METADATA_BASE (0xffc0000)
#define TSLDR_METADATA_SIZE SMALL_PAGE_SIZE

#define TSLDR_MDINFO_HASH   (0xffff)

#define PC_MONITOR_FRONTEND_CHANNEL (15)
#define PC_MONITOR_PROTOCON_BASE_CHANNEL (24)

#define PC_MONITOR_CALL_DEPLOY (1)
#define PC_MONITOR_CALL_BACKUP_CONTEXT (20)
#define PC_MONITOR_CALL_TERMINATE (0x100)

// base of all shared os services metadata regions
uintptr_t msvcdb_base = 0x0ff80000;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

#define SET_PROTOCON_AS_INSTANTIATED(C) \
    do { protocon_states[C] = PROTOCON_ACTIVE; } while (0);

#define SET_PROTOCON_AS_AVAILABLE(C) \
    do { protocon_states[C] = PROTOCON_PASSIVE; } while (0);


void monitor_main_cothread_spawn(const client_entry_t client_entry, void *arg, char err_msg[])
{
    if (microkit_cothread_spawn(client_entry, arg) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(err_msg);
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();
}


void monitor_main_init_storage(void)
{
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to mount\n");
        microkit_internal_crash(-1);
    }
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) finished fs initialisation\n");
}


static inline void monitor_main_notify_frontend()
{
    microkit_notify(PC_MONITOR_FRONTEND_CHANNEL);
}

void monitor_call_deploy_protocon_second_half()
{
    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_second_half\n");

    // FIXME: should not use shared memory to determine state...
    Elf64_Ehdr *payload_eh = (Elf64_Ehdr *)ext_payload_elf;
    if (payload_eh->e_shoff == 0 || payload_eh->e_shnum == 0 || payload_eh->e_shentsize != sizeof(Elf64_Shdr)
        || payload_eh->e_shstrndx == SHN_UNDEF || payload_eh->e_shstrndx >= payload_eh->e_shnum)
    {
        TSLDR_DBG_PRINT(PROGNAME "no section headers present or unexpected shentsize or invalid e_shstrndx\n");
        monitor_main_notify_frontend();
        return;
    }

    // a request stat for OS services to be filled out by the client payload info...
    protocon_svc_req_t req;

    // the section defined by the user application in elf for specifying requested OS services
    Elf64_Shdr *user_defined_svc_section;
    user_defined_svc_section = (Elf64_Shdr *)tsldr_miscutil_find_section_from_elf((void *)ext_payload_elf, PC_SVC_DESC_SECTION_NAME);
    if (!user_defined_svc_section) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to restart container as no iface section specified\n");
        monitor_main_notify_frontend();
        return;
    }

    // we will put the info of user-specified, requested OS services (user_defined_svc_section)
    // into the request variable with a given format (req) and use it to query the OS service distribution map
    // if we have any available dynamic PD that offers the superset of requested OS services,
    // return the cid of that dynamic PD, update the state of the dynamic PD (protocon), and do the trusted loading work underneath
    // otherwise, return early (when the cid returned is invalid)
    int cid = monitor_match_ossvc_request_with_available_pd((void *)ext_payload_elf, user_defined_svc_section, &req, protocon_states);
    if (cid >= PC_CHILD_PER_MONITOR_MAX_NUM || cid < 0) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to find suitable container for payload\n");
        TSLDR_DBG_PRINT(PROGNAME "Fetched cid number is: %d\n", cid);
        monitor_main_notify_frontend();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", cid);

    uintptr_t payload_base = PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE + PC_MONITOR_REGION_SIZE * cid;
    uintptr_t protocon_base = PC_MONITOR_REGION_PROTOCON_ELF_BASE + PC_MONITOR_REGION_SIZE * cid;
    uintptr_t trampoline_base = PC_MONITOR_REGION_TRAMPOLINE_ELF_BASE + PC_MONITOR_REGION_SIZE * cid;


    Elf64_Ehdr *protocon_eh = (Elf64_Ehdr *)ext_protocon_elf;
    uintptr_t entry = protocon_eh->e_entry;

    tsldr_miscutil_load_elf((void*)protocon_base, protocon_eh);
    TSLDR_DBG_PRINT(PROGNAME "Copied proto container to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)payload_base, (char *)ext_payload_elf, ELF_FILE_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied client program to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)trampoline_base, (char *)ext_trampoline_elf, ELF_FILE_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied trampoline program to child PD's memory region\n");

    monitor_patch_payload_with_ossvc_info(cid, &req, payload_base, msvcdb_base);

    tsldr_main_monitor_init_mdinfo((tsldr_mdinfodb_t *)microkit_trusted_loading_info, cid, (void *)((char *)TSLDR_METADATA_BASE + cid * TSLDR_METADATA_SIZE));

    tsldr_miscutil_memcpy((char *)TSLDR_CONTEXT_BASE + cid * TSLDR_CONTEXT_SIZE, &protocon_ctx_db[cid], sizeof(tsldr_context_t));

    tsldr_main_monitor_privilege_pd(cid);

    SET_PROTOCON_AS_INSTANTIATED(cid)

    /* switch to trusted loader */
    microkit_pd_restart(cid, entry);
    TSLDR_DBG_PRINT(PROGNAME "Started child PD at entrypoint address: %x\n", (unsigned long long)entry);
}


void init(void)
{
    assert(fs_config_check_magic(&fs_config));
    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    // global os services state initialisation...
    tsldr_miscutil_memset(monitor_svc_dist_map, 0, sizeof(int) * PC_CHILD_PER_MONITOR_MAX_NUM * SVC_TYPE_MAX_NUM);
    monitor_init_ossvc_map();

    // global client state initialisation...
    tsldr_miscutil_memset(protocon_states, PROTOCON_PASSIVE, sizeof(int) * PC_CHILD_PER_MONITOR_MAX_NUM);
    // clean all loader context...
    tsldr_miscutil_memset(protocon_ctx_db, 0, sizeof(tsldr_context_t) * PC_CHILD_PER_MONITOR_MAX_NUM);

    stack_ptrs_arg_array_t costacks = { (uintptr_t) monitor_costack1, (uintptr_t) monitor_costack2 };
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);
    monitor_main_cothread_spawn(monitor_main_init_storage, NULL, " failed to spawn thread for storage initialisation.\n");
}

void notified(microkit_channel ch)
{
    fs_process_completions(NULL);

    microkit_cothread_recv_ntfn(ch);
}


void monitor_main_handle_fault(microkit_child child, microkit_msginfo msginfo)
{
    seL4_Word label = microkit_msginfo_get_label(msginfo);
    seL4_Word notidy_flag = 0;
    // we handle only VM fault particularly...
    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        // we use it for running into a purely empty thread? (or init a dynamic pd)
        notidy_flag = ip | address;
        if (notidy_flag) {
            TSLDR_DBG_PRINT(PROGNAME "seL4_Fault_VMFault\n");
            TSLDR_DBG_PRINT(PROGNAME "Fault address: 0x%x\n", (unsigned long long)address);
            TSLDR_DBG_PRINT(PROGNAME "Fault instruction pointer: 0x%x\n", (unsigned long long)ip);
        } else {
            TSLDR_DBG_PRINT(PROGNAME "receive the first fault from an empty pd with id; '%d'\n", child);
        }
    }
    microkit_pd_stop(child);

    if (!notidy_flag) {
        // early return if we receive the first fault from dynamic pds (or empty threads if possible?)
        return;
    }
    // do not print fault label for initialising dynamic pd...
    TSLDR_DBG_PRINT(PROGNAME "Fault label: %d\n", label);
    monitor_main_notify_frontend();
}


seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    monitor_main_handle_fault(child, msginfo);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}

seL4_MessageInfo_t monitor_call_deploy_protocon_first_half(void)
{
    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_first_half\n");
    seL4_Word err;
    tsldr_main_check_elf_integrity(ext_protocon_elf, &err);
    if (err) {
        TSLDR_DBG_PRINT(PROGNAME "Integrity check failed for protocon elf\n");
        monitor_main_notify_frontend();
        return microkit_msginfo_new(seL4_NoError, 0);
    }
    TSLDR_DBG_PRINT(PROGNAME "Integrity check passed for protocon elf\n");
    monitor_main_cothread_spawn(monitor_call_deploy_protocon_second_half, NULL, "cannot initialise monitor cothread for monitor call.\n");
    return microkit_msginfo_new(seL4_NoError, 0);
}


int monitor_main_get_cid_from_channel(microkit_channel ch)
{
    if (ch < PC_MONITOR_PROTOCON_BASE_CHANNEL ||
        ch >= (PC_MONITOR_PROTOCON_BASE_CHANNEL + PC_CHILD_PER_MONITOR_MAX_NUM))
    {
        TSLDR_DBG_PRINT(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        microkit_internal_crash(-1);
    }
    return ch - PC_MONITOR_PROTOCON_BASE_CHANNEL;
}


seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);
    assert(protocon_states[cid] == PROTOCON_ACTIVE);

    SET_PROTOCON_AS_AVAILABLE(cid)

    monitor_main_notify_frontend();

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t monitor_call_backup_protocon_loading_context(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);

    tsldr_context_t *context = (tsldr_context_t *)((unsigned char *)TSLDR_CONTEXT_BASE + cid * TSLDR_CONTEXT_SIZE);

    tsldr_miscutil_memcpy(&protocon_ctx_db[cid], context, sizeof(tsldr_context_t));

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t monitor_main_handle_pccall(microkit_channel ch)
{
    /* get the first word of the message */
    seL4_Word call_id = microkit_mr_get(0);
    seL4_MessageInfo_t ret = microkit_msginfo_new(0, 0);

    /* call for the container monitor */
    switch (call_id) {
    case PC_MONITOR_CALL_DEPLOY:
        TSLDR_DBG_PRINT(PROGNAME "Deploy an application to a dynamic PD\n");
        ret = monitor_call_deploy_protocon_first_half();
        break;
    case PC_MONITOR_CALL_BACKUP_CONTEXT:
        TSLDR_DBG_PRINT(PROGNAME "Backing up trusted loading context for dynamic PD with ID: %d\n", monitor_main_get_cid_from_channel(ch));
        ret = monitor_call_backup_protocon_loading_context(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE:
        TSLDR_DBG_PRINT(PROGNAME "Exit and uninstantiate a dynamic PD\n");
        ret = monitor_call_restore_protocon(ch);
        break;
    default:
        /* do nothing for now */
        TSLDR_DBG_PRINT(PROGNAME "Undefined container monitor call: %lu\n", call_id);
        break;
    }

    return ret;
}


seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return monitor_main_handle_pccall(ch);
}


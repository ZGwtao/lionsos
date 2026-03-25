
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
#define FE_MONITOR_REGION_SIZE (0x800000)
// elf files from frontend as external files...
// shared memory with the frontend PD
#define FE_MONITOR_REGION_PROTOCON_ELF_BASE (0x6000000)
#define FE_MONITOR_REGION_TRAMPOLINE_ELF_BASE (0x6800000)
#define FE_MONITOR_REGION_CLIENT_PAYLOAD_BASE (0x7000000)

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

// these are the craziest thing for microkit cothreads
co_control_t co_controller_mem;
static char monitor_costack1[0x10000];
static char monitor_costack2[0x10000];
static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

// record the number of OS services of each type provided by each dynamic PD (protocon)
// so, at first level the index is the dynamic PD index (16 dynamic PDs at most),
// and at second level the index is the OS service type index (8 types at most)
int monitor_svc_dist_map[PC_CHILD_PER_MONITOR_MAX_NUM][SVC_TYPE_MAX_NUM];

// record the trusted loading context of each dynamic PD (protocon)
tsldr_context_t protocon_ctx_db[PC_CHILD_PER_MONITOR_MAX_NUM];

// record the current lifecycle state of each dynamic PD (protocon)
protocon_lifecycle_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];

#define SMALL_PAGE_SIZE     (0x1000)

// this is the base address of the trusted loader context region for each dynamic PD (protocon)
// this describes the information of all requested low-level access rights of a dynamic PD, which is the SUBSET of trusted loading metadata
#define TSLDR_CONTEXT_BASE  (0xff40000)
#define TSLDR_CONTEXT_SIZE  SMALL_PAGE_SIZE
// this is the base address of the trusted loader metadata region for each dynamic PD (protocon)
// the monitor PD will prepare the metadata for each dynamic PD in this region, and the dynamic PD will read the metadata from this region when it is loading
// this describes the information of all low-level access rights of a dynamic PD, which will be used by the trusted loader to do the actual loading work
#define TSLDR_METADATA_BASE (0xffc0000)
#define TSLDR_METADATA_SIZE SMALL_PAGE_SIZE

// if a trusted loader metadata region is initialised, check the hash with this number
// if not match, it means the metadata is not initialised
#define TSLDR_MDINFO_HASH   (0xffff)

// channels id for monitor PD to communicate with the frontend PD and the dynamic PDs (protocons)
#define PC_MONITOR_FRONTEND_CHANNEL (15)
#define PC_MONITOR_PROTOCON_BASE_CHANNEL (24)

// monitor call numbers
#define PC_MONITOR_CALL_DEPLOY (1)
#define PC_MONITOR_CALL_BACKUP_CONTEXT (20)
#define PC_MONITOR_CALL_TERMINATE (0x100)

// base of all shared os services metadata regions
// the region is for all dynamic PDs, each dynamic PD has a piece between the monitor PD and the dynamic PD itself
// we use it to store the information of OS services requested by the client program
// we will encode the low-level access rights information of OS services requested
// and put the serialised, encoded info into this region.
// the dynamic PD can then read the OS svc information at high-level, while initialise the trusted loader
// with the low-level information provided here...
uintptr_t msvcdb_base = 0x0ff80000;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

__attribute__((__section__(".monitor_svc_db"))) monitor_svcdb_t monitor_svc_db;


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

void monitor_main_load_elfs_into_protocon(int cid)
{
    uintptr_t payload_base = PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE + PC_MONITOR_REGION_SIZE * cid;
    uintptr_t protocon_base = PC_MONITOR_REGION_PROTOCON_ELF_BASE + PC_MONITOR_REGION_SIZE * cid;
    uintptr_t trampoline_base = PC_MONITOR_REGION_TRAMPOLINE_ELF_BASE + PC_MONITOR_REGION_SIZE * cid;

    tsldr_miscutil_load_elf((void*)protocon_base, (const Elf64_Ehdr *)FE_MONITOR_REGION_PROTOCON_ELF_BASE);
    TSLDR_DBG_PRINT(PROGNAME "Copied proto container to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)payload_base, (char *)FE_MONITOR_REGION_CLIENT_PAYLOAD_BASE, FE_MONITOR_REGION_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied client program to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)trampoline_base, (char *)FE_MONITOR_REGION_TRAMPOLINE_ELF_BASE, FE_MONITOR_REGION_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied trampoline program to child PD's memory region\n");
}


void monitor_call_deploy_protocon_second_half()
{
    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_second_half\n");

    // FIXME: should not use shared memory to determine state...
    Elf64_Ehdr *payload_eh = (Elf64_Ehdr *)FE_MONITOR_REGION_CLIENT_PAYLOAD_BASE;
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
    user_defined_svc_section = (Elf64_Shdr *)tsldr_miscutil_find_section_from_elf((void *)FE_MONITOR_REGION_CLIENT_PAYLOAD_BASE, PC_SVC_DESC_SECTION_NAME);
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
    int cid = monitor_match_ossvc_request_with_available_pd((void *)FE_MONITOR_REGION_CLIENT_PAYLOAD_BASE, user_defined_svc_section, &req, protocon_states);
    if (cid >= PC_CHILD_PER_MONITOR_MAX_NUM || cid < 0) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to find suitable container for payload\n");
        TSLDR_DBG_PRINT(PROGNAME "Fetched cid number is: %d\n", cid);
        monitor_main_notify_frontend();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", cid);

    monitor_main_load_elfs_into_protocon(cid);

    Elf64_Ehdr *client_payload_eh = (Elf64_Ehdr *)PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE + PC_MONITOR_REGION_SIZE * cid;
    // when the client payload is loaded into the memory of the dynamic PD,
    // we will patch the payload with the information of where to access the OS services (i.e., pointers to the OS services)
    // we don't patch it on the shared memory of frontend side, as it is not correct to change the content of source input
    monitor_patch_payload_with_ossvc_info(cid, &req, (uintptr_t)client_payload_eh, msvcdb_base);

    // prepare the trusted loading metadata for this dynamic PD (protocon)
    // the information source is patched by microkit (microkit_trusted_loading_info)
    tsldr_main_monitor_init_mdinfo((tsldr_mdinfodb_t *)microkit_trusted_loading_info, cid, (void *)((char *)TSLDR_METADATA_BASE + cid * TSLDR_METADATA_SIZE));

    // if a dynamic pd has a trusted loading context, copy the context from the db into the dynamic pd's shared memory region for trusted loading
    // otherwise just do nothing as the trusted loader will check the metadata and find it is not initialised, then skip the restoring process and jump to the next steps directly
    tsldr_miscutil_memcpy((char *)TSLDR_CONTEXT_BASE + cid * TSLDR_CONTEXT_SIZE, &protocon_ctx_db[cid], sizeof(tsldr_context_t));

    // before trusted loading, grant high privileges to the dynamic PD (protocon)
    tsldr_main_monitor_privilege_pd(cid);

    // mark the matched dynamic PD (protocon) as instantiated (active, in use)
    SET_PROTOCON_AS_INSTANTIATED(cid)
    
    /* --- at this stage, start the protocon --- */

    Elf64_Ehdr *protocon_eh = (Elf64_Ehdr *)FE_MONITOR_REGION_PROTOCON_ELF_BASE;
    /* switch to trusted loader in protocon */
    microkit_pd_restart(cid, protocon_eh->e_entry);
    TSLDR_DBG_PRINT(PROGNAME "Started child PD at entrypoint address: %x\n", protocon_eh->e_entry);
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
    tsldr_main_check_elf_integrity(FE_MONITOR_REGION_PROTOCON_ELF_BASE, &err);
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


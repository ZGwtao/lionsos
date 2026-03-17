
#include <microkit.h>
#include <stdarg.h>
#include <sddf/util/printf.h>
#include <libtrustedlo.h>

#include <ossvc.h>
#include <libmicrokitco.h>
#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <pc_config.h>
#include <protocon.h>

#define PROGNAME "[@monitor] "

// shared memory with the proto-container PDs
uintptr_t trusted_loader_exec   = 0x10000000;
uintptr_t trampoline_elf        = 0x30000000;
uintptr_t container_elf         = 0x50000000;

// each elf file is of the same upper size limit
#define ELF_FILE_SIZE           0x800000
// elf files from frontend as external files...
// shared memory with the frontend PD
uintptr_t ext_protocon_elf      = 0x6000000;
uintptr_t ext_trampoline_elf    = 0x6800000;
uintptr_t ext_payload_elf       = 0x7000000;

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[PC_WORKER_THREAD_NUM + 1];

uint64_t _worker_thread_stack_one = 0xA0000000;
uint64_t _worker_thread_stack_two = 0xB0000000;


request_metadata_t request_metadata[FS_QUEUE_CAPACITY];
buffer_metadata_t buffer_metadata[FS_QUEUE_CAPACITY];


int monitor_svc_dist_map[PC_CHILD_PER_MONITOR_MAX_NUM][SVC_TYPE_MAX_NUM];

tsldr_context_t protocon_ctx_db[PC_CHILD_PER_MONITOR_MAX_NUM];

protocon_lifecycle_state_t protocon_states[PC_CHILD_PER_MONITOR_MAX_NUM];



#define SMALL_PAGE_SIZE     0x1000

#define TSLDR_CONTEXT_BASE  0xff40000
#define TSLDR_CONTEXT_SIZE  SMALL_PAGE_SIZE

#define TSLDR_METADATA_BASE 0xffc0000
#define TSLDR_METADATA_SIZE SMALL_PAGE_SIZE

#define TSLDR_MDINFO_HASH   0xffff



#define PC_MONITOR_FRONTEND_CHANNEL (15)



// base of all shared acgroup metadata regions
uintptr_t msvcdb_base = 0x0ff80000;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;


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
    tsldr_miscutil_memset(request_metadata, 0, sizeof(request_metadata_t) * FS_QUEUE_CAPACITY);
    tsldr_miscutil_memset(buffer_metadata, 0, sizeof(buffer_metadata_t) * FS_QUEUE_CAPACITY);

    TSLDR_DBG_PRINT(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "MP|ERROR: Failed to mount\n");
    }
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) finished fs initialisation\n");
}


static void patch_elf_connection(void *elf_base, char data_file[], uintptr_t vaddr)
{
    int err = 0;
    seL4_Word target_sh = tsldr_miscutil_fetch_elf_section_with_vaddr(elf_base, vaddr);
    if (!target_sh) {
        // halt...
        while (1);
    }
    // FIXME: allow multiple connections...
    pico_vfs_readfile2buf((void *)target_sh, data_file, &err);
    if (err != seL4_NoError) {
        // halt...
        TSLDR_DBG_PRINT(PROGNAME "Failed to establish connection for %s iface: %d", data_file, vaddr);
        while (1);
    }
}


static inline void monitor_main_notify_frontend()
{
    microkit_notify(PC_MONITOR_FRONTEND_CHANNEL);
}

void monitor_call_debute_lower()
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf;

    // FIXME: should not use shared memory to determine state...
    Elf64_Ehdr *eh = (Elf64_Ehdr *)ext_payload_elf;
    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize != sizeof(Elf64_Shdr))
        TSLDR_DBG_PRINT(PROGNAME "no section headers present or unexpected shentsize\n");
    if (eh->e_shstrndx == SHN_UNDEF || eh->e_shstrndx >= eh->e_shnum)
        TSLDR_DBG_PRINT(PROGNAME "invalid e_shstrndx");

    // FIXME: should not use shared memory to determine state...
    Elf64_Shdr *iface_sh;
    iface_sh = (Elf64_Shdr *)tsldr_miscutil_find_section_from_elf((void *)ext_payload_elf, PC_SVC_DESC_SECTION_NAME);
    if (!iface_sh) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to restart container as no iface section specified\n");
        monitor_main_notify_frontend();
        return;
    }

    // a request stat for the client payload...
    protocon_svc_req_t req;

    int cid = monitor_match_ossvc_request_with_available_pd((void *)ext_payload_elf, iface_sh, &req, protocon_states);
    if (cid >= PC_CHILD_PER_MONITOR_MAX_NUM || cid < 0) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to find suitable container for payload\n");
        TSLDR_DBG_PRINT(PROGNAME "Fetched cid number is: %d\n", cid);
        monitor_main_notify_frontend();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", cid);

    uintptr_t payload_base = container_elf + ELF_FILE_SIZE * cid;
    uintptr_t protocon_base = trusted_loader_exec + ELF_FILE_SIZE * cid;
    uintptr_t trampoline_base = trampoline_elf + ELF_FILE_SIZE * cid;
    uintptr_t entry = ehdr->e_entry;

    tsldr_miscutil_load_elf((void*)protocon_base, ehdr);
    TSLDR_DBG_PRINT(PROGNAME "Copied trusted loader to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)payload_base, (char *)ext_payload_elf, ELF_FILE_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied client program to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)trampoline_base, (char *)ext_trampoline_elf, ELF_FILE_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied trampoline program to child PD's memory region\n");

    monitor_patch_payload_with_ossvc_info(cid, &req, payload_base, msvcdb_base, patch_elf_connection);

    tsldr_main_monitor_init_mdinfo((tsldr_mdinfodb_t *)microkit_template_spec, cid, (void *)((char *)TSLDR_METADATA_BASE + cid * TSLDR_METADATA_SIZE));

    tsldr_miscutil_memcpy((char *)TSLDR_CONTEXT_BASE + cid * TSLDR_CONTEXT_SIZE, &protocon_ctx_db[cid], sizeof(tsldr_context_t));

    tsldr_main_monitor_privilege_pd(cid);
    //
    // set the client PD to restart as exclusive... 
    // mark current client[cid] PD in use
    //
    protocon_states[cid] = PROTOCON_ACTIVE;

    /* switch to trusted loader */
    microkit_pd_restart(cid, entry);
    TSLDR_DBG_PRINT(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)entry);
}


void init(void)
{
    assert(fs_config_check_magic(&fs_config));
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;

    tsldr_miscutil_memset(monitor_svc_dist_map, 0, sizeof(int) * PC_CHILD_PER_MONITOR_MAX_NUM * SVC_TYPE_MAX_NUM);

    // global acgroup state initialisation...
    monitor_init_ossvc_map();

    // global client state initialisation...
    tsldr_miscutil_memset(protocon_states, PROTOCON_PASSIVE, sizeof(int) * PC_CHILD_PER_MONITOR_MAX_NUM);
    // clean all loader context...
    tsldr_miscutil_memset(protocon_ctx_db, 0, sizeof(tsldr_context_t) * PC_CHILD_PER_MONITOR_MAX_NUM);

    stack_ptrs_arg_array_t costacks = {
        _worker_thread_stack_one,
        _worker_thread_stack_two
    };

    microkit_cothread_init(&co_controller_mem, PC_WORKER_THREAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < (PC_WORKER_THREAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }

    monitor_main_cothread_spawn(monitor_main_init_storage, NULL, " failed to spawn thread for storage initialisation.\n");

    TSLDR_DBG_PRINT(PROGNAME "Finished init\n");
}

void notified(microkit_channel ch)
{
    fs_process_completions();

    microkit_cothread_recv_ntfn(ch);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    TSLDR_DBG_PRINT(PROGNAME "Received fault message for child PD: %d\n", child);

    seL4_Word label = microkit_msginfo_get_label(msginfo);
    TSLDR_DBG_PRINT(PROGNAME "Fault label: %d\n", label);

    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        TSLDR_DBG_PRINT(PROGNAME "seL4_Fault_VMFault\n");
        TSLDR_DBG_PRINT(PROGNAME "Fault address: 0x%x\n", (unsigned long long)address);
        TSLDR_DBG_PRINT(PROGNAME "Fault instruction pointer: 0x%x\n", (unsigned long long)ip);
    }

    microkit_pd_stop(child);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}

seL4_MessageInfo_t monitor_call_debute(void)
{
    tsldr_main_check_elf_integrity(ext_protocon_elf);

    monitor_main_cothread_spawn(monitor_call_debute_lower, NULL, "cannot initialise monitor cothread for monitor call.\n");
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_restore(microkit_channel ch)
{
    // sanity check for the channel ID
    if (ch < 24 || ch >= 56) {
        TSLDR_DBG_PRINT(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        return microkit_msginfo_new(-1, 0);
    }
    assert(protocon_states[ch - 24] == PROTOCON_ACTIVE);

    // restore client PD state...
    protocon_states[ch - 24] = PROTOCON_PASSIVE;

    monitor_main_notify_frontend();

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t monitor_call_backup_tsldr_context(microkit_channel ch)
{
    // sanity check for the channel ID
    if (ch < 24 || ch >= 56) {
        TSLDR_DBG_PRINT(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        return microkit_msginfo_new(-1, 0);
    }

    tsldr_context_t *context;
    // fetch target trusted loading context...
    context = (tsldr_context_t *)((unsigned char *)TSLDR_CONTEXT_BASE + (ch - 24) * TSLDR_CONTEXT_SIZE);

    // backup trusted loading context in target slot..
    tsldr_miscutil_memcpy(&protocon_ctx_db[ch - 24], context, sizeof(tsldr_context_t));

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    TSLDR_DBG_PRINT(PROGNAME "Received protected message on channel: %d\n", ch);

    /* get the first word of the message */
    seL4_Word monitorcall_number = microkit_mr_get(0);

    seL4_MessageInfo_t ret;

    /* call for the container monitor */
    switch (monitorcall_number) {
    case 1:
        TSLDR_DBG_PRINT(PROGNAME "Loading trusted loader and the first client\n");
        ret = monitor_call_debute();
        break;
    case 20:
        TSLDR_DBG_PRINT(PROGNAME "Exit to uninstantiated container\n");
        ret = monitor_call_backup_tsldr_context(ch);
        break;
    case 0x100:
        TSLDR_DBG_PRINT(PROGNAME "Exit to uninstantiated container\n");
        ret = monitor_call_restore(ch);
        break;
    default:
        /* do nothing for now */
        TSLDR_DBG_PRINT(PROGNAME "Undefined container monitor call: %lu\n", monitorcall_number);
        break;
    }

    return ret;
}

#include <microkit.h>
#include <stdarg.h>
#include <string.h>
#include <sddf/util/printf.h>
#include <elf_utils.h>
#include <libtrustedlo.h>

#include <libmicrokitco.h>
#include <lions/fs/config.h>
#include <pico_vfs.h>
#include <pc_config.h>

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

bool fs_init;

// the first level index is referenced by PD idx
// the second level index is referenced by connection types
//
//      typedef enum {
//         FS_IFACE = 0,
//         SERIAL_IFACE,
//         NETWORK_IFACE,
//         TIMER_IFACE,
//         I2C_IFACE,
//         RESERVED, /* could be more than this... */
//         UNUSED,
//      } pc_svc_iface_t;
//
// stores the number of one kind of conn
//
int acg_stat_map[MAX_PERM_CL_NUM][MAX_PERC_AK_NUM];

// availability of current PDs...
int client_stat[MAX_PERM_CL_NUM];

#define SMALL_PAGE_SIZE     0x1000

#define TSLDR_CONTEXT_BASE  0xff40000
#define TSLDR_CONTEXT_SIZE  SMALL_PAGE_SIZE

#define TSLDR_METADATA_BASE 0xffc0000
#define TSLDR_METADATA_SIZE SMALL_PAGE_SIZE

tsldr_context_t loader_context[MAX_PERM_CL_NUM];

/*
 * A shared memory region with container, containing content from tsldr_metadata_patched
 * Will be init each time the container restarts by copying the data from above
 */
uintptr_t tsldr_metadata;

// base of all shared acgroup metadata regions
acgrp_arr_list_t *acgroup_metadata_base = (acgrp_arr_list_t *)0x0ff80000;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;


void test_entrypoint(void)
{
    memset(request_metadata, 0, sizeof(request_metadata_t) * FS_QUEUE_CAPACITY);
    memset(buffer_metadata, 0, sizeof(buffer_metadata_t) * FS_QUEUE_CAPACITY);

    microkit_dbg_printf(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        microkit_dbg_printf(PROGNAME "MP|ERROR: Failed to mount\n");
    }
    fs_init = true;

    microkit_dbg_printf(PROGNAME "(fs mount) finished fs initialisation\n");
}

static Elf64_Shdr *elf_find_section(void *elf_base, char section_name[])
{
    Elf64_Ehdr *eh = (Elf64_Ehdr *)elf_base;
    Elf64_Shdr *sh_table = (Elf64_Shdr *)(elf_base + eh->e_shoff);
    Elf64_Shdr *shstr_sh = &sh_table[eh->e_shstrndx];

    const char *shstrtab = (const char *)(elf_base + shstr_sh->sh_offset);

    Elf64_Shdr *target_sh = NULL;
    for (int i = 0; i < eh->e_shnum; ++i) {
        Elf64_Shdr *sh = &sh_table[i];
        if (sh->sh_name >= shstr_sh->sh_size) continue;
        const char *name = shstrtab + sh->sh_name;
        if (strcmp(name, section_name) == 0) {
            target_sh = sh;
            break;
        }
    }
    return target_sh;
}

static inline uint64_t vaddr_to_file_off_elf64(const void *elf_base, uint64_t vaddr) {
    const uint8_t    *base = (const uint8_t *)elf_base;
    const Elf64_Ehdr *eh   = (const Elf64_Ehdr *)base;
    const Elf64_Shdr *sh   = (const Elf64_Shdr *)(base + eh->e_shoff);

    for (uint16_t i = 0; i < eh->e_shnum; ++i) {
        uint64_t start = sh[i].sh_addr;
        uint64_t size  = sh[i].sh_size;
        if (vaddr >= start && vaddr < start + size) {
            if (sh[i].sh_type == SHT_NOBITS) return (uint64_t)-1;
            return (uint64_t)(elf_base + sh[i].sh_offset + (vaddr - start));
        }
    }
    return (uint64_t)-1;
}

static int patch_elf_section(void *elf_base, char section_name[], char data_file[])
{
    Elf64_Shdr *target_sh;
    // find target elf section for patching
    target_sh= elf_find_section(elf_base, section_name);
    if (!target_sh) {
        microkit_dbg_printf(PROGNAME "section '%s' not found\n", section_name);
        return -1;
    }

    int err = 0;
    pico_vfs_readfile2buf((void *)(elf_base + (uint64_t)target_sh->sh_offset), data_file, &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "  %d \n", (void *)(elf_base));
    microkit_dbg_printf(PROGNAME "  %d \n", (void *)(elf_base + (uint64_t)target_sh->sh_offset));
    microkit_dbg_printf(PROGNAME "  %d \n", (void *)(((Elf64_Ehdr *)elf_base)->e_entry + (uint64_t)target_sh->sh_offset));
    return err;
}

static void patch_elf_connection(void *elf_base, char data_file[], uintptr_t vaddr)
{
    int err = 0;
    uintptr_t target_sh = vaddr_to_file_off_elf64(elf_base, vaddr);
    if (!target_sh) {
        // halt...
        while (1);
    }
    // FIXME: allow multiple connections...
    pico_vfs_readfile2buf((void *)target_sh, data_file, &err);
    if (err != seL4_NoError) {
        // halt...
        microkit_dbg_printf(PROGNAME "Failed to establish connection for %s iface: %d", data_file, vaddr);
        while (1);
    }
}


void monitor_call_debute_lower()
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf;

    // FIXME: should not use shared memory to determine state...
    Elf64_Ehdr *eh = (Elf64_Ehdr *)ext_payload_elf;
    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize != sizeof(Elf64_Shdr))
        microkit_dbg_printf(PROGNAME "no section headers present or unexpected shentsize\n");
    if (eh->e_shstrndx == SHN_UNDEF || eh->e_shstrndx >= eh->e_shnum)
        microkit_dbg_printf(PROGNAME "invalid e_shstrndx");

    // FIXME: should not use shared memory to determine state...
    Elf64_Shdr *iface_sh;
    iface_sh = elf_find_section((void *)ext_payload_elf, IFACE_SECTION_NAME);
    if (!iface_sh) {
        microkit_dbg_printf(PROGNAME "Failed to restart container as no iface section specified\n");
        ////
        // FIXME:
        //  try to signal frontend to print instruction for next step
        //
        microkit_notify(15);
        return;
    }
    // choose an available container PD in here... 
    // we assume that all service of the same kind can match (if the connection structure is change, hard code it as well)
    // MUST ADHERE TO WHAT THIS CONTAINER MONITOR EXPOSES TO THE OUTER WORLD!!
    // one PD has at most 32 allowed acgroups (if one acgroup points to one service...)
    //
    // what the iface gives can be something like:
    // <type1, 2>, <type2, 3>, <type3, 4>...
    // we don't have to worry about the referenced address of each required service, just to find a good match here
    // so we can have a map for one PD which contains acgroup
    // type1, num: [acg id]
    // type2, num: [acg id]
    // ...
    // finished and picked one
    //

    // a request stat for the client payload...
    acg_req_t req;

    int cid = fetch_iface_section_info((void *)ext_payload_elf, iface_sh, &req);
    if (cid >= MAX_PERM_CL_NUM || cid < 0) {
        microkit_dbg_printf(PROGNAME "Failed to find suitable container for payload\n");
        microkit_dbg_printf(PROGNAME "Fetched cid number is: %d\n", cid);
        //
        // FIXME:
        //  try to signal frontend to print instruction for next step
        //
        microkit_notify(15);
        return;
    }
    microkit_dbg_printf(PROGNAME "cid available: %d\n", cid);
    //
    // from the iface section, get how many kinds of service the client need, and the number of each kind
    // we plan to use this information to find a matched child PD whose acgroup array can cover
    // 

    // adjust global pointer
    tsldr_metadata = (uintptr_t)((unsigned char *)TSLDR_METADATA_BASE + cid * TSLDR_METADATA_SIZE);
    microkit_dbg_printf(PROGNAME "tsldr_metadata: 0x%x\n", tsldr_metadata);
    // initialise the target tsldr_metadata
    tsldr_main_monitor_init_mdinfo((tsldr_mdinfodb_t *)microkit_template_spec, cid, (void *)tsldr_metadata);

    microkit_dbg_printf(PROGNAME "=>>> 0x%x\n", tsldr_metadata);
    // bring back target trusted loader context...
    tsldr_context_t *context;
    // fetch target trusted loading context...
    context = (tsldr_context_t *)((unsigned char *)TSLDR_CONTEXT_BASE + cid * TSLDR_CONTEXT_SIZE);

    // backup trusted loading context in target slot..
    custom_memcpy(context, &loader_context[cid], sizeof(tsldr_context_t));

    tsldr_main_monitor_privilege_pd(cid);

    uintptr_t payload_base = container_elf + ELF_FILE_SIZE * cid;
    uintptr_t protocon_base = trusted_loader_exec + ELF_FILE_SIZE * cid;
    uintptr_t trampoline_base = trampoline_elf + ELF_FILE_SIZE * cid;
    uintptr_t entry = ehdr->e_entry;

    load_elf((void*)protocon_base, ehdr);
    microkit_dbg_printf(PROGNAME "Copied trusted loader to child PD's memory region\n");

    custom_memcpy((void*)payload_base, (char *)ext_payload_elf, ELF_FILE_SIZE);
    microkit_dbg_printf(PROGNAME "Copied client program to child PD's memory region\n");

    custom_memcpy((void*)trampoline_base, (char *)ext_trampoline_elf, ELF_FILE_SIZE);
    microkit_dbg_printf(PROGNAME "Copied trampoline program to child PD's memory region\n");

    funq(cid, &req, payload_base, patch_elf_connection);
    //
    // set the client PD to restart as exclusive... 
    // mark current client[cid] PD in use
    //
    client_stat[cid] = 1;

    /* switch to trusted loader */
    microkit_pd_restart(cid, entry);
    microkit_dbg_printf(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)entry);
}


void init(void)
{
    microkit_dbg_puts("Hello from monitor\n");
    sddf_printf("Test serial driver\n");

    assert(fs_config_check_magic(&fs_config));
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_init = false;

    tsldr_mdinfodb_t *ptr_spec_trusted_loader = (tsldr_mdinfodb_t *)microkit_template_spec;
    microkit_dbg_printf(PROGNAME "%d\n", ptr_spec_trusted_loader->avails);
    microkit_dbg_printf(PROGNAME "%s\n", microkit_name);

    // practically we use 16 indices...
    for (int i = 0; i < ptr_spec_trusted_loader->avails; ++i) {
        // must provide valid hash to 
        if (ptr_spec_trusted_loader->infodb[i].system_hash != 0xffff) {
            // do not initialise unspecified tsldr metadata
            continue;
        }
        // adjust global pointer
        tsldr_metadata = (uintptr_t)((char *)TSLDR_METADATA_BASE + i * TSLDR_METADATA_SIZE);
        microkit_dbg_printf(PROGNAME "tsldr_metadata: 0x%x\n", tsldr_metadata);
        // initialise the target tsldr_metadata
        tsldr_main_monitor_init_mdinfo(ptr_spec_trusted_loader, i, (void *)tsldr_metadata);
    }
    custom_memset(acg_stat_map, 0, sizeof(int) * MAX_PERM_CL_NUM * MAX_PERC_AK_NUM);

    // global acgroup state initialisation...
    init_acg_state_map();

    // global client state initialisation...
    custom_memset(client_stat, 0, sizeof(int) * MAX_PERM_CL_NUM);
    // clean all loader context...
    custom_memset(loader_context, 0, sizeof(tsldr_context_t) * MAX_PERM_CL_NUM);

    stack_ptrs_arg_array_t costacks = {
        _worker_thread_stack_one,
        _worker_thread_stack_two
    };

    microkit_cothread_init(&co_controller_mem, PC_WORKER_THREAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < (PC_WORKER_THREAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }

    if (microkit_cothread_spawn(test_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot initialise frontend cothread1\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    microkit_dbg_printf(PROGNAME "Finished init\n");
}

void notified(microkit_channel ch)
{
    fs_process_completions();

    microkit_cothread_recv_ntfn(ch);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    microkit_dbg_printf(PROGNAME "Received fault message for child PD: %d\n", child);

    seL4_Word label = microkit_msginfo_get_label(msginfo);
    microkit_dbg_printf(PROGNAME "Fault label: %d\n", label);

    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        microkit_dbg_printf(PROGNAME "seL4_Fault_VMFault\n");
        microkit_dbg_printf(PROGNAME "Fault address: 0x%x\n", (unsigned long long)address);
        microkit_dbg_printf(PROGNAME "Fault instruction pointer: 0x%x\n", (unsigned long long)ip);
    }

    microkit_pd_stop(child);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}

seL4_MessageInfo_t monitor_call_debute(void)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf;
    if (custom_memcmp(ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        microkit_dbg_printf(PROGNAME "Data in shared memory region must be an ELF file\n");
        return microkit_msginfo_new(seL4_InvalidArgument, 0);
    }
    microkit_dbg_printf(PROGNAME "Verified ELF header\n");

    if (microkit_cothread_spawn(monitor_call_debute_lower, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot initialise monitor cothread\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    microkit_dbg_printf(PROGNAME "finished with loading data files\n");
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_restore(microkit_channel ch)
{
    // sanity check for the channel ID
    if (ch < 24 || ch >= 56) {
        microkit_dbg_printf(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        return microkit_msginfo_new(-1, 0);
    }
    assert(client_stat[ch - 24]);

    // restore client PD state...
    client_stat[ch - 24] = 0;

    // TODO
    // => call frontend's shell to dump instructions for switching work env
    microkit_notify(15);

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t monitor_call_backup_tsldr_context(microkit_channel ch)
{
    // sanity check for the channel ID
    if (ch < 24 || ch >= 56) {
        microkit_dbg_printf(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        return microkit_msginfo_new(-1, 0);
    }

    tsldr_context_t *context;
    // fetch target trusted loading context...
    context = (tsldr_context_t *)((unsigned char *)TSLDR_CONTEXT_BASE + (ch - 24) * TSLDR_CONTEXT_SIZE);

    // backup trusted loading context in target slot..
    custom_memcpy(&loader_context[ch - 24], context, sizeof(tsldr_context_t));

    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    microkit_dbg_printf(PROGNAME "Received protected message on channel: %d\n", ch);

    /* get the first word of the message */
    seL4_Word monitorcall_number = microkit_mr_get(0);

    seL4_MessageInfo_t ret;

    /* call for the container monitor */
    switch (monitorcall_number) {
    case 1:
        microkit_dbg_printf(PROGNAME "Loading trusted loader and the first client\n");
        ret = monitor_call_debute();
        break;
    //case 2:
    //    microkit_dbg_printf(PROGNAME "Restart trusted loader and a new client\n");
    //    ret = monitor_call_restart(ch - 15);
    //    break;
    case 20:
        microkit_dbg_printf(PROGNAME "Exit to uninstantiated container\n");
        ret = monitor_call_backup_tsldr_context(ch);
        break;
    case 0x100:
        microkit_dbg_printf(PROGNAME "Exit to uninstantiated container\n");
        ret = monitor_call_restore(ch);
        break;
    default:
        /* do nothing for now */
        microkit_dbg_printf(PROGNAME "Undefined container monitor call: %lu\n", monitorcall_number);
        break;
    }

    return ret;
}
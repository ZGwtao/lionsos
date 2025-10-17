
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

// argument passing structure to worker cothread
typedef struct {
    Elf64_Ehdr *eh;
    uint64_t elf_base;
} arg_t;

/* 4KB * 64 in size */
tsldr_md_array_t tsldr_metadata_patched;


acgrp_arr_list_t acgrp_metadata_patched;

/*
 * A shared memory region with container, containing content from tsldr_metadata_patched
 * Will be init each time the container restarts by copying the data from above
 */
uintptr_t tsldr_metadata;
// base of all shared metadata regions
tsldr_md_t *tsldr_metadata_base = (tsldr_md_t *)0xffc0000;

// base of all shared acgroup metadata regions
acgrp_arr_list_t *acgroup_metadata_base = (acgrp_arr_list_t *)0x0ff80000;

seL4_Word system_hash;
unsigned char public_key[PUBLIC_KEY_BYTES];

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

static int patch_iface_sections(void *elf_base, Elf64_Shdr *sh)
{
    // parse the interface section ...
    template_pd_iface_t *ib = (template_pd_iface_t *)(elf_base + (uint64_t)sh->sh_offset);

    const uint8_t *nums = &ib->t1_num;
    const pc_svc_iface_t *types = &ib->type1;
    const uintptr_t (*ifaces[8])[PC_MAX_IFACE_NUM] = {
        &ib->t1_iface, &ib->t2_iface, &ib->t3_iface, &ib->t4_iface,
        &ib->t5_iface, &ib->t6_iface, &ib->t7_iface, &ib->t8_iface
    };

    for (int i = 0; i < PC_MAX_IFACE_TYPE; ++i) {
        if (nums[i] == 0) { // pass...
            continue;
        }
        // fetch the number of interfaces...
        uint8_t n = nums[i];
        // sanity checks (dump unused ones)
        if (n > PC_MAX_IFACE_NUM) {
            n = PC_MAX_IFACE_NUM;
        }
        // fetch interface array
        const uintptr_t *arr = *ifaces[i];
        // check interface type and establish connections...
        switch(types[i]) {
        case FS_IFACE: {
            for (uint8_t j = 0; j < n; ++j) {
                patch_elf_connection(elf_base, "fs_client_container.data", arr[j]);
            }
            break;
        }
        case TIMER_IFACE: {
            for (uint8_t j = 0; j < 1; ++j) {
                patch_elf_connection(elf_base, "timer_client_container.data", arr[j]);
            }
            break;
        }
        case SERIAL_IFACE: {
            for (uint8_t j = 0; j < n; ++j) {
                patch_elf_connection(elf_base, "serial_client_sp0.data", arr[j]);
            }
            break;
        }
        default:
            microkit_dbg_printf(PROGNAME "Unsupported interface type: %d", types[i]);
            break;
        };
    }
    return 0;
}

void monitor_call_debute_lower()
{
    //arg_t *arg_ptr = (arg_t *) microkit_cothread_my_arg();
    //arg_t my_owned_args = *arg_ptr;

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf; //my_owned_args.eh;

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
        return;
    }
    // choose an available container PD in here... 

    acgrp_array_t *acg_arr_ptr;
    size_t acg_num = acgrp_metadata_patched.len;
    microkit_dbg_printf(PROGNAME "number of available PDs that have acg: %d\n", acg_num);
    for (int i = 0; i < acg_num; ++i) {
        acg_arr_ptr = &acgrp_metadata_patched.list[i];
        microkit_dbg_printf(PROGNAME "[acg_arr] - PD idx: %d\n", acg_arr_ptr->pd_idx);
        size_t grp_num = acg_arr_ptr->grp_num;
        for (int j = 0; j < grp_num; ++j) {
            acgrp_t *grp_ptr = &acg_arr_ptr->array[j];
            if (grp_ptr->grp_init != false) {
                microkit_dbg_printf(PROGNAME "[acg_arr][acg: %d]: grp id:   %d\n", j, grp_ptr->grp_idx);
                microkit_dbg_printf(PROGNAME "[acg_arr][acg: %d]: grp type: %d\n", j, grp_ptr->grp_type);

                // iterate all available mapings of this acg...
                StrippedMapping *map_ptr = grp_ptr->mappings;
                for (int k = 0; k < 16; ++k) {
                    if (!map_ptr[k].vaddr) {
                        continue;
                    }
                    microkit_dbg_printf(PROGNAME "  =>: mappings[%d] vaddr: 0x%x\n", k, map_ptr[k].vaddr);
                }
            }
        }
    }

    int cid = 1;

    // finished and picked one
    int err = tsldr_grant_cspace_access(cid);
    if (err != seL4_NoError) {
        microkit_dbg_printf(PROGNAME "Failed to grant cspace access to target container PD\n");
        return;
    }

    uintptr_t payload_base = container_elf + 0x800000 * cid;
    uintptr_t protocon_base = trusted_loader_exec + 0x800000 * cid;
    uintptr_t trampoline_base = trampoline_elf + 0x800000 * cid;
    uintptr_t entry = ehdr->e_entry;

    load_elf((void*)protocon_base, ehdr);
    microkit_dbg_printf(PROGNAME "Copied trusted loader to child PD's memory region\n");

    custom_memcpy((void*)payload_base, (char *)ext_payload_elf, ELF_FILE_SIZE);
    microkit_dbg_printf(PROGNAME "Copied client program to child PD's memory region\n");

    custom_memcpy((void*)trampoline_base, (char *)ext_trampoline_elf, ELF_FILE_SIZE);
    microkit_dbg_printf(PROGNAME "Copied trampoline program to child PD's memory region\n");

    // establish necessary connections for the payload (not really atm,)
    err = patch_iface_sections((void *)payload_base, iface_sh);
    assert(err == seL4_NoError);

    // fill access rights group metadata now for the payload...
    // then the trusted loader will revoke unnecessary capabilities beside the ones we can to establish...
    acgrp_arr_list_t *acg = (acgrp_arr_list_t *)((unsigned char *)acgroup_metadata_base + 0x1000 * cid);
    microkit_dbg_printf(PROGNAME "Curr acg addr: 0x%x, cid: %d\n", acg, cid);

    // parse 

    acg->len = 4;

    uintptr_t mappings[100];
    mappings[0] = 0xfffe00000;
    mappings[1] = 0xfffe11000;
    mappings[2] = 0xfffe01000;
    mappings[3] = 0xfffe12000;
    encode_access_rights_to((unsigned char *)acg + sizeof(size_t), NULL, 0, NULL, 0, mappings, 4);

    /* switch to trusted loader */
    microkit_pd_restart(cid, entry);
    microkit_dbg_printf(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)entry);
}


void monitor_call_restart_lower()
{
    arg_t *arg_ptr = (arg_t *) microkit_cothread_my_arg();
    arg_t my_owned_args = *arg_ptr;

    Elf64_Ehdr *eh = my_owned_args.eh;
    void *elf_base = (void *)my_owned_args.elf_base;

    if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize != sizeof(Elf64_Shdr))
        microkit_dbg_printf(PROGNAME "no section headers present or unexpected shentsize\n");
    if (eh->e_shstrndx == SHN_UNDEF || eh->e_shstrndx >= eh->e_shnum)
        microkit_dbg_printf(PROGNAME "invalid e_shstrndx");

    Elf64_Shdr *iface_sh;
    iface_sh = elf_find_section(elf_base, IFACE_SECTION_NAME);
    if (!iface_sh) {
        microkit_dbg_printf(PROGNAME "Failed to restart container as no iface section specified!\n");
        return;
    }
    // choose an available container PD in here... 

    int cid = 1;

    // finished and picked one
    int err = tsldr_grant_cspace_access(cid);
    if (err != seL4_NoError) {
        microkit_dbg_printf(PROGNAME "Failed to grant cspace access to target container PD\n");
        return;
    }
    err = patch_iface_sections(elf_base, iface_sh);
    assert(err == seL4_NoError);

    /* switch to trusted loader */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf;
    /* set a flag for the trusted loader to check whether to boot or to restart... */
    microkit_dbg_printf(PROGNAME "Restart template PD without reloading trusted loader\n");
    seL4_UserContext ctxt = {0};
    ctxt.pc = ehdr->e_entry;
    ctxt.sp = 0x01000000000;
    err = seL4_TCB_WriteRegisters(
              BASE_TCB_CAP + cid,
              seL4_True,
              0, /* No flags */
              1, /* writing 1 register */
              &ctxt
          );

    if (err != seL4_NoError) {
        microkit_dbg_puts("microkit_pd_restart: error writing TCB registers\n");
        microkit_internal_crash(err);
    }
    microkit_dbg_printf(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)ehdr->e_entry);
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

    // practically we use 32 indices...
    for (int i = 0; i < tsldr_metadata_patched.avails; ++i) {
        // must provide valid hash to 
        if (tsldr_metadata_patched.md_array[i].system_hash != system_hash) {
            // do not initialise unspecified tsldr metadata
            continue;
        }
        // adjust global pointer
        tsldr_metadata = (uintptr_t)(tsldr_metadata_base + i);
        // initialise the target tsldr_metadata
        tsldr_init_metadata(&tsldr_metadata_patched, i);
    }

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

    //arg_t arg1 = { .eh = ehdr, .elf_base = NULL };
    //if (microkit_cothread_spawn(monitor_call_debute_lower, &arg1) == LIBMICROKITCO_NULL_HANDLE) {
    if (microkit_cothread_spawn(monitor_call_debute_lower, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot initialise monitor cothread\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    microkit_dbg_printf(PROGNAME "finished with loading data files\n");
    return microkit_msginfo_new(seL4_NoError, 0);
}


seL4_MessageInfo_t monitor_call_restart(void)
{
    /* reload the trusted loader to the target place */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)ext_protocon_elf;

    load_elf((void*)trusted_loader_exec, ehdr);
    microkit_dbg_printf(PROGNAME "Copied trusted loader to child PD's memory region\n");

    custom_memcpy((void*)container_elf, (char *)ext_payload_elf, ELF_FILE_SIZE);
    microkit_dbg_printf(PROGNAME "Copied client program to child PD's memory region\n");

    arg_t arg1 = { .eh = (Elf64_Ehdr *)container_elf, .elf_base = container_elf };
    if (microkit_cothread_spawn(monitor_call_restart_lower, &arg1) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot initialise monitor cothread\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    microkit_dbg_printf(PROGNAME "finished with loading data files\n");
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_restore(void)
{
    // TODO
    microkit_notify(15);

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
    case 2:
        microkit_dbg_printf(PROGNAME "Restart trusted loader and a new client\n");
        ret = monitor_call_restart();
        break;
    case 0x100:
        microkit_dbg_printf(PROGNAME "Exit to uninstantiated container\n");
        ret = monitor_call_restore();
        break;
    default:
        /* do nothing for now */
        microkit_dbg_printf(PROGNAME "Undefined container monitor call: %lu\n", monitorcall_number);
        break;
    }

    return ret;
}
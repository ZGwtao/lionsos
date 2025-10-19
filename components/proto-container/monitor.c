
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


/* 4KB * 64 in size */
tsldr_md_array_t tsldr_metadata_patched;


acgrp_arr_list_t acgrp_metadata_patched;

// maximum per monitor client container number
#define MAX_PERM_CL_NUM 32
// maximum kinds of connection (acgroup kinds) each container has
#define MAX_PERC_AK_NUM 8
// maximum number of one connection in a container...
#define MAX_PERK_NUM    8

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


typedef struct {
    // records how many connection a PD can have under a type
    int acg_per_type_num[MAX_PERC_AK_NUM];
    //
    uintptr_t acg_attr[MAX_PERC_AK_NUM][MAX_PERK_NUM];
} acg_req_t;

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


static int fetch_iface_section_info(void *elf_base, Elf64_Shdr *sh, acg_req_t *req)
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
        req->acg_per_type_num[types[i]] = n;
        // fetch interface array
        const uintptr_t *arr = *ifaces[i];
        // check interface type and establish connections...

        switch(types[i]) {
        case FS_IFACE:
        case TIMER_IFACE:
        case SERIAL_IFACE: {
            for (uint8_t j = 0; j < n; ++j) {
                req->acg_attr[types[i]][j] = arr[j];
            }
            break;
        }
        default:
            microkit_dbg_printf(PROGNAME "Unsupported interface type: %d", types[i]);
            break;
        };
    }

    int cid = MAX_PERM_CL_NUM;
    // try to get available cid with subset match
    for (int i = 0; i < MAX_PERM_CL_NUM; ++i) {
        if (client_stat[i]) {
            // iterate the PD list to find next available cid...
            continue;
        }
        size_t b = 0;
        for (int j = 0; j < MAX_PERC_AK_NUM; ++j) {
            b |= (req->acg_per_type_num[j] > acg_stat_map[i][j]);
        }
        if (!b) {
            cid = i;
            break;
        }
    }
    return cid;
}


//
// initialise the global acgroup state map
//  => everything is read from the microkit patched metadata
//
static void init_acg_state_map(void)
{
    acgrp_array_t *acg_arr_ptr;
    size_t pd_num = acgrp_metadata_patched.len;

    microkit_dbg_printf(PROGNAME "number of available PDs that have acg: %d\n", pd_num);

    for (int i = 0; i < pd_num; ++i) {
        // fetch a client PD that contains acgroups
        acg_arr_ptr = &acgrp_metadata_patched.list[i];
        //microkit_dbg_printf(PROGNAME "[acg_arr] - PD idx: %d\n", acg_arr_ptr->pd_idx);
        assert(acg_arr_ptr->pd_idx <= MAX_PERM_CL_NUM);

        for (int j = 0; j < acg_arr_ptr->grp_num; ++j) {
            // check each acgroup
            acgrp_t *grp_ptr = &acg_arr_ptr->array[j];
            // if this is a valid group (which means initiliased)
            if (grp_ptr->grp_init != false) {
                // ensure this is a valid type...
                assert(grp_ptr->grp_type <= MAX_PERC_AK_NUM);

                int cur_num = acg_stat_map[acg_arr_ptr->pd_idx][grp_ptr->grp_type];
                // check if we have enough connections of a type
                if (cur_num >= MAX_PERK_NUM) {
                    // halt...
                    microkit_dbg_printf(PROGNAME "current number of %d type acg in PD%d is %d\n", grp_ptr->grp_type, i, cur_num);
                    microkit_internal_crash(-1);
                }
                acg_stat_map[acg_arr_ptr->pd_idx][grp_ptr->grp_type]++;

                microkit_dbg_printf(PROGNAME "[acg_arr][acg: %d]: grp id:   %d\n", j, grp_ptr->grp_idx);
                microkit_dbg_printf(PROGNAME "[acg_arr][acg: %d]: grp type: %d\n", j, grp_ptr->grp_type);

                // iterate all available mapings of this acg...
                StrippedMapping *map_ptr = grp_ptr->mappings;
                for (int k = 0; k < 16; ++k) {
                    if (!map_ptr[k].vaddr) {
                        continue;
                    }
                    microkit_dbg_printf(PROGNAME "  =>: mappings[%d] vaddr: 0x%x, pn: %d, size: 0x%x\n",
                                        k, map_ptr[k].vaddr, map_ptr[k].number_of_pages, map_ptr[k].page_size);
                }
                uint8_t *e_ptr = grp_ptr->channels;
                for (int k = 0; k < 16; ++k) {
                    if (e_ptr[k] >= 62) {
                        continue;
                    }
                    microkit_dbg_printf(PROGNAME "  =>: channel[%d]: %d\n", k, e_ptr[k]);
                }
            }
        }
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
    if (cid >= MAX_PERM_CL_NUM && cid < 0) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "cid available: %d\n", cid);
    //
    // from the iface section, get how many kinds of service the client need, and the number of each kind
    // we plan to use this information to find a matched child PD whose acgroup array can cover
    // 

    // adjust global pointer
    tsldr_metadata = (uintptr_t)(tsldr_metadata_base + cid);
    // initialise the target tsldr_metadata
    tsldr_init_metadata(&tsldr_metadata_patched, cid);


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

    // fill access rights group metadata now for the payload...
    // then the trusted loader will revoke unnecessary capabilities beside the ones we can to establish...
    access_rights_table_t *acg = (access_rights_table_t *)((unsigned char *)acgroup_metadata_base + 0x1000 * cid);

    // so the trusted loader will not care how these access rights entry sit
    // all we have to do is specifying a number of total rights while put them after the number
    // now the job is to collect all access rights from the acgroup from the given acg
    // but still, we need to choose a subset from the acgroup ...

    // this is the current alternative to choose a subset from...
    acgrp_array_t *acg_arr_ptr = &acgrp_metadata_patched.list[cid];

    size_t num_channels = 0;
    size_t num_mappings = 0;
    size_t num_irqs = 0;

    uint64_t channels[100];
    uint64_t mappings[100];
    uint64_t irqs[100];

    // get the subset from the above according to the instructions given in req...
    acgrp_t *grp_array = acg_arr_ptr->array;
    // check all available acgroups...
    for (int i = 0; i < acg_arr_ptr->grp_num; ++i) {
        if (!grp_array[i].grp_init) {
            continue;
        }
        uint8_t type = grp_array[i].grp_type;
        if (!req.acg_per_type_num[type]) {
            continue;
        }
        //
        for (int j = 0; j < 8; ++j) {
            if (grp_array[i].channels[j] >= 62) {
                continue;
            }
            channels[num_channels++] = grp_array[i].channels[j];
        }
        //for (int j = 0; j < 8; ++j) {
        //    if (grp_array[i].irqs[j] >= 62) {
        //        continue;
        //    }
        //    irqs[num_irqs++] = grp_array[i].irqs[j];
        //}
        for (int j = 0; j < 16; ++j) {
            if (!grp_array[i].mappings[j].vaddr) {
                continue;
            }
            mappings[num_mappings++] = grp_array[i].mappings[j].vaddr;
        }
        // update the payload with given data path...
        patch_elf_connection((void *)payload_base, grp_array[i].data_path, req.acg_attr[type][req.acg_per_type_num[type] - 1]);

        microkit_dbg_printf(PROGNAME "update section with offset: 0x%x with %s\n", req.acg_attr[type][req.acg_per_type_num[type] - 1], grp_array[i].data_path);

        // update number of element under given type
        req.acg_per_type_num[type]--;
    }

    acg->len = num_channels + num_irqs + num_mappings;
    encode_access_rights_to((unsigned char *)acg + sizeof(size_t), channels, num_channels, irqs, num_irqs, mappings, num_mappings);

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
    custom_memset(acg_stat_map, 0, sizeof(int) * MAX_PERM_CL_NUM * MAX_PERC_AK_NUM);
    // global acgroup state initialisation...
    init_acg_state_map();
    // global client state initialisation...
    custom_memset(client_stat, 0, sizeof(int) * MAX_PERM_CL_NUM);

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
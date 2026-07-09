
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
#include <pd_io_queue.h>
#include <monitor_vm_layout.h>

#define PROGNAME "[@monitor] "

// these memory regions are shared memory between
//    -> the monitor (container monitor)
//    -> the dynamic pds (protocon - proto containers)
// the size of these memory regions are:
//    -> PC_MONITOR_REGION_SIZE (for one dynamic pd)
//
#define PC_MONITOR_REGION_SIZE MONITOR_VM_LOADER_PROGRAM_SIZE
#define PC_MONITOR_REGION_PROTOCON_ELF_BASE MONITOR_VM_LOADER_PROGRAM_BASE
#define PC_MONITOR_REGION_TRAMPOLINE_ELF_BASE MONITOR_VM_TRAMPOLINE_IMAGE_BASE
#define PC_MONITOR_REGION_CLIENT_PAYLOAD_BASE MONITOR_VM_CONTAINER_IMAGE_BASE

// each elf file is of the same upper size limit
#define ORC_MONITOR_REGION_SIZE (0x800000)
// elf files from orchestrator as external files...
// shared memory with the orchestrator PD
#define ORC_MONITOR_REGION_PROTOCON_ELF_BASE (0x6000000)
#define ORC_MONITOR_REGION_TRAMPOLINE_ELF_BASE (0x6800000)
#define ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE (0x7000000)

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
#define TSLDR_CONTEXT_BASE  MONITOR_VM_LOADER_CONTEXT_BASE
#define TSLDR_CONTEXT_SIZE  MONITOR_VM_LOADER_CONTEXT_SIZE
// this is the base address of the trusted loader metadata region for each dynamic PD (protocon)
// the monitor PD will prepare the metadata for each dynamic PD in this region, and the dynamic PD will read the metadata from this region when it is loading
// this describes the information of all low-level access rights of a dynamic PD, which will be used by the trusted loader to do the actual loading work
#define TSLDR_METADATA_BASE MONITOR_VM_LOADER_METADATA_BASE
#define TSLDR_METADATA_SIZE MONITOR_VM_LOADER_METADATA_SIZE

// if a trusted loader metadata region is initialised, check the hash with this number
// if not match, it means the metadata is not initialised
#define TSLDR_MDINFO_HASH   (0xffff)

// channels id for monitor PD to communicate with the orchestrator PD and the dynamic PDs (protocons)
#define PC_MONITOR_ORCHESTRATOR_CHANNEL (15)
#define PC_MONITOR_PROTOCON_BASE_CHANNEL (24)

// monitor call numbers
#define PC_MONITOR_CALL_DEPLOY (1)
#define PC_MONITOR_CALL_HANG (3)
#define PC_MONITOR_CALL_RESUME (4)
#define PC_MONITOR_CALL_LIST_PROTOCONS (5)
#define PC_MONITOR_CALL_QUERY_PROTOCONS (10)
#define PC_MONITOR_CALL_FLIP_ACL_RULE (17)
#define PC_MONITOR_CALL_BACKUP_CONTEXT (20)
#define PC_MONITOR_CALL_TERMINATE_EXT (6)
#define PC_MONITOR_CALL_TERMINATE (0x100)

// base of all shared os services metadata regions
// the region is for all dynamic PDs, each dynamic PD has a piece between the monitor PD and the dynamic PD itself
// we use it to store the information of OS services requested by the client program
// we will encode the low-level access rights information of OS services requested
// and put the serialised, encoded info into this region.
// the dynamic PD can then read the OS svc information at high-level, while initialise the trusted loader
// with the low-level information provided here...
uintptr_t msvcdb_base = MONITOR_VM_OSSVC_METADATA_BASE;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

__attribute__((__section__(".monitor_svc_db"))) monitor_svcdb_t monitor_svc_db;

#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

typedef struct monitor_deploy_request {
    uint32_t num_req_pc;
} monitor_deploy_request_t;

/*
 * A value of zero means that there is no active deployment request.
 * The request-specific count is copied into a local variable by the
 * deployment cothread before it performs any work.
 */
static uint32_t req_pc_num = 0;
static bool deploy_request_active = false;
static monitor_deploy_request_t deploy_request;

static void monitor_finish_deploy_request(void)
{
    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;
}

#define SET_PROTOCON_AS_INSTANTIATED(C) \
    do { protocon_states[C] = PROTOCON_ACTIVE; } while (0);

#define SET_PROTOCON_AS_HANG(C) \
    do { protocon_states[C] = PROTOCON_HANG; } while (0);

#define SET_PROTOCON_AS_AVAILABLE(C) \
    do { protocon_states[C] = PROTOCON_PASSIVE; } while (0);

seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch);

seL4_Word pd_io_acl_rule = 0;

#define PD_IO_CLIENT_COUNT              4u
#define PD_IO_MONITOR_NOTIFY_BASE       40u
#define PD_IO_MONITOR_SLOT_BASE         0x80000000u
#define PD_IO_MONITOR_SLOT_SIZE         0x00400000u

#define PD_IO_CAPACITY                  512u
#define PD_IO_BUFFER_SIZE               2048u
#define PD_IO_DATA_SIZE                 (PD_IO_CAPACITY * PD_IO_BUFFER_SIZE)

#define PD_IO_CLIENT_TX_FREE_OFFSET     0x000000u
#define PD_IO_CLIENT_RX_FREE_OFFSET     0x003000u
#define PD_IO_CLIENT_TX_ACTIVE_OFFSET   0x006000u
#define PD_IO_CLIENT_RX_ACTIVE_OFFSET   0x009000u
#define PD_IO_CLIENT_TX_DATA_OFFSET     0x00C000u
#define PD_IO_CLIENT_RX_DATA_OFFSET     0x10C000u

static pd_io_link_t client_links[PD_IO_CLIENT_COUNT];

static void monitor_init_client_link(uint32_t cid)
{
    uintptr_t base = PD_IO_MONITOR_SLOT_BASE +
                     cid * PD_IO_MONITOR_SLOT_SIZE;

    /*
     * monitor RX == client TX
     */
    pd_io_direction_init(
        &client_links[cid].rx,
        (pd_io_queue_t *)(base + PD_IO_CLIENT_TX_FREE_OFFSET),
        (pd_io_queue_t *)(base + PD_IO_CLIENT_TX_ACTIVE_OFFSET),
        (void *)(base + PD_IO_CLIENT_TX_DATA_OFFSET),
        PD_IO_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );

    /*
     * monitor TX == client RX
     */
    pd_io_direction_init(
        &client_links[cid].tx,
        (pd_io_queue_t *)(base + PD_IO_CLIENT_RX_FREE_OFFSET),
        (pd_io_queue_t *)(base + PD_IO_CLIENT_RX_ACTIVE_OFFSET),
        (void *)(base + PD_IO_CLIENT_RX_DATA_OFFSET),
        PD_IO_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );

    /*
     * The monitor is the sole shared-state initialisation owner.
     * Do this before the corresponding client starts.
     */
    int err = pd_io_direction_reset_and_fill(&client_links[cid].rx);
    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(PROGNAME "failed to initialise client %u RX: %d\n",
                    cid, err);
        microkit_internal_crash(err);
    }

    err = pd_io_direction_reset_and_fill(&client_links[cid].tx);
    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(PROGNAME "failed to initialise client %u TX: %d\n",
                    cid, err);
        microkit_internal_crash(err);
    }
}

static void monitor_init_all_client_links(void)
{
    for (uint32_t cid = 0; cid < PD_IO_CLIENT_COUNT; cid++) {
        monitor_init_client_link(cid);
    }
}

#define PD_IO_MONITOR_SOURCE_ID UINT8_MAX

static bool monitor_pd_can_receive(uint32_t cid)
{
    return cid < PD_IO_CLIENT_COUNT &&
           cid < PC_CHILD_PER_MONITOR_MAX_NUM &&
           protocon_states[cid] != PROTOCON_PASSIVE;
}

static int monitor_send_pong(uint32_t sender_cid)
{
    static const char pong[] = "pong from monitor";

    /*
     * The response is also a framed pd_io message.
     *
     * source          = monitor
     * bitmap_targets  = original sender
     * payload         = "pong from monitor"
     */
    uint8_t target_bitmap = (uint8_t)(1u << sender_cid);

    int err = pd_io_direction_send(
        &client_links[sender_cid].tx,
        PD_IO_MONITOR_SOURCE_ID,
        target_bitmap,
        pong,
        (uint32_t)sizeof(pong)
    );

    if (err != PD_IO_QUEUE_OK) {
        sddf_printf(
            PROGNAME "failed to send pong to client %u: %d\n",
            sender_cid,
            err
        );
        return err;
    }

    microkit_notify(PD_IO_MONITOR_NOTIFY_BASE + sender_cid);
    return PD_IO_QUEUE_OK;
}

static void monitor_forward_payload(uint32_t sender_cid,
                                    const pd_io_header_t *header,
                                    const void *payload,
                                    uint32_t payload_len)
{
    uint8_t requested_targets = header->bitmap_targets;

    for (uint32_t target_cid = 0;
         target_cid < PD_IO_CLIENT_COUNT;
         target_cid++) {

        if (target_cid == sender_cid) {
            continue;
        }
        if (pd_io_acl_rule) {
        #if 1
            if ((sender_cid % 2) != (target_cid % 2)) {
                continue;
            }
        } else {
            if ((sender_cid % 2) == (target_cid % 2)) {
                continue;
            }
        #endif
        }

        uint8_t target_bit = (uint8_t)(1u << target_cid);

        /*
         * The sender did not select this PD.
         */
        if ((requested_targets & target_bit) == 0) {
            continue;
        }

        /*
         * Do not route to a passive/uninstantiated PD.
         *
         * This follows the requested rule: any state other than
         * PROTOCON_PASSIVE is considered routable.
         */
        if (!monitor_pd_can_receive(target_cid)) {
            sddf_printf(
                PROGNAME
                "not forwarding client %u message to passive client %u\n",
                sender_cid,
                target_cid
            );
            continue;
        }

        /*
         * Preserve the authenticated sender ID rather than trusting
         * header->source supplied by the client.
         *
         * Preserve the original target bitmap so the receiver can see
         * the complete intended recipient set.
         */
        int err = pd_io_direction_send(
            &client_links[target_cid].tx,
            (uint8_t)sender_cid,
            requested_targets,
            payload,
            payload_len
        );

        if (err != PD_IO_QUEUE_OK) {
            sddf_printf(
                PROGNAME
                "failed to forward from client %u to client %u: %d\n",
                sender_cid,
                target_cid,
                err
            );
            continue;
        }

        microkit_notify(PD_IO_MONITOR_NOTIFY_BASE + target_cid);

        sddf_printf(
            PROGNAME
            "forwarded %u bytes from client %u to client %u\n",
            payload_len,
            sender_cid,
            target_cid
        );
    }
}

static void monitor_handle_client_payload(uint32_t sender_cid)
{
    pd_io_header_t header;
    uint8_t payload[PD_IO_BUFFER_SIZE];
    uint32_t payload_len;

    if (sender_cid >= PD_IO_CLIENT_COUNT) {
        sddf_printf(
            PROGNAME "invalid sender client ID: %u\n",
            sender_cid
        );
        return;
    }

    for (;;) {
        int err = pd_io_direction_receive(
            &client_links[sender_cid].rx,
            &header,
            payload,
            sizeof(payload),
            &payload_len
        );

        if (err == PD_IO_QUEUE_EMPTY) {
            break;
        }

        if (err != PD_IO_QUEUE_OK) {
            sddf_printf(
                PROGNAME "client %u receive failed: %d\n",
                sender_cid,
                err
            );

            /*
             * A malformed descriptor/header has already been recycled
             * by pd_io_direction_receive(). Continue draining later
             * messages instead of abandoning the queue.
             */
            continue;
        }

        /*
         * Do not trust a client-provided source field for routing.
         * The channel that delivered the notification identifies the
         * actual sender.
         */
        if (header.source != (uint8_t)sender_cid) {
            sddf_printf(
                PROGNAME
                "client %u supplied mismatched source ID %u; "
                "using channel-derived sender ID\n",
                sender_cid,
                header.source
            );
        }
        assert(header.payload_size == payload_len);

        /*
         * A framed message with zero payload is interpreted as a ping.
         */
        if (payload_len == 0) {
            (void)monitor_send_pong(sender_cid);
            continue;
        }

        monitor_forward_payload(
            sender_cid,
            &header,
            payload,
            payload_len
        );
    }
}

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


static inline void monitor_main_notify_orchestrator()
{
    microkit_notify(PC_MONITOR_ORCHESTRATOR_CHANNEL);
}

void monitor_main_load_elfs_into_protocon(int cid)
{
    uintptr_t payload_base = monitor_vm_region_base(&monitor_vm_layout.container_image, cid);
    uintptr_t protocon_base = monitor_vm_region_base(&monitor_vm_layout.loader_program, cid);
    uintptr_t trampoline_base = monitor_vm_region_base(&monitor_vm_layout.trampoline_image, cid);

    tsldr_miscutil_load_elf((void*)protocon_base, (const Elf64_Ehdr *)ORC_MONITOR_REGION_PROTOCON_ELF_BASE);
    TSLDR_DBG_PRINT(PROGNAME "Copied proto container to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)payload_base, (char *)ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE, ORC_MONITOR_REGION_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied client program to child PD's memory region\n");

    tsldr_miscutil_memcpy((void*)trampoline_base, (char *)ORC_MONITOR_REGION_TRAMPOLINE_ELF_BASE, ORC_MONITOR_REGION_SIZE);
    TSLDR_DBG_PRINT(PROGNAME "Copied trampoline program to child PD's memory region\n");
}


void monitor_call_deploy_protocon_second_half(void)
{
    monitor_deploy_request_t *request = microkit_cothread_my_arg();
    uint32_t num_req_pc = request->num_req_pc;

    TSLDR_DBG_PRINT(PROGNAME "entry of monitor_call_deploy_protocon_second_half\n");

    if (num_req_pc < MIN_REQ_PC_NUM || num_req_pc > MAX_REQ_PC_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME "Invalid active deployment request count: %u\n",
            num_req_pc
        );
        monitor_finish_deploy_request();
        monitor_main_notify_orchestrator();
        return;
    }

    // FIXME: should not use shared memory to determine state...
    Elf64_Ehdr *payload_eh = (Elf64_Ehdr *)ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE;
    if (payload_eh->e_shoff == 0 ||
        payload_eh->e_shnum == 0 ||
        payload_eh->e_shentsize != sizeof(Elf64_Shdr) ||
        payload_eh->e_shstrndx == SHN_UNDEF ||
        payload_eh->e_shstrndx >= payload_eh->e_shnum)
    {
        TSLDR_DBG_PRINT(
            PROGNAME
            "no section headers present or unexpected shentsize "
            "or invalid e_shstrndx\n"
        );
        monitor_finish_deploy_request();
        monitor_main_notify_orchestrator();
        return;
    }

    protocon_svc_req_t req;

    Elf64_Shdr *user_defined_svc_section =
        (Elf64_Shdr *)tsldr_miscutil_find_section_from_elf(
            (void *)ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE,
            PC_SVC_DESC_SECTION_NAME
        );
    if (!user_defined_svc_section) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Failed to restart container as no iface section specified\n"
        );
        monitor_finish_deploy_request();
        monitor_main_notify_orchestrator();
        return;
    }

    for (uint32_t to_deploy = 0; to_deploy < num_req_pc; ++to_deploy) {
        int cid = monitor_match_ossvc_request_with_available_pd(
            (void *)ORC_MONITOR_REGION_CLIENT_PAYLOAD_BASE,
            user_defined_svc_section,
            &req,
            protocon_states
        );
        if (cid >= PC_CHILD_PER_MONITOR_MAX_NUM || cid < 0) {
            TSLDR_DBG_PRINT(
                PROGNAME
                "Failed to find suitable container for payload\n"
            );
            TSLDR_DBG_PRINT(
                PROGNAME "Requested PC number: %u\n",
                num_req_pc
            );
            monitor_finish_deploy_request();
            monitor_main_notify_orchestrator();
            return;
        }
        TSLDR_DBG_PRINT(PROGNAME "cid available: %d\n", cid);

        monitor_main_load_elfs_into_protocon(cid);

        Elf64_Ehdr *client_payload_eh =
            (Elf64_Ehdr *)monitor_vm_region_base(
                &monitor_vm_layout.container_image,
                cid
            );

        monitor_patch_payload_with_ossvc_info(
            cid,
            &req,
            (uintptr_t)client_payload_eh,
            msvcdb_base
        );

        tsldr_main_monitor_init_mdinfo(
            (tsldr_mdinfodb_t *)microkit_trusted_loading_info,
            cid,
            (void *)monitor_vm_region_base(
                &monitor_vm_layout.loader_metadata,
                cid
            )
        );

        tsldr_miscutil_memcpy(
            (char *)monitor_vm_region_base(
                &monitor_vm_layout.loader_context,
                cid
            ),
            &protocon_ctx_db[cid],
            sizeof(tsldr_context_t)
        );

        tsldr_main_monitor_privilege_pd(cid);

        SET_PROTOCON_AS_INSTANTIATED(cid)

        Elf64_Ehdr *protocon_eh =
            (Elf64_Ehdr *)ORC_MONITOR_REGION_PROTOCON_ELF_BASE;

        microkit_pd_restart(cid, protocon_eh->e_entry);
        TSLDR_DBG_PRINT(
            PROGNAME
            "Started child PD at entrypoint address: %x\n",
            protocon_eh->e_entry
        );
    }

    monitor_finish_deploy_request();
    monitor_main_notify_orchestrator();
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
    // tsldr_miscutil_memset(protocon_states, PROTOCON_PASSIVE, sizeof(int) * PC_CHILD_PER_MONITOR_MAX_NUM);
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        protocon_states[i] = PROTOCON_PASSIVE;
    }

    req_pc_num = 0;
    deploy_request.num_req_pc = 0;
    deploy_request_active = false;

    // clean all loader context...
    tsldr_miscutil_memset(protocon_ctx_db, 0, sizeof(tsldr_context_t) * PC_CHILD_PER_MONITOR_MAX_NUM);

    stack_ptrs_arg_array_t costacks = { (uintptr_t) monitor_costack1, (uintptr_t) monitor_costack2 };
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);
    monitor_main_cothread_spawn(monitor_main_init_storage, NULL, " failed to spawn thread for storage initialisation.\n");

    monitor_init_all_client_links();
}

 void notified(microkit_channel ch)
 {
    if (ch >= PD_IO_MONITOR_NOTIFY_BASE &&
        ch < PD_IO_MONITOR_NOTIFY_BASE + PD_IO_CLIENT_COUNT) {
        uint32_t cid = ch - PD_IO_MONITOR_NOTIFY_BASE;
        monitor_handle_client_payload(cid);
        return;
    }

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
    monitor_main_notify_orchestrator();

    monitor_call_restore_protocon(child + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}


seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    monitor_main_handle_fault(child, msginfo);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}

seL4_MessageInfo_t monitor_call_deploy_protocon_first_half(seL4_Word num_req_pc)
{
    if (num_req_pc < MIN_REQ_PC_NUM || num_req_pc > MAX_REQ_PC_NUM) {
        TSLDR_DBG_PRINT(
            PROGNAME "Invalid requested PC count: %lu\n",
            (unsigned long)num_req_pc
        );
        return microkit_msginfo_new(-1, 0);
    }

    /*
     * The orchestrator ELF buffers and deployment context are shared.
     * Only one deployment request may be active at a time.
     */
    if (deploy_request_active) {
        TSLDR_DBG_PRINT(
            PROGNAME
            "Rejected deploy request: another deployment is still active\n"
        );
        return microkit_msginfo_new(-1, 0);
    }

    TSLDR_DBG_PRINT(
        PROGNAME "entry of monitor_call_deploy_protocon_first_half\n"
    );

    seL4_Word err;
    tsldr_main_check_elf_integrity(
        ORC_MONITOR_REGION_PROTOCON_ELF_BASE,
        &err
    );
    if (err) {
        TSLDR_DBG_PRINT(
            PROGNAME "Integrity check failed for protocon elf\n"
        );
        monitor_main_notify_orchestrator();
        return microkit_msginfo_new(err, 0);
    }

    deploy_request.num_req_pc = (uint32_t)num_req_pc;
    req_pc_num = (uint32_t)num_req_pc;
    deploy_request_active = true;

    TSLDR_DBG_PRINT(
        PROGNAME "Integrity check passed for protocon elf\n"
    );

    if (microkit_cothread_spawn(
            monitor_call_deploy_protocon_second_half,
            &deploy_request
        ) == LIBMICROKITCO_NULL_HANDLE)
    {
        TSLDR_DBG_PRINT(
            PROGNAME
            "cannot initialise monitor cothread for monitor call.\n"
        );
        monitor_finish_deploy_request();
        monitor_main_notify_orchestrator();
        return microkit_msginfo_new(-1, 0);
    }

    microkit_cothread_yield();
    return microkit_msginfo_new(seL4_NoError, 0);
}


int monitor_main_get_cid_from_channel(microkit_channel ch)
{
    if (ch < PC_MONITOR_PROTOCON_BASE_CHANNEL ||
        ch >= (PC_MONITOR_PROTOCON_BASE_CHANNEL + PC_CHILD_PER_MONITOR_MAX_NUM))
    {
        TSLDR_DBG_PRINT(PROGNAME "Received signal from non-client PD that tries to uninstantiate client PD!\n");
        // microkit_internal_crash(-1);
        return 0xffc;
    }
    return ch - PC_MONITOR_PROTOCON_BASE_CHANNEL;
}


seL4_MessageInfo_t monitor_call_restore_protocon(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);
    if (cid == 0xffc) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to restore given with ch: %d\n", ch);
    } else {
        // assert(protocon_states[cid] == PROTOCON_ACTIVE);
        SET_PROTOCON_AS_AVAILABLE(cid)
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_stop_and_restore_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for stop and restore\n");
        return microkit_msginfo_new(-1, 0);
    }
    microkit_pd_stop(target_pd_id);
    return monitor_call_restore_protocon(target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL);
}

seL4_MessageInfo_t monitor_call_hang_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for hang\n");
        return microkit_msginfo_new(-1, 0);
    }
    int cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    int cid = monitor_main_get_cid_from_channel(cid_to_check);
    if (cid == 0xffc) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to restore given with ch: %d\n", cid_to_check);
    } else {
        if (protocon_states[cid] != PROTOCON_ACTIVE) {
            TSLDR_DBG_PRINT(PROGNAME "PD to hang must be active first!\n");
        } else {
            SET_PROTOCON_AS_HANG(cid)
            microkit_pd_stop(target_pd_id);
        }
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_resume_protocon(microkit_channel ch)
{
    int target_pd_id = ch;
    if (target_pd_id < 0 || target_pd_id >= PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id given for resume\n");
        return microkit_msginfo_new(-1, 0);
    }
    int cid_to_check = target_pd_id + PC_MONITOR_PROTOCON_BASE_CHANNEL;
    int cid = monitor_main_get_cid_from_channel(cid_to_check);
    if (cid == 0xffc) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid PD id to resume given with ch: %d\n", cid_to_check);
    } else {
        if (protocon_states[cid] != PROTOCON_HANG) {
            TSLDR_DBG_PRINT(PROGNAME "Invalid PD state to resume!\n");
        } else {
            /* static inline void microkit_pd_resume(microkit_child pd) */
            seL4_Error err;
            err = seL4_TCB_Resume(BASE_TCB_CAP + target_pd_id);
            if (err != seL4_NoError) {
                microkit_dbg_puts("microkit_pd_resume: error resuming TCB '");
                microkit_dbg_put32(target_pd_id);
                microkit_dbg_puts("'\n");
                microkit_internal_crash(err);
            }
            SET_PROTOCON_AS_INSTANTIATED(cid)
        }
    }
    monitor_main_notify_orchestrator();
    return microkit_msginfo_new(seL4_NoError, 0);
}

static inline void monitor_main_list_protocon_states(int num_protocons)
{
    if (num_protocons > PC_CHILD_PER_MONITOR_MAX_NUM) {
        TSLDR_DBG_PRINT(PROGNAME "Invalid number of protocons to list: %d\n", num_protocons);
        return;
    }
    for (int i = 0; i < num_protocons; ++i) {
        sddf_printf("[*] dynamic-PD [id=%d] has state: ", i);
        switch (protocon_states[i]) {
            case PROTOCON_ACTIVE:
                sddf_printf("in-use");
                break;
            case PROTOCON_PASSIVE:
                sddf_printf("avail");
                break;
            case PROTOCON_HANG:
                sddf_printf("hang");
                break;
            default:
                sddf_printf("unknown: %d", protocon_states[i]);
        };
        sddf_printf("\n");
    }
}

seL4_MessageInfo_t monitor_call_list_protocons()
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);
    return microkit_msginfo_new(seL4_NoError, 0);
}

seL4_MessageInfo_t monitor_call_query_protocons(microkit_channel ch)
{
    // FIXME: should not hardcode the number of pc to list
    monitor_main_list_protocon_states(4);

    seL4_Word self_id = monitor_main_get_cid_from_channel(ch);
    seL4_Word bitmap = 0;
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if ((protocon_states[i] == PROTOCON_ACTIVE || protocon_states[i] == PROTOCON_HANG) && i != self_id) {
            bitmap |= (1ULL << i);
        }
    }
    seL4_MessageInfo_t ret = microkit_msginfo_new(seL4_NoError, 2);
    microkit_mr_set(0, bitmap);
    microkit_mr_set(1, monitor_main_get_cid_from_channel(ch));
    if (bitmap == 0) {
        sddf_printf("No dynamic PDs are currently available for communication\n");
    }
    return ret;
}

seL4_MessageInfo_t monitor_call_backup_protocon_loading_context(microkit_channel ch)
{
    int cid = monitor_main_get_cid_from_channel(ch);

    tsldr_context_t *context = \
        (tsldr_context_t *)monitor_vm_region_base(&monitor_vm_layout.loader_context, cid);

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
        seL4_Word num_req_pc = microkit_mr_get(1);
        ret = monitor_call_deploy_protocon_first_half(num_req_pc);
        break;
    case PC_MONITOR_CALL_FLIP_ACL_RULE:
        pd_io_acl_rule = !pd_io_acl_rule;
        break;
    case PC_MONITOR_CALL_BACKUP_CONTEXT:
        TSLDR_DBG_PRINT(PROGNAME "Backing up trusted loading context for dynamic PD with ID: %d\n", monitor_main_get_cid_from_channel(ch));
        ret = monitor_call_backup_protocon_loading_context(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE:
        TSLDR_DBG_PRINT(PROGNAME "Exit and uninstantiate a dynamic PD\n");
        /* this is usually invoked from client side, so ch id = pd id + 15 */
        /* given the above condition, use monitor_call_restore_protocon? */
#if 0
        ret = monitor_call_restore_protocon(ch);
#else
        ret = monitor_call_stop_and_restore_protocon(ch - PC_MONITOR_PROTOCON_BASE_CHANNEL);
#endif
        break;
    case PC_MONITOR_CALL_HANG: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Hang dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_hang_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_RESUME: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Resume dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_resume_protocon(target_pd_id);
        break;
    }
    case PC_MONITOR_CALL_LIST_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "List states of dynamic PDs\n");
        ret = monitor_call_list_protocons();
        break;
    case PC_MONITOR_CALL_QUERY_PROTOCONS:
        TSLDR_DBG_PRINT(PROGNAME "Query states of dynamic PDs\n");
        ret = monitor_call_query_protocons(ch);
        break;
    case PC_MONITOR_CALL_TERMINATE_EXT: {
        seL4_Word target_pd_id = seL4_GetMR(1);
        TSLDR_DBG_PRINT(PROGNAME "Terminate dynamic PD with ID: %d\n", target_pd_id);
        ret = monitor_call_stop_and_restore_protocon(target_pd_id);
        break;
    }
    default:
        /* do nothing for now */
        TSLDR_DBG_PRINT(PROGNAME "Undefined container monitor call: %d\n", call_id);
        break;
    }

    return ret;
}


seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    return monitor_main_handle_pccall(ch);
}



#include <microkit.h>
#include <stdarg.h>
#include <string.h>
#include <sddf/util/printf.h>
#include <elf_utils.h>
#include <libtrustedlo.h>

#include <libmicrokitco.h>
#include <lions/fs/config.h>
#include <fs_helpers.h>

#define PROGNAME "[@monitor] "

uintptr_t trusted_loader_exec = 0x4000000;
uintptr_t trampoline_elf = 0xD800000;
uintptr_t container_elf = 0xA00000000;

__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[PC_WORKER_THREAD_NUM + 1];

uint64_t _worker_thread_stack_one = 0xA0000000;
uint64_t _worker_thread_stack_two = 0xB0000000;


request_metadata_t request_metadata[FS_QUEUE_CAPACITY];
buffer_metadata_t buffer_metadata[FS_QUEUE_CAPACITY];

bool fs_init;


/* 4KB in size */
tsldr_md_t tsldr_metadata_patched;
/*
 * A shared memory region with container, containing content from tsldr_metadata_patched
 * Will be init each time the container restarts by copying the data from above
 */
uintptr_t tsldr_metadata = 0x1000000;

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


void init(void)
{
    microkit_dbg_puts("Hello from monitor\n");
    sddf_printf("Test serial driver\n");

    assert(fs_config_check_magic(&fs_config));
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_init = false;

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
    seL4_Error error = tsldr_grant_cspace_access();
    if (error != seL4_NoError) {
        return microkit_msginfo_new(error, 0);
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)0x6000000;

    if (custom_memcmp(ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        microkit_dbg_printf(PROGNAME "Data in shared memory region must be an ELF file\n");
        return microkit_msginfo_new(seL4_InvalidArgument, 0);
    }

    microkit_dbg_printf(PROGNAME "Verified ELF header\n");

    /* init metadata for proto-container's tsldr */
    tsldr_init_metadata(&tsldr_metadata_patched);

    load_elf((void*)trusted_loader_exec, ehdr);
    microkit_dbg_printf(PROGNAME "Copied trusted loader to child PD's memory region\n");

    custom_memcpy((void*)container_elf, (char *)0xb000000, 0x800000);
    microkit_dbg_printf(PROGNAME "Copied client program to child PD's memory region\n");

    custom_memcpy((void*)trampoline_elf, (char *)0x6800000, 0x800000);
    microkit_dbg_printf(PROGNAME "Copied trampoline program to child PD's memory region\n");

    // Restart the child PD at the entry point
    microkit_pd_restart(PD_TEMPLATE_CHILD_TCB, ehdr->e_entry);
    microkit_dbg_printf(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)ehdr->e_entry);

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
        //ret = monitor_call_restart();
        break;
    default:
        /* do nothing for now */
        microkit_dbg_printf(PROGNAME "Undefined container monitor call: %lu\n", monitorcall_number);
        break;
    }

    return ret;
}
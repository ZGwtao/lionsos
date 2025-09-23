/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <elf_utils.h>
#include <stdarg.h>
#include <stdint.h>
#include <microkit.h>
#include <k_r_malloc.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>

#include <libmicrokitco.h>
#include <lions/fs/config.h>
#include <pico_vfs.h>

#define PROGNAME "[@frontend] "

uintptr_t shared1 = 0x4000000;
uintptr_t shared2 = 0xb000000;
uintptr_t shared3 = 0x6000000;

//__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
//__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[PC_WORKER_THREAD_NUM + 1];

uint64_t _worker_thread_stack_one = 0xA0000000;
uint64_t _worker_thread_stack_two = 0xB0000000;

request_metadata_t request_metadata[FS_QUEUE_CAPACITY];
buffer_metadata_t buffer_metadata[FS_QUEUE_CAPACITY];


//serial_queue_handle_t serial_rx_queue_handle;
//serial_queue_handle_t serial_tx_queue_handle;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

bool fs_init;


#define POOL_SIZE   16384
static char morecore[POOL_SIZE];
pool_cookie_t *cookie;



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

void load_entrypoint(void)
{
    while(!fs_init) {
        microkit_cothread_yield();
    }

    uint64_t dir_fd = opendir(".");
    microkit_dbg_printf(PROGNAME "(dir open): fd is %d opened\n", dir_fd);

    int err;
    uint64_t pos;

    pos = pico_vfs_readfile2buf((void *)shared1, "protocon.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "Wrote proto-container's ELF file into memory\n");

    pos = pico_vfs_readfile2buf((void *)shared2, "micropython.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "Wrote client's ELF file into memory\n");

    pos = pico_vfs_readfile2buf((void *)shared3, "trampoline.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "Wrote trampoline's ELF file into memory\n");

    microkit_dbg_printf(PROGNAME "Making ppc to container monitor backend\n");

    microkit_msginfo info;
    seL4_Error error;

    microkit_mr_set(0, 1);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
#if 0
    //custom_memcpy((void *)shared2, _test, _test_end - _test);
    microkit_dbg_printf(PROGNAME "Wrote test's ELF file into memory\n");

    microkit_mr_set(0, 2);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
#endif
    while(1) {
        //microkit_dbg_printf(PROGNAME "Ready to handle tasks\n");
        while (1) {
            microkit_cothread_yield();
        }
    }
}


void init(void)
{
    microkit_dbg_printf(PROGNAME "Entered init\n");

    cookie = mspace_bootstrap_allocator(POOL_SIZE, morecore);
    if (!cookie) {
        microkit_internal_crash(-1);
    }
    microkit_dbg_printf(PROGNAME "Init memory allocator\n");
#if 0
    char *c = mspace_k_r_malloc_alloc(&cookie->k_r_malloc, sizeof(c));
    if (c != NULL) {
        *c = 'a';
        mspace_k_r_malloc_free(&cookie->k_r_malloc, c);
    }
#endif
    //assert(serial_config_check_magic(&serial_config));
    //assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));
#if 0
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
#endif
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

    if (microkit_cothread_spawn(load_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot initialise frontend cothread1\n");
        microkit_internal_crash(-1);
    }

    microkit_cothread_yield();

    microkit_dbg_printf(PROGNAME "Finished init\n");
}

void notified(microkit_channel ch)
{
    //microkit_dbg_printf(PROGNAME "Received notification on channel: %d\n", ch);

    fs_process_completions();

    microkit_cothread_recv_ntfn(ch);
}

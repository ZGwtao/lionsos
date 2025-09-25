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

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
//__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[PC_WORKER_THREAD_NUM + 1];

uint64_t _worker_thread_stack_one = 0xA0000000;
uint64_t _worker_thread_stack_two = 0xB0000000;

request_metadata_t request_metadata[FS_QUEUE_CAPACITY];
buffer_metadata_t buffer_metadata[FS_QUEUE_CAPACITY];


serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

bool fs_init;


#define POOL_SIZE   16384
static char morecore[POOL_SIZE];
pool_cookie_t *cookie;


static void print_prompt(void)
{
    sddf_putchar_unbuffered('f');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('o');
    sddf_putchar_unbuffered('n');
    sddf_putchar_unbuffered('t');
    sddf_putchar_unbuffered('e');
    sddf_putchar_unbuffered('n');
    sddf_putchar_unbuffered('d');
    sddf_putchar_unbuffered('>');
    sddf_putchar_unbuffered('$');
    sddf_putchar_unbuffered(' ');
}


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
    int err;
    uint64_t pos;

    pos = pico_vfs_readfile2buf((void *)shared1, "protocon.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "Wrote proto-container's ELF file into memory\n");

    pos = pico_vfs_readfile2buf((void *)shared3, "trampoline.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    microkit_dbg_printf(PROGNAME "Wrote trampoline's ELF file into memory\n");

    sddf_putchar_unbuffered('\n');
    print_prompt();
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
    assert(serial_config_check_magic(&serial_config));
    //assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

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

#define INPUT_BUF_SIZE 128
#define FNAME_BUF_SIZE 64

static char input_buf[INPUT_BUF_SIZE];
static char fname_buf[FNAME_BUF_SIZE];
static size_t input_len = 0;


void load_elf_payload(void)
{
    while(!fs_init) {
        microkit_cothread_yield();
    }
    microkit_dbg_printf(PROGNAME "entry of load_elf_payload\n");

    int err;
    uint64_t pos;
    microkit_msginfo info;
    seL4_Error error;

    pos = pico_vfs_readfile2buf((void *)shared2, fname_buf, &err);
    if (err != seL4_NoError) {
        microkit_dbg_printf(PROGNAME "Failed to read %s\n", fname_buf);
        return;
    }
    microkit_dbg_printf(PROGNAME "Wrote test's ELF file into memory\n");

    microkit_mr_set(0, 1);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
}

/* ----- Command handlers ----- */

static void parse_start_cmd(const char *arg)
{
    // Skip leading spaces
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("\nInvalid usage: 'start' requires a filename\n> ");
        return;
    }

    // Extract filename (stop at space or NUL)
    const char *end = arg;
    while (*end && *end != ' ') end++;

    size_t len = end - arg;
    if (len >= sizeof(fname_buf)) len = sizeof(fname_buf) - 1;

    memset(fname_buf, 0, FNAME_BUF_SIZE);
    memcpy(fname_buf, arg, len);
    fname_buf[len] = '\0';

    sddf_printf("\nValid command: start %s\n> ", fname_buf);

    if (microkit_cothread_spawn(load_elf_payload, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        microkit_dbg_printf(PROGNAME "Cannot frontend cothread to load payload\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();
}

static void handle_line(const char *line)
{
    while (*line == ' ') line++;  // skip spaces

    if (*line == '\0') {
        // empty input
        return;
    }

    if (strncmp(line, "start", 5) == 0) {
        const char *after = line + 5;
        if (*after == '\0') {
            sddf_printf("Invalid usage: 'start' requires a filename\n");
        } else if (*after == ' ') {
            parse_start_cmd(after);
        } else {
            sddf_printf("Invalid command format\n");
        }
    } else {
        sddf_printf("Unknown command: %s\n", line);
    }
}

/* ----- Microkit callback ----- */

void notified(microkit_channel ch)
{
    fs_process_completions();
    microkit_cothread_recv_ntfn(ch);

    if (ch == serial_config.rx.id) {
        char c;
        while (!serial_dequeue(&serial_rx_queue_handle, &c)) {
            if (c == '\r') {
                // end of line
                sddf_putchar_unbuffered('\r');
                sddf_putchar_unbuffered('\n');

                input_buf[input_len] = '\0';
                handle_line(input_buf);

                // reset buffer and show prompt
                input_len = 0;
                print_prompt();
            } else if (c == '\b' || c == 127) {
                // backspace
                if (input_len > 0) {
                    input_len--;
                    sddf_putchar_unbuffered('\b');
                    sddf_putchar_unbuffered(' ');
                    sddf_putchar_unbuffered('\b');
                }
            } else {
                // normal char
                if (input_len < INPUT_BUF_SIZE - 1) {
                    input_buf[input_len++] = c;
                    sddf_putchar_unbuffered(c); // immediate echo
                } else {
                    sddf_printf("\nInput too long, buffer cleared\n");
                    input_len = 0;
                    print_prompt();
                }
            }
        }
    }
}
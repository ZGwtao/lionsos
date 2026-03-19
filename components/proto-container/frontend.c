/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <microkit.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>

#include <libtrustedlo.h>
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

//uint64_t _worker_thread_stack_one = 0xA0000000;
//uint64_t _worker_thread_stack_two = 0xB0000000;

static char mp_stack1[0x10000];
static char mp_stack2[0x10000];
static co_control_t co_controller_mem;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

fs_queue_t *fs_command_queue;
fs_queue_t *fs_completion_queue;
char *fs_share;

bool fs_init;

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


void shell_inst_epilogue(void);


void test_entrypoint(void)
{
    TSLDR_DBG_PRINT(PROGNAME "(fs mount) start fs initialisation\n");
    fs_cmpl_t completion;
    int err = fs_command_blocking(&completion, (fs_cmd_t){ .type = FS_CMD_INITIALISE });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        TSLDR_DBG_PRINT(PROGNAME "MP|ERROR: Failed to mount\n");
    }
    fs_init = true;

    TSLDR_DBG_PRINT(PROGNAME "(fs mount) finished fs initialisation\n");
}

void load_entrypoint(void)
{
    while(!fs_init) {
        microkit_cothread_yield();
    }
#if 0
    int err;
    pico_vfs_readfile2buf((void *)shared1, "protocon.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote proto-container's ELF file into memory\n");

    pico_vfs_readfile2buf((void *)shared3, "trampoline.elf", &err);
    if (err != seL4_NoError) {
        // halt...
        while (1);
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote trampoline's ELF file into memory\n");
#endif
    sddf_putchar_unbuffered('\n');
    print_prompt();
}


microkit_channel mp_curr_wait_ch;

/* Configures the different wait behaviour of Micropython when keyboard
interrupts are received. */
typedef enum mp_cothread_wait_type {
    /**
     * Micropython will not be awoken until a notification is received on the
     * wait channel. Pending keyboard interrupts are not processed.
     */
    MP_WAIT_NO_INTERRUPT = 0,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will still be
     * received by the Micropython cothread.
     */
    MP_WAIT_RECV,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will be dropped.
     * NOTE: this will not stack to more than one notification drop is
     * Micropython is interrupted more than once.
     */
    MP_WAIT_DROP,
    /**
     * Micropython will be awoken early if a keyboard interrupt is received. The
     * subsequent scheduled notification that was emulated will be dropped,
     * unless the Micropython cothread waits on the channel again.
     */
    MP_WAIT_DROP_UNTIL_WAIT
} mp_cothread_wait_type_t;

typedef struct mp_cothread_ch_state {
    bool drop;
    mp_cothread_wait_type_t type;
} mp_cothread_ch_state_t;

mp_cothread_ch_state_t mp_channels[MICROKIT_MAX_CHANNELS];

void mp_cothread_wait(microkit_channel ch,
                      mp_cothread_wait_type_t handle_interrupt)
{
    if (mp_channels[ch].type == MP_WAIT_DROP_UNTIL_WAIT) {
        mp_channels[ch].drop = false;
    }

    mp_channels[ch].type = handle_interrupt;
    mp_curr_wait_ch = ch;
    microkit_cothread_wait_on_channel(ch);
    
    if (handle_interrupt != MP_WAIT_NO_INTERRUPT) {
        /* Ensure interrupts received while waiting are processed and raised. */
        //mp_handle_pending(true);
        TSLDR_DBG_PRINT(PROGNAME "mp_cothread_wait: woke up from wait on channel %d with interrupt handling type %d\n", ch, handle_interrupt);
    }
}


static void blocking_wait(microkit_channel ch) {
    mp_cothread_wait(ch, MP_WAIT_NO_INTERRUPT);
}


void init(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Entered init\n");

    assert(serial_config_check_magic(&serial_config));


    TSLDR_DBG_PRINT(PROGNAME "check serial config\n");

    //assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));


    TSLDR_DBG_PRINT(PROGNAME "check fs config\n");

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    
    fs_set_blocking_wait(blocking_wait);
    fs_command_queue = fs_config.server.command_queue.vaddr;
    fs_completion_queue = fs_config.server.completion_queue.vaddr;
    fs_share = fs_config.server.share.vaddr;
    fs_init = false;

    TSLDR_DBG_PRINT(PROGNAME "finalised init\n");

    stack_ptrs_arg_array_t costacks = { (uintptr_t) mp_stack1, (uintptr_t) mp_stack2 };
    TSLDR_DBG_PRINT(PROGNAME "%x, %x\n", mp_stack1, costacks[0]);
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);

    TSLDR_DBG_PRINT(PROGNAME "XXXX\n");

    if (microkit_cothread_spawn(test_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot initialise frontend cothread2\n");
        microkit_internal_crash(-1);
    }
    
    //microkit_cothread_yield();
#if 0
    if (microkit_cothread_spawn(load_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot initialise cothread1\n");
        microkit_internal_crash(-1);
    }
#endif
    microkit_cothread_yield();

    TSLDR_DBG_PRINT(PROGNAME "Finished init\n");

    sddf_putchar_unbuffered('\n');
    print_prompt();
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
    TSLDR_DBG_PRINT(PROGNAME "entry of load_elf_payload\n");

    int err;
    microkit_msginfo info;
    seL4_Error error;

    pico_vfs_readfile2buf((void *)shared2, fname_buf, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to read %s\n", fname_buf);
        shell_inst_epilogue();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote test's ELF file into memory\n");

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
        TSLDR_DBG_PRINT(PROGNAME "Cannot frontend cothread to load payload\n");
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


void shell_inst_epilogue(void)
{
    sddf_putchar_unbuffered('T');
    sddf_putchar_unbuffered('y');
    sddf_putchar_unbuffered('p');
    sddf_putchar_unbuffered('e');
    sddf_putchar_unbuffered(':');
    sddf_putchar_unbuffered(' ');
    sddf_putchar_unbuffered('\"');
    sddf_putchar_unbuffered('C');
    sddf_putchar_unbuffered('t');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('l');
    sddf_putchar_unbuffered(' ');
    sddf_putchar_unbuffered('\\');
    sddf_putchar_unbuffered(' ');
    sddf_putchar_unbuffered('0');
    sddf_putchar_unbuffered('\"');
    sddf_putchar_unbuffered(' ');
    sddf_putchar_unbuffered('t');
    sddf_putchar_unbuffered('o');
    sddf_putchar_unbuffered(' ');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('e');
    sddf_putchar_unbuffered('t');
    sddf_putchar_unbuffered('u');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('n');
    sddf_putchar_unbuffered('\n');
}


/* ----- Microkit callback ----- */

void notified(microkit_channel ch)
{
    fs_process_completions(NULL);
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
    } else if (ch == 30) { /* notification from monitor */
        shell_inst_epilogue();
    }
}
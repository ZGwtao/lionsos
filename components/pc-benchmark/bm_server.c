/*
 * Copyright 2026, UNSW
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

#define PROGNAME "[@pcbench_server] "

#define SERVER_MONITOR_PAYLOAD_REGION_BASE (0xb000000)
#define SERVER_MONITOR_PROTOCON_REGION_BASE (0x4000000)
#define SERVER_MONITOR_TRAMPOLINE_REGION_BASE (0x6000000)

#define MONITOR_CALL_DEPLOY (1)
#define MONITOR_CALL_BENCHMARK (5)

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

static char bm_server_stack1[0x10000];
static char bm_server_stack2[0x10000];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }


extern char _bm_proto_container[];
extern char _bm_proto_container_end[];
extern char _bm_trampoline[];
extern char _bm_trampoline_end[];
extern char _bm_payload[];
extern char _bm_payload_end[];


static void print_prompt(void)
{
    sddf_putchar_unbuffered('b');
    sddf_putchar_unbuffered('m');
    sddf_putchar_unbuffered('_');
    sddf_putchar_unbuffered('s');
    sddf_putchar_unbuffered('e');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('v');
    sddf_putchar_unbuffered('e');
    sddf_putchar_unbuffered('r');
    sddf_putchar_unbuffered('>');
    sddf_putchar_unbuffered('$');
    sddf_putchar_unbuffered(' ');
}


void shell_inst_epilogue(void);

void init(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Entered init\n");
    assert(serial_config_check_magic(&serial_config));
    TSLDR_DBG_PRINT(PROGNAME "check serial config\n");

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    TSLDR_DBG_PRINT(PROGNAME "finished init\n");
    
    stack_ptrs_arg_array_t costacks = { (uintptr_t) bm_server_stack1, (uintptr_t) bm_server_stack2 };
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);

    sddf_putchar_unbuffered('\n');
    print_prompt();
}

#define INPUT_BUF_SIZE 128
#define FNAME_BUF_SIZE 64

static char input_buf[INPUT_BUF_SIZE];
static char fname_buf[FNAME_BUF_SIZE];
static size_t input_len = 0;

static void bm_server_call_monitor(int moncall)
{
    microkit_msginfo info;
    seL4_Error error;
    microkit_mr_set(0, moncall);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        sddf_printf("Received error from calling monitor with '%d'\n", moncall);
        microkit_internal_crash(error);
    }
}

void load_elf_payload(void)
{

    tsldr_miscutil_memcpy((void *)SERVER_MONITOR_PROTOCON_REGION_BASE,
                          (void *)_bm_proto_container, _bm_proto_container_end - _bm_proto_container);
    tsldr_miscutil_memcpy((void *)SERVER_MONITOR_TRAMPOLINE_REGION_BASE,
                          (void *)_bm_trampoline, _bm_trampoline_end - _bm_trampoline);
    tsldr_miscutil_memcpy((void *)SERVER_MONITOR_PAYLOAD_REGION_BASE,
                          (void *)_bm_payload, _bm_payload_end - _bm_payload);

    bm_server_call_monitor(MONITOR_CALL_DEPLOY);
}

/* ----- Command handlers ----- */

static int parse_start_cmd(const char *arg)
{
    // Skip leading spaces
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: 'start' requires a filename\n> ");
        return 1;
    }

    // Extract filename (stop at space or NUL)
    const char *end = arg;
    while (*end && *end != ' ') end++;

    size_t len = end - arg;
    if (len >= sizeof(fname_buf)) len = sizeof(fname_buf) - 1;

    memset(fname_buf, 0, FNAME_BUF_SIZE);
    memcpy(fname_buf, arg, len);
    fname_buf[len] = '\0';
#if 1
    if (microkit_cothread_spawn(load_elf_payload, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot spawn cothread to load payload\n");
        return 1;
    }
    microkit_cothread_yield();
#endif
    sddf_printf("> ");
    return 0;
}

static int handle_line(const char *line)
{
    while (*line == ' ') line++;  // skip spaces

    if (*line == '\0') {
        // empty input
        sddf_printf("> ");
        return 0;
    }

    if (strncmp(line, "start", 5) == 0) {
        const char *after = line + 5;
        if (*after == '\0') {
            sddf_printf("Invalid usage: 'start' requires a filename\n");
            return 1;
        } else if (*after == ' ') {
            return parse_start_cmd(after);
        } else {
            sddf_printf("Invalid command format\n");
            return 1;
        }
    } else {
        sddf_printf("Unknown command: %s\n", line);
        return 1;
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
    sddf_putchar_unbuffered('>');
    sddf_putchar_unbuffered(' ');
}


/* ----- Microkit callback ----- */

void notified(microkit_channel ch)
{
    microkit_cothread_recv_ntfn(ch);

    if (ch == serial_config.rx.id) {
        char c;
        while (!serial_dequeue(&serial_rx_queue_handle, &c)) {
            if (c == '\r') {
                // end of line
                sddf_putchar_unbuffered('\r');
                sddf_putchar_unbuffered('\n');

                input_buf[input_len] = '\0';
                int err = handle_line(input_buf);

                // reset buffer and show prompt
                input_len = 0;
                if (err) {
                    print_prompt();
                }
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
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

#define PROGNAME "[@orchestrator] "

uintptr_t shared1 = 0x4000000;
uintptr_t shared2 = 0xb000000;
uintptr_t shared3 = 0x6000000;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;

static char mp_stack1[0x10000];
static char mp_stack2[0x10000];
static co_control_t co_controller_mem;

static void blocking_wait(microkit_channel ch) { microkit_cothread_wait_on_channel(ch); }

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
#if 1
    pico_vfs_readfile2buf((void *)shared1, "protocon.elf", &err);
    if (err != seL4_NoError) {
        goto _exit;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote proto-container's ELF file into memory\n");

    pico_vfs_readfile2buf((void *)shared3, "trampoline.elf", &err);
    if (err != seL4_NoError) {
        goto _exit;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote trampoline's ELF file into memory\n");
#endif
_exit:
    sddf_putchar_unbuffered('\n');
    print_prompt();
}

void init(void)
{
    TSLDR_DBG_PRINT(PROGNAME "Entered init\n");
    assert(serial_config_check_magic(&serial_config));
    TSLDR_DBG_PRINT(PROGNAME "check serial config\n");
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
    microkit_cothread_init(&co_controller_mem, 0x10000, costacks);

    if (microkit_cothread_spawn(test_entrypoint, NULL) == LIBMICROKITCO_NULL_HANDLE) {
        TSLDR_DBG_PRINT(PROGNAME "Cannot initialise orchestrator cothread2\n");
        microkit_internal_crash(-1);
    }
    microkit_cothread_yield();

    TSLDR_DBG_PRINT(PROGNAME "finished init\n");
}

#define INPUT_BUF_SIZE 128
#define FNAME_BUF_SIZE 64

static char input_buf[INPUT_BUF_SIZE];
static char fname_buf[FNAME_BUF_SIZE];
static char _buf[FNAME_BUF_SIZE];
static size_t input_len = 0;

#define MIN_REQ_PC_NUM 1U
#define MAX_REQ_PC_NUM 4U

uint32_t req_pc_num = MIN_REQ_PC_NUM;

void load_elf_payload(void)
{
    while(!fs_init) {
        microkit_cothread_yield();
    }
    TSLDR_DBG_PRINT(PROGNAME "entry of load_elf_payload\n");

    microkit_msginfo info;
    seL4_Error error;

    pico_vfs_readfile2buf((void *)shared2, fname_buf, &error);
    if (error != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to read %s\n", fname_buf);
        shell_inst_epilogue();
        return;
    }
    TSLDR_DBG_PRINT(PROGNAME "Wrote test's ELF file into memory\n");

    microkit_mr_set(0, 1);
    microkit_mr_set(1, req_pc_num);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
}

/* ----- Command handlers ----- */

static int parse_start_cmd(const char *arg)
{
    const char *filename_start;
    const char *filename_end;
    size_t filename_len;
    uint32_t requested_pc_num = MIN_REQ_PC_NUM;

    /* Skip spaces before the filename. */
    while (*arg == ' ') {
        arg++;
    }

    if (*arg == '\0') {
        sddf_printf(
            "Invalid usage: expected 'start <elf>' "
            "or 'start <elf> <pc_num>'\n> "
        );
        return 1;
    }

    /*
     * Parse the filename.
     *
     * Accepted forms:
     *   start elf
     *   start elf x
     */
    filename_start = arg;

    while (*arg != '\0' && *arg != ' ') {
        arg++;
    }

    filename_end = arg;
    filename_len = (size_t)(filename_end - filename_start);

    if (filename_len == 0) {
        sddf_printf("Invalid usage: missing ELF filename\n> ");
        return 1;
    }

    if (filename_len >= sizeof(fname_buf)) {
        sddf_printf(
            "Invalid usage: ELF filename is too long "
            "(maximum %u characters)\n> ",
            (unsigned int)(sizeof(fname_buf) - 1)
        );
        return 1;
    }

    /* Skip spaces after the filename. */
    while (*arg == ' ') {
        arg++;
    }

    /*
     * An optional pc_num follows the filename.
     * When omitted, retain the current req_pc_num value.
     */
    if (*arg != '\0') {
        uint32_t value = 0;

        /*
         * The first character must be a decimal digit.
         * This rejects negative values and signs such as "+1".
         */
        if (*arg < '0' || *arg > '9') {
            sddf_printf(
                "Invalid pc_num: expected an integer from %u to %u\n> ",
                MIN_REQ_PC_NUM,
                MAX_REQ_PC_NUM
            );
            return 1;
        }

        while (*arg >= '0' && *arg <= '9') {
            uint32_t digit = (uint32_t)(*arg - '0');

            /*
             * The accepted maximum is only four, so reject as
             * soon as the parsed value becomes too large.
             */
            value = value * 10U + digit;

            if (value > MAX_REQ_PC_NUM) {
                sddf_printf(
                    "Invalid pc_num: expected an integer from %u to %u\n> ",
                    MIN_REQ_PC_NUM,
                    MAX_REQ_PC_NUM
                );
                return 1;
            }

            arg++;
        }

        /* Spaces after pc_num are permitted. */
        while (*arg == ' ') {
            arg++;
        }

        /* Reject another argument or malformed numeric text. */
        if (*arg != '\0') {
            sddf_printf(
                "Invalid usage: expected 'start <elf>' "
                "or 'start <elf> <pc_num>'\n> "
            );
            return 1;
        }

        if (value < MIN_REQ_PC_NUM || value > MAX_REQ_PC_NUM) {
            sddf_printf(
                "Invalid pc_num: expected an integer from %u to %u\n> ",
                MIN_REQ_PC_NUM,
                MAX_REQ_PC_NUM
            );
            return 1;
        }

        requested_pc_num = value;
    }

    /*
     * Commit both parsed values only after the complete command has
     * passed validation. An invalid command therefore cannot partially
     * modify fname_buf or req_pc_num.
     */
    memset(fname_buf, 0, sizeof(fname_buf));
    memcpy(fname_buf, filename_start, filename_len);
    fname_buf[filename_len] = '\0';

    req_pc_num = requested_pc_num;

    if (
        microkit_cothread_spawn(
            load_elf_payload,
            NULL
        ) == LIBMICROKITCO_NULL_HANDLE
    ) {
        TSLDR_DBG_PRINT(
            PROGNAME "Cannot spawn cothread to load payload\n"
        );
        return 1;
    }

    microkit_cothread_yield();

    sddf_printf("> ");
    return 0;
}

static int parse_lspcs_cmd(void)
{
    microkit_msginfo info;
    seL4_Error error;

    /* syscall id: list proto containers */
    microkit_mr_set(0, 5);

    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    sddf_printf("> \n");
    return 0;
}

static int parse_flip_cmd(void)
{
    microkit_msginfo info;
    seL4_Error error;

    /* syscall id: flip acl rule */
    microkit_mr_set(0, 17);

    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    sddf_printf("> \n");
    return 0;
}

static int parse_resume_cmd(const char *arg)
{
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: 'resume' requires a PD id\n> ");
        return 1;
    }

    /*
     * Expected format:
     *   -i num
     */

    if (arg[0] != '-' || arg[1] != 'i') {
        sddf_printf("Invalid usage: expected '-i <pd_id>'\n> ");
        return 1;
    }

    arg += 2;

    if (*arg != ' ') {
        sddf_printf("Invalid usage: expected space after '-i'\n> ");
        return 1;
    }

    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: missing PD id after '-i'\n> ");
        return 1;
    }

    seL4_Word target_pd_id = 0;

    while (*arg >= '0' && *arg <= '9') {
        seL4_Word digit = *arg - '0';
        target_pd_id = target_pd_id * 10 + digit;
        arg++;
    }

    while (*arg == ' ') arg++;

    if (*arg != '\0') {
        sddf_printf("Invalid usage: unexpected trailing argument\n> ");
        return 1;
    }

    /* init target_pd_id with _buf */

    microkit_msginfo info;
    seL4_Error error;

    /* syscall id: resume proto containers */
    microkit_mr_set(0, 4);
    /* PD id as the second arg */
    microkit_mr_set(1, target_pd_id);

    info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    sddf_printf("> \n");
    return 0;
}


static int parse_hang_cmd(const char *arg)
{
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: 'hang' requires a PD id\n> ");
        return 1;
    }

    /*
     * Expected format:
     *   -i num
     */

    if (arg[0] != '-' || arg[1] != 'i') {
        sddf_printf("Invalid usage: expected '-i <pd_id>'\n> ");
        return 1;
    }

    arg += 2;

    if (*arg != ' ') {
        sddf_printf("Invalid usage: expected space after '-i'\n> ");
        return 1;
    }

    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: missing PD id after '-i'\n> ");
        return 1;
    }

    seL4_Word target_pd_id = 0;

    while (*arg >= '0' && *arg <= '9') {
        seL4_Word digit = *arg - '0';
        target_pd_id = target_pd_id * 10 + digit;
        arg++;
    }

    while (*arg == ' ') arg++;

    if (*arg != '\0') {
        sddf_printf("Invalid usage: unexpected trailing argument\n> ");
        return 1;
    }

    /* init target_pd_id with _buf */

    microkit_msginfo info;
    seL4_Error error;

    /* syscall id: stop proto containers */
    microkit_mr_set(0, 3);
    /* PD id as the second arg */
    microkit_mr_set(1, target_pd_id);

    info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    sddf_printf("> \n");
    return 0;
}


static int parse_stop_cmd(const char *arg)
{
    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: 'stop' requires a PD id\n> ");
        return 1;
    }

    /*
     * Expected format:
     *   -i num
     */

    if (arg[0] != '-' || arg[1] != 'i') {
        sddf_printf("Invalid usage: expected '-i <pd_id>'\n> ");
        return 1;
    }

    arg += 2;

    if (*arg != ' ') {
        sddf_printf("Invalid usage: expected space after '-i'\n> ");
        return 1;
    }

    while (*arg == ' ') arg++;

    if (*arg == '\0') {
        sddf_printf("Invalid usage: missing PD id after '-i'\n> ");
        return 1;
    }

    seL4_Word target_pd_id = 0;

    while (*arg >= '0' && *arg <= '9') {
        seL4_Word digit = *arg - '0';
        target_pd_id = target_pd_id * 10 + digit;
        arg++;
    }

    while (*arg == ' ') arg++;

    if (*arg != '\0') {
        sddf_printf("Invalid usage: unexpected trailing argument\n> ");
        return 1;
    }

    /* init target_pd_id with _buf */

    microkit_msginfo info;
    seL4_Error error;

    /* syscall id: stop proto containers */
    microkit_mr_set(0, 6);
    /* PD id as the second arg */
    microkit_mr_set(1, target_pd_id);

    info = microkit_ppcall(1, microkit_msginfo_new(0, 2));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    sddf_printf("> \n");
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
    } else if (strncmp(line, "lspcs", 5) == 0) {
        const char *after = line + 5;
        while (*after == ' ') after++;
        if (*after != '\0') {
            sddf_printf("Invalid command format\n");
            return 1;
        }
        return parse_lspcs_cmd();
    } else if (strncmp(line, "flip", 4) == 0) {
        const char *after = line + 4;
        while (*after == ' ') after++;
        if (*after != '\0') {
            sddf_printf("Invalid command format\n");
            return 1;
        }
        return parse_flip_cmd();
    } else if (strncmp(line, "stop", 4) == 0) {
        const char *after = line + 4;
        if (*after == '\0') {
            sddf_printf("Invalid usage: 'stop' requires a PD id\n");
            return 1;
        } else if (*after == ' ') {
            return parse_stop_cmd(after);
        } else {
            sddf_printf("Invalid command format\n");
            return 1;
        }
    } else if (strncmp(line, "hang", 4) == 0) {
        const char *after = line + 4;
        if (*after == '\0') {
            sddf_printf("Invalid usage: 'hang' requires a PD id\n");
            return 1;
        } else if (*after == ' ') {
            return parse_hang_cmd(after);
        } else {
            sddf_printf("Invalid command format\n");
            return 1;
        }
    } else if (strncmp(line, "resume", 6) == 0) {
        const char *after = line + 6;
        if (*after == '\0') {
            sddf_printf("Invalid usage: 'resume' requires a PD id\n");
            return 1;
        } else if (*after == ' ') {
            return parse_resume_cmd(after);
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
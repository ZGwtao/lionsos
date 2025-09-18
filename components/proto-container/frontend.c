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
#include <fs_helpers.h>

#define PROGNAME "[@frontend] "

uintptr_t shared1 = 0x4000000;
uintptr_t shared2 = 0xb000000;
uintptr_t shared3 = 0x6000000;

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
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



uint64_t opendir(void)
{
    const char *path = ".";
    ptrdiff_t path_buffer;
    int err = fs_buffer_allocate(&path_buffer);
    if (err) {
        microkit_dbg_printf(PROGNAME "Failed to allocate buffer for a path\n");
        return -1;
    }
    uint64_t path_len = strlen(path);
    memcpy(fs_buffer_ptr(path_buffer), path, path_len);

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_DIR_OPEN,
        .params.dir_open = {
            .path.offset = path_buffer,
            .path.size = path_len,
        }
    });
    fs_buffer_free(path_buffer);
    if (err) {
        microkit_dbg_printf(PROGNAME "Failed to open directory: %s\n", path);
        return -1;
    }

    if (completion.status != FS_STATUS_SUCCESS) {
        microkit_dbg_printf(PROGNAME "Failed to open directory: %s\n", path);
        return -1;
    }
    return completion.data.dir_open.fd;
}

void listdir(uint64_t fd)
{
    for (;;) {
        ptrdiff_t name_buffer;
        int err = fs_buffer_allocate(&name_buffer);
        assert(!err);

        fs_cmpl_t completion;
        fs_command_blocking(&completion, (fs_cmd_t){
            .type = FS_CMD_DIR_READ,
            .params.dir_read = {
                .fd = fd,
                .buf.offset = name_buffer,
                .buf.size = FS_BUFFER_SIZE,
            }
        });

        if (completion.status != FS_STATUS_SUCCESS) {
            microkit_dbg_printf(PROGNAME "Failed to read directory with fd: %d\n", fd);
            fs_buffer_free(name_buffer);
            break;
        }

        const char *fn = fs_buffer_ptr(name_buffer);

        if (fn[0] == '.' && (fn[1] == 0 || fn[1] == '.')) {
            fs_buffer_free(name_buffer);
            continue;
        }

        //uint64_t path_len = completion.data.dir_read.path_len;
        microkit_dbg_printf("%s\n", name_buffer);

        fs_buffer_free(name_buffer);

        return;
    }
}

void closedir(uint64_t fd)
{
    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_DIR_CLOSE,
        .params.dir_close.fd = fd,
    });
}

uint64_t openfile(void)
{
    const char *fname = "protocon.elf";

    ptrdiff_t buffer;
    int err = fs_buffer_allocate(&buffer);
    assert(!err);

    uint64_t path_len = strlen(fname) + 1;
    memcpy(fs_buffer_ptr(buffer), fname, path_len);

    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_OPEN,
        .params.file_open = {
            .path.offset = buffer,
            .path.size = path_len,
            .flags = 0 & (0 | FS_OPEN_FLAGS_CREATE),
        }
    });
    fs_buffer_free(buffer);
    if (completion.status != FS_STATUS_SUCCESS) {
        microkit_dbg_printf(PROGNAME "(file open) failed to open %s\n", fname);
        return -1;
    }
    uint64_t fd = completion.data.file_open.fd;

    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_SIZE,
        .params.file_size.fd = fd,
    });
    if (completion.status != FS_STATUS_SUCCESS) {
        fs_command_blocking(&completion, (fs_cmd_t){
            .type = FS_CMD_FILE_CLOSE,
            .params.file_close.fd = fd,
        });
        fs_buffer_free(buffer);
        microkit_dbg_printf(PROGNAME "(file open) failed to open %s\n", fname);
        return -1;
    }
    microkit_dbg_printf(PROGNAME "(file open) open fd %d\n", fd);
    return fd;
}

uint64_t readfile(void *dest, uint64_t size, uint64_t fd, uint64_t pos)
{
    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    assert(!err);

    microkit_dbg_printf(PROGNAME "(fs read) begin to read data from %d\n", fd);

    fs_cmpl_t completion;
    err = fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_READ,
        .params.file_read = {
            .fd = fd,
            .offset = pos,
            .buf.offset = read_buffer,
            .buf.size = size,
        }
    });
    if (err || completion.status != FS_STATUS_SUCCESS) {
        fs_buffer_free(read_buffer);
        microkit_dbg_printf(PROGNAME "(fs read) failed to read file with fd: %d\n", fd);
        return -1;
    }

    memcpy(dest, fs_buffer_ptr(read_buffer), completion.data.file_read.len_read);
    fs_buffer_free(read_buffer);

    microkit_dbg_printf(PROGNAME "(fs read) have read %d data successfully\n", completion.data.file_read.len_read);
    return pos + completion.data.file_read.len_read;
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

    uint64_t dir_fd = opendir();
    microkit_dbg_printf(PROGNAME "(dir open): fd is %d opened\n", dir_fd);

    listdir(dir_fd);
    microkit_dbg_printf(PROGNAME "(dir list): fd is %d listed\n", dir_fd);

    closedir(dir_fd);
    microkit_dbg_printf(PROGNAME "(dir close): fd is %d closed\n", dir_fd);

    uint64_t file_fd = openfile();
    microkit_dbg_printf(PROGNAME "(file open): fd is %d opened\n", file_fd);

    uint64_t pos = 0;
    uint64_t pre;
    uintptr_t buf = shared1;

    while (true) {
        pre = pos;
        pos = readfile((void *)buf, FS_BUFFER_SIZE, file_fd, pos);
        if (pos == (uint64_t)-1) break;
        if (pos == pre) {
            microkit_dbg_printf(PROGNAME "(file read): all read from %d\n", file_fd);
            break;
        }
        buf += pos - pre;
    }
    microkit_dbg_printf(PROGNAME "(file read): read %d data from %d \n", pos, file_fd);

    //custom_memcpy((void *)shared1, _proto_container, _proto_container_end - _proto_container);
    microkit_dbg_printf(PROGNAME "Wrote proto-container's ELF file into memory\n");
#if 0
    //custom_memcpy((void *)shared2, _client, _client_end - _client);
    microkit_dbg_printf(PROGNAME "Wrote client's ELF file into memory\n");

    //custom_memcpy((void *)shared3, _trampoline, _trampoline_end - _trampoline);
    microkit_dbg_printf(PROGNAME "Wrote trampoline's ELF file into memory\n");
#endif
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
        microkit_dbg_printf(PROGNAME "Ready to handle tasks\n");
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
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));
    assert(fs_config_check_magic(&fs_config));

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

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
    microkit_dbg_printf(PROGNAME "Received notification on channel: %d\n", ch);

    fs_process_completions();
    microkit_cothread_recv_ntfn(ch);
}

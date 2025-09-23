
#include <microkit.h>
#include <pico_vfs.h>
#include <string.h>
#include <assert.h>
#include <microkit.h>
#include <elf_utils.h>

#define PROGNAME "  => [@picovfs] "


uint64_t opendir(char path[])
{
    microkit_dbg_printf(PROGNAME "Entry of opendir func\n");

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

static void listdir(uint64_t fd)
{
    for (;;) {
        ptrdiff_t name_buffer;
        int err = fs_buffer_allocate(&name_buffer);
        if (err != seL4_NoError) {
            microkit_dbg_printf(PROGNAME "failed to allocate buffer to list directory");
            // halt...
            while (1);
        }

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
        fs_buffer_free(name_buffer);
        return;
    }
}

static void closedir(uint64_t fd)
{
    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_DIR_CLOSE,
        .params.dir_close.fd = fd,
    });
}

static uint64_t openfile(char fname[])
{
    ptrdiff_t buffer;
    int err = fs_buffer_allocate(&buffer);
    if (err != seL4_NoError) {
        microkit_dbg_printf(PROGNAME "failed to allocate buffer to open file");
        // halt...
        while (1);
    }

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
        return -1;
    }
    return fd;
}

static uint64_t readfile(void *dest, uint64_t size, uint64_t fd, uint64_t pos)
{
    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    if (err != seL4_NoError) {
        microkit_dbg_printf(PROGNAME "failed to allocate buffer to read file");
        // halt...
        while (1);
    }

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
        return -1;
    }

    memcpy(dest, fs_buffer_ptr(read_buffer), completion.data.file_read.len_read);
    fs_buffer_free(read_buffer);

    return pos + completion.data.file_read.len_read;
}

static void closefile(uint64_t fd)
{
    fs_cmpl_t completion;
    fs_command_blocking(&completion, (fs_cmd_t){
        .type = FS_CMD_FILE_CLOSE,
        .params.file_close.fd = fd,
    });
}


uint64_t pico_vfs_readfile2buf(void *buf, char *path, int *err)
{
    *err = seL4_NoError;

    uint64_t file_fd = openfile(path);
    if (file_fd != (uint64_t)-1) {
        microkit_dbg_printf(PROGNAME "(file open): fd is %d opened\n", file_fd);
    } else {
        *err = -1;
        microkit_dbg_printf(PROGNAME "(file open): failed to open protocon.elf\n");
        return 0;
    }

    uint64_t pos = 0;
    uint64_t pre;
    while (true) {
        pre = pos;
        pos = readfile(buf, FS_BUFFER_SIZE, file_fd, pos);
        if (pos == (uint64_t)-1) {
            *err = -1;
            microkit_dbg_printf(PROGNAME "(file read): failed to read from fd: %d\n", file_fd);
            return 0;
        }
        if (pos == pre) {
            microkit_dbg_printf(PROGNAME "(file read): all read from %d\n", file_fd);
            break;
        }
        buf += pos - pre;
    }
    microkit_dbg_printf(PROGNAME "(file read): read %d data from %d \n", pos, file_fd);

    closefile(file_fd);

    return pos;
}

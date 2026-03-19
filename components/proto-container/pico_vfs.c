
#include <pico_vfs.h>
#include <string.h>
#include <assert.h>
#include <microkit.h>
#include <libtrustedlo.h>

#define PROGNAME "  => [@picovfs] "


static uint64_t openfile(char fname[])
{
    ptrdiff_t buffer;
    int err = fs_buffer_allocate(&buffer);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "failed to allocate buffer to open file");
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
        TSLDR_DBG_PRINT(PROGNAME "failed to size file");
        return -1;
    }
    return fd;
}

static uint64_t readfile(void *dest, uint64_t size, uint64_t fd, uint64_t pos)
{
    ptrdiff_t read_buffer;
    int err = fs_buffer_allocate(&read_buffer);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "failed to allocate buffer to read file");
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
        TSLDR_DBG_PRINT(PROGNAME "failed to read file");
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
        TSLDR_DBG_PRINT(PROGNAME "(file open): fd is %d opened\n", file_fd);
    } else {
        *err = -1;
        TSLDR_DBG_PRINT(PROGNAME "(file open): failed to open %s\n", path);
        return 0;
    }

    uint64_t pos = 0;
    uint64_t pre;
    while (true) {
        pre = pos;
        pos = readfile(buf, FS_BUFFER_SIZE, file_fd, pos);
        if (pos == (uint64_t)-1) {
            *err = -1;
            TSLDR_DBG_PRINT(PROGNAME "(file read): failed to read from fd: %d\n", file_fd);
            return 0;
        }
        if (pos == pre) {
            TSLDR_DBG_PRINT(PROGNAME "(file read): all read from %d\n", file_fd);
            break;
        }
        buf += pos - pre;
    }
    TSLDR_DBG_PRINT(PROGNAME "(file read): read %d data from %d \n", pos, file_fd);

    closefile(file_fd);

    return pos;
}

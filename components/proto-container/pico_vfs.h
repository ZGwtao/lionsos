
#include <lions/fs/helpers.h>

typedef struct request_metadata {
    fs_cmd_t command;
    fs_cmpl_t completion;
    bool used;
    bool complete;
} request_metadata_t;

typedef struct buffer_metadata {
    bool used;
} buffer_metadata_t;

/* return the size of file mapped in the memory */
uint64_t pico_vfs_readfile2buf(void *buf, char *path, int *err);

/* external open directory */
uint64_t opendir(char path[]);

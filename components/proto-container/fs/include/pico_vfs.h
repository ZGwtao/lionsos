
#include <fs_helpers.h>


/* return the size of file mapped in the memory */
uint64_t pico_vfs_readfile2buf(void *buf, char *path, int *err);

/* external open directory */
uint64_t opendir(char path[]);

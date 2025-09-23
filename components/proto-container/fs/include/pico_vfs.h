
#include <fs_helpers.h>


/* return the size of file mapped in the memory */
uint64_t pico_vfs_mmap(void *buf, char *path, uint64_t *err);

/* external open directory */
uint64_t opendir(char path[]);

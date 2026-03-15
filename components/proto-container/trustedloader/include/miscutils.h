
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#include <elf.h>


void* tsldr_miscutil_memcpy(void* dest, const void* src, uint64_t n);
void tsldr_miscutil_memset(void *dest, int value, uint64_t size);
int tsldr_miscutil_memcmp(const unsigned char* s1, const unsigned char* s2, int n);


void tsldr_miscutil_dbg_print(const char *format, ...);

#if defined(CONFIG_DEBUG_BUILD)
#define TSLDR_DBG_PRINT(...) \
    do { tsldr_miscutil_dbg_print(__VA_ARGS__); } while (0)
#else
#define TSLDR_DBG_PRINT(...) \
    do { } while (0)
#endif


void tsldr_miscutil_load_elf(void *dest_vaddr, const Elf64_Ehdr *ehdr);


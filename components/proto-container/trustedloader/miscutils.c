
#include <miscutils.h>


void* tsldr_miscutil_memcpy(void* dest, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void tsldr_miscutil_memset(void *dest, int value, uint64_t size)
{
    unsigned char *d = (unsigned char *)dest;
    for (uint64_t i = 0; i < size; i++) {
        d[i] = (unsigned char)value;
    }
}

int tsldr_miscutil_memcmp(const unsigned char* s1, const unsigned char* s2, int n)
{
    for (int i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return (s1[i] - s2[i]);
        }
    }
    return 0;
}



void tsldr_miscutil_load_elf(void *dest_vaddr, const Elf64_Ehdr *ehdr)
{
    Elf64_Phdr *phdr = (Elf64_Phdr *)((char*)ehdr + ehdr->e_phoff);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }

        void *src = (char*)ehdr + phdr[i].p_offset;
        void *dest = (void *)(dest_vaddr + phdr[i].p_vaddr - ehdr->e_entry);

        tsldr_miscutil_memcpy(dest, src, phdr[i].p_filesz);

        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            seL4_Word bss_size = phdr[i].p_memsz - phdr[i].p_filesz;
            tsldr_miscutil_memset((char *)dest + phdr[i].p_filesz, 0, bss_size);
        }
    }
}


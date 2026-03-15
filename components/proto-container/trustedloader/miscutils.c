
#include <miscutils.h>
#include <stdarg.h>

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


void putc(uint8_t ch)
{
#if defined(CONFIG_PRINTING)
    seL4_DebugPutChar(ch);
#endif
}

void puts(const char *s)
{
    while (*s) {
        putc(*s);
        s++;
    }
}

static char hexchar(unsigned int v)
{
    return v < 10 ? '0' + v : ('a' - 10) + v;
}

void puthex32(uint32_t val)
{
    char buffer[8 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[8 + 3 - 1] = 0;
    for (unsigned i = 8 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    puts(buffer);
}

void puthex64(uint64_t val)
{
    char buffer[16 + 3];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[16 + 3 - 1] = 0;
    for (unsigned i = 16 + 1; i > 1; i--) {
        buffer[i] = hexchar(val & 0xf);
        val >>= 4;
    }
    puts(buffer);
}

void putdecimal(uint8_t val)
{
    if (0 <= val && val <= 9) {
        putc('0' + val);
    } else {
        /* fallback, shouldn't really happen */
        puthex32(val);
    }
}

void tsldr_miscutil_dbg_print(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    const char *ptr = format;

    while (*ptr != '\0') {
        if (*ptr == '%') {
            ptr++; // Move past '%'

            switch (*ptr) {
                case 's': {
                    // String
                    const char *str = va_arg(args, const char *);
                    if (str != 0) {
                        microkit_dbg_puts(str);
                    } else {
                        microkit_dbg_puts("(null)");
                    }
                    break;
                }
                case 'd': {
                    // Decimal
                    uint64_t val = va_arg(args, uint64_t);
                    putdecimal(val);
                    break;
                }
                case 'x': {
                    // Hexadecimal
                    uint64_t val = va_arg(args, uint64_t);
                    puthex64(val);
                    break;
                }
                case 'c': {
                    // Character
                    int c = va_arg(args, int); // char is promoted to int
                    microkit_dbg_putc((char)c);
                    break;
                }
                case '%': {
                    // Literal '%'
                    microkit_dbg_putc('%');
                    break;
                }
                default: {
                    // Unsupported format specifier, print it literally
                    microkit_dbg_putc('%');
                    microkit_dbg_putc(*ptr);
                    break;
                }
            }
            ptr++; // Move past format specifier
        } else {
            // Regular character
            microkit_dbg_putc(*ptr);
            ptr++;
        }
    }

    va_end(args);
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


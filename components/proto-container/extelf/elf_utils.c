#include "include/elf.h"
#include <microkit.h>
#include <stdarg.h>

// Utility function to output a hexadecimal number
void puthex(uint64_t num) {
    char buffer[17]; // Maximum 16 hex digits for 64-bit + null terminator
    int i = 16;
    buffer[i] = '\0';
    if (num == 0) {
        buffer[--i] = '0';
    } else {
        while (num > 0 && i > 0) {
            uint8_t digit = num % 16;
            if (digit < 10) {
                buffer[--i] = '0' + digit;
            } else {
                buffer[--i] = 'a' + (digit - 10);
            }
            num /= 16;
        }
    }
    microkit_dbg_puts(&buffer[i]);
}

// Utility function to output a decimal number
void putdec(uint64_t num) {
    char buffer[21]; // Maximum 20 digits for uint64_t + null terminator
    int i = 20;
    buffer[i] = '\0';
    if (num == 0) {
        buffer[--i] = '0';
    } else {
        while (num > 0 && i > 0) {
            uint8_t digit = num % 10;
            buffer[--i] = '0' + digit;
            num /= 10;
        }
    }
    microkit_dbg_puts(&buffer[i]);
}

// Helper function to translate ELF type to string
const char* get_elf_type(uint16_t type) {
    switch (type) {
        case ET_NONE: return "NONE (No file type)";
        case ET_REL:  return "REL (Relocatable file)";
        case ET_EXEC: return "EXEC (Executable file)";
        case ET_DYN:  return "DYN (Shared object file)";
        case ET_CORE: return "CORE (Core file)";
        default:      return "UNKNOWN";
    }
}

// Helper function to translate ELF data encoding to string
const char* get_elf_data_encoding(uint8_t data) {
    switch (data) {
        case ELFDATA2LSB: return "Little endian";
        case ELFDATA2MSB: return "Big endian";
        default:          return "Unknown";
    }
}

// Helper function to translate ELF OS/ABI to string
const char* get_elf_osabi(uint8_t osabi) {
    switch (osabi) {
        case ELFOSABI_LINUX:      return "UNIX - Linux";
        default:                   return "Unknown";
    }
}

// Helper function to translate ELF version to string
const char* get_elf_version(uint32_t version) {
    switch (version) {
        case EV_CURRENT: return "Current";
        default:         return "Unknown";
    }
}

void putvar(uint64_t var, char* name) {
    microkit_dbg_puts(name);
    microkit_dbg_puts(": ");
    puthex(var);
    microkit_dbg_putc('\n');
}



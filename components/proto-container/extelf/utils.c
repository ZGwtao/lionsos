#include "include/utils.h"
#include <microkit.h>
#include <stdarg.h>
#include <stdint.h>

void microkit_dbg_printf(const char *format, ...) {
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
                    putdec(val);
                    break;
                }
                case 'x': {
                    // Hexadecimal
                    uint64_t val = va_arg(args, uint64_t);
                    puthex(val);
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

/**
 * @brief Outputs an unsigned 64-bit integer in decimal format.
 *
 * @param val The unsigned 64-bit integer to print.
 */
void putdec(uint64_t val) {
    // Buffer to hold the decimal digits (max 20 digits for uint64_t)
    char buffer[21];
    int index = 20; // Start from the end of the buffer
    buffer[index] = '\0'; // Null-terminate the string

    if (val == 0) {
        buffer[--index] = '0';
    } else {
        while (val > 0 && index > 0) {
            buffer[--index] = '0' + (val % 10);
            val /= 10;
        }
    }

    // Output the resulting string
    const char *str = &buffer[index];
    while (*str) {
        microkit_dbg_putc(*str++);
    }
}

/**
 * @brief Outputs an unsigned 64-bit integer in hexadecimal format.
 *
 * @param val The unsigned 64-bit integer to print.
 */
void puthex(uint64_t val) {
    // Prefix for hexadecimal representation
    microkit_dbg_puts("0x");

    // Buffer to hold the hexadecimal digits (max 16 digits for uint64_t)
    char buffer[17];
    int index = 16; // Start from the end of the buffer
    buffer[index] = '\0'; // Null-terminate the string

    if (val == 0) {
        buffer[--index] = '0';
    } else {
        while (val > 0 && index > 0) {
            uint8_t digit = val & 0xF; // Get the last 4 bits
            if (digit < 10) {
                buffer[--index] = '0' + digit;
            } else {
                buffer[--index] = 'a' + (digit - 10);
            }
            val >>= 4; // Shift right by 4 bits to process the next digit
        }
    }

    // Output the resulting string
    const char *str = &buffer[index];
    while (*str) {
        microkit_dbg_putc(*str++);
    }
}

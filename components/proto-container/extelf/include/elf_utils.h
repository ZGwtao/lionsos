#ifndef ELF_CHECKER_H
#define ELF_CHECKER_H

#include <stdint.h>
#include <elf.h>

/**
 * @brief Checks whether the ELF file is valid and prints its information.
 *
 * @param _receiver Pointer to the start of the ELF file in memory.
 * @param _receiver_end Pointer to the end of the ELF file in memory.
 */
void print_elf(const char* _receiver, const char* _receiver_end);

/**
 * @brief Outputs a hexadecimal representation of a number.
 *
 * @param num The number to convert and output in hexadecimal.
 */
void puthex(uint64_t num);

/**
 * @brief Outputs a decimal representation of a number.
 *
 * @param num The number to convert and output in decimal.
 */
void putdec(uint64_t num);

void putvar(uint64_t var, char* name);

#endif /* ELF_CHECKER_H */

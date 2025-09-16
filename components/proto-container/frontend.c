/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <elf_utils.h>
#include <stdint.h>
#include <microkit.h>

#define PROGNAME "[@frontend] "

uintptr_t shared1 = 0x4000000;
uintptr_t shared2 = 0xb000000;
uintptr_t shared3 = 0x6000000;

void init(void)
{
    microkit_dbg_printf(PROGNAME "Entered init\n");

    //custom_memcpy((void *)shared1, _proto_container, _proto_container_end - _proto_container);
    microkit_dbg_printf(PROGNAME "Wrote proto-container's ELF file into memory\n");

    //custom_memcpy((void *)shared2, _client, _client_end - _client);
    microkit_dbg_printf(PROGNAME "Wrote client's ELF file into memory\n");

    //custom_memcpy((void *)shared3, _trampoline, _trampoline_end - _trampoline);
    microkit_dbg_printf(PROGNAME "Wrote trampoline's ELF file into memory\n");

    microkit_dbg_printf(PROGNAME "Making ppc to container monitor backend\n");

    microkit_msginfo info;
    seL4_Error error;

    microkit_mr_set(0, 1);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }

    //custom_memcpy((void *)shared2, _test, _test_end - _test);
    microkit_dbg_printf(PROGNAME "Wrote test's ELF file into memory\n");

    microkit_mr_set(0, 2);
    info = microkit_ppcall(1, microkit_msginfo_new(0, 1));
    error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }

    microkit_dbg_printf(PROGNAME "Finished init\n");
}

void notified(microkit_channel ch)
{
    microkit_dbg_printf(PROGNAME "Received notification on channel: %d\n", ch);
}

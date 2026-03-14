/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "elf.h"
#include "elf_utils.h"
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <ed25519.h>
#include <libtrustedlo.h>
#include <string.h>
#include <sddf/timer/config.h>

#define PROGNAME "[@protocon] "


/* 4KB in size, read-only */
uintptr_t tsldr_metadata    = 0x0A00000;
uintptr_t acgroup_metadata  = 0x0A01000;
uintptr_t trampoline_elf    = 0x1000000;
uintptr_t container_elf     = 0x2000000;
uintptr_t container_exec    = 0x2800000;

#define STACKS_SIZE 0x1000

uintptr_t trampoline_stack_top  = (0x00FFFE00000);
//uintptr_t tsldr_stack_bottom    = (0x00FFFFFF000);
uintptr_t container_stack_top   = (0x00FFFC00000);

/*
 * vaddr: 0xE00000
 * Should not be static because it needs to be patched externally
 */
trusted_loader_t *loader_context;


void init(void)
{
    __sel4_ipc_buffer = (seL4_IPCBuffer *)0x100000;
    loader_context = (trusted_loader_t *)0xE00000;

    tsldr_main_self_loading(tsldr_metadata, acgroup_metadata, loader_context, container_elf, container_exec, trampoline_elf, trampoline_stack_top);
}

void notified(microkit_channel ch)
{
    microkit_dbg_printf(PROGNAME "Received notification on channel: %d\n", ch);
}

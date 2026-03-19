/*
 * Copyright 2025, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <stddef.h>
#include <stdint.h>
#include <microkit.h>
#include <libtrustedlo.h>
#include <string.h>

#define TSLDR_METADATA (0x0A00000)
#define OSSVC_METADATA (0x0A01000)
#define TRAMPOLINE_ELF (0x1000000)
#define CONTAINER_ELF (0x2000000)
#define CONTAINER_EXEC (0x2800000)

// trampoline is going to use a different stack with both the trusted loader and the client program,
// so we need to prepare a stack for it (so that trampoline can clean up the old stack)
#define TRAMPOLINE_STACK_TOP (0x00FFFE00000)

// the base address of the trusted loader context region for each dynamic PD (protocon)
// this describes the information of all requested low-level access rights of a dynamic PD,
// which is the SUBSET of trusted loading metadata
#define TSLDR_CONTEXT_BASE (0xE00000)

void init(void)
{
    // 0x100000 is reserved by the microkit tool for dynamic PD's ipc buffer
    __sel4_ipc_buffer = (seL4_IPCBuffer *)0x100000;
    
    tsldr_context_t *trusted_loader_context = (tsldr_context_t *)TSLDR_CONTEXT_BASE;

    /* --- trusted loading main function --- */
    
    tsldr_main_self_loading(
        (void *)TSLDR_METADATA, // the place where the trusted loader metadata is placed by the monitor
        (void *)OSSVC_METADATA, // the place where the information of all OS services is placed by the monitor
        trusted_loader_context,
        (uintptr_t)(void *)CONTAINER_ELF,  // the place where client elf is placed by the monitor
        (uintptr_t)(void *)CONTAINER_EXEC, // the place where client elf is loaded and executed
        (uintptr_t)(void *)TRAMPOLINE_ELF, // the place where the trampoline elf is placed by the monitor
        TRAMPOLINE_STACK_TOP
    );
}

void notified(microkit_channel ch) {}
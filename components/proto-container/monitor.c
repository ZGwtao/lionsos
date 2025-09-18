
#include <microkit.h>
#include <stdarg.h>
#include <sddf/util/printf.h>
#include <elf_utils.h>
#include <libtrustedlo.h>

#define PROGNAME "[@monitor] "

uintptr_t trusted_loader_exec = 0x4000000;
uintptr_t trampoline_elf = 0xD800000;
uintptr_t container_elf = 0xA00000000;

/* 4KB in size */
tsldr_md_t tsldr_metadata_patched;
/*
 * A shared memory region with container, containing content from tsldr_metadata_patched
 * Will be init each time the container restarts by copying the data from above
 */
uintptr_t tsldr_metadata = 0x1000000;

seL4_Word system_hash;
unsigned char public_key[PUBLIC_KEY_BYTES];


void init(void)
{
    microkit_dbg_puts("Hello from monitor\n");
    sddf_printf("Test serial driver\n");
#if 0
/* to use printf, we need stdout as an FD (1) */
// no plan for a VFS...
// can use seL4_libs instead
// -- sel4muslsys
// ...
    printf(">>>\n");
#endif

    seL4_UserContext ctxt = {0};
    ctxt.pc = 0x2000000;
    ctxt.sp = 0x10000000000;
    seL4_Error error = seL4_TCB_WriteRegisters(
        BASE_TCB_CAP + PD_TEMPLATE_CHILD_TCB,
        seL4_True,
        0, /* No flags */
        1, /* writing 1 register */
        &ctxt
    );

    if (error != seL4_NoError) {
        microkit_dbg_puts("microkit_pd_restart: error writing TCB registers\n");
        microkit_internal_crash(error);
    }
    microkit_pd_restart(PD_TEMPLATE_CHILD_TCB, 0x2000000);
}

void notified(microkit_channel ch)
{
    ;
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    microkit_dbg_printf(PROGNAME "Received fault message for child PD: %d\n", child);

    seL4_Word label = microkit_msginfo_get_label(msginfo);
    microkit_dbg_printf(PROGNAME "Fault label: %d\n", label);

    if (label == seL4_Fault_VMFault) {
        seL4_Word ip = microkit_mr_get(seL4_VMFault_IP);
        seL4_Word address = microkit_mr_get(seL4_VMFault_Addr);
        microkit_dbg_printf(PROGNAME "seL4_Fault_VMFault\n");
        microkit_dbg_printf(PROGNAME "Fault address: 0x%x\n", (unsigned long long)address);
        microkit_dbg_printf(PROGNAME "Fault instruction pointer: 0x%x\n", (unsigned long long)ip);
    }

    microkit_pd_stop(child);

    // Stop the thread explicitly; no need to reply to the fault
    return seL4_False;
}

seL4_MessageInfo_t monitor_call_debute(void)
{
    seL4_Error error = tsldr_grant_cspace_access();
    if (error != seL4_NoError) {
        return microkit_msginfo_new(error, 0);
    }

    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)0x6000000;

    if (custom_memcmp(ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        microkit_dbg_printf(PROGNAME "Data in shared memory region must be an ELF file\n");
        return microkit_msginfo_new(seL4_InvalidArgument, 0);
    }

    microkit_dbg_printf(PROGNAME "Verified ELF header\n");

    /* init metadata for proto-container's tsldr */
    tsldr_init_metadata(&tsldr_metadata_patched);

    load_elf((void*)trusted_loader_exec, ehdr);
    microkit_dbg_printf(PROGNAME "Copied trusted loader to child PD's memory region\n");

#if 0
    custom_memcpy((void*)container_elf, (char *)shared2, 0x800000);
    microkit_dbg_printf(PROGNAME "Copied client program to child PD's memory region\n");
#endif

    custom_memcpy((void*)trampoline_elf, (char *)0x6800000, 0x800000);
    microkit_dbg_printf(PROGNAME "Copied trampoline program to child PD's memory region\n");

    // Restart the child PD at the entry point
    microkit_pd_restart(PD_TEMPLATE_CHILD_TCB, ehdr->e_entry);
    microkit_dbg_printf(PROGNAME "Started child PD at entrypoint address: 0x%x\n", (unsigned long long)ehdr->e_entry);

    return microkit_msginfo_new(seL4_NoError, 0);
}



seL4_MessageInfo_t protected(microkit_channel ch, microkit_msginfo msginfo)
{
    microkit_dbg_printf(PROGNAME "Received protected message on channel: %d\n", ch);

    /* get the first word of the message */
    seL4_Word monitorcall_number = microkit_mr_get(0);

    seL4_MessageInfo_t ret;

    /* call for the container monitor */
    switch (monitorcall_number) {
    case 1:
        microkit_dbg_printf(PROGNAME "Loading trusted loader and the first client\n");
        ret = monitor_call_debute();
        break;
    case 2:
        microkit_dbg_printf(PROGNAME "Restart trusted loader and a new client\n");
        //ret = monitor_call_restart();
        break;
    default:
        /* do nothing for now */
        microkit_dbg_printf(PROGNAME "Undefined container monitor call: %lu\n", monitorcall_number);
        break;
    }

    return ret;
}
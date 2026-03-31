#include <microkit.h>
#include <stdint.h>
#include <libtrustedlo.h>

typedef void (*entry_fn_t)(void);

// FIXME: we can have a simpler trampoline.c !!
void init(void)
{
    uintptr_t ossvc_metadata  = 0xA01000;
    uintptr_t tsldr_metadata    = 0xA00000;
    uintptr_t tsldr_program     = 0x200000;
    uintptr_t tsldr_context     = 0xE00000;
    // FIXME: the stack addr is wrong for x86??
#if defined(CONFIG_ARCH_X86_64)
    uintptr_t tsldr_stack_bottom    = 0x7fffffffe000;
#elif defined(CONFIG_ARCH_AARCH64)
    uintptr_t tsldr_stack_bottom    = 0x0FFFFFFF000;
#else
#error "Unsupported architecture for stack address"
#endif
    uintptr_t container_stack_bottom    = 0x00FFFBFF000;
    uintptr_t container_stack_top       = 0x00FFFC00000;
    uintptr_t client_elf = 0x2000000;

    TSLDR_DBG_PRINT("[@trampoline] entry of trampoline.\n");

    /* say goodbye to the old stack */
    tsldr_miscutil_memset((void *)tsldr_stack_bottom, 0, 0x1000);

    /* clean up trusted loader metadata... */
    tsldr_miscutil_memset((void *)tsldr_metadata, 0, 0x1000);

    /* clean up access rights group metadata */
    // is disposable...
    tsldr_miscutil_memset((void *)ossvc_metadata, 0, 0x1000);

    /* clean up trusted loader... */
    //tsldr_miscutil_memset((void *)tsldr_program, 0, 0x800000);

    /* clean up container stack... */
    tsldr_miscutil_memset((void *)container_stack_bottom, 0, 0x1000);

    // syscall for tsldr_context cleanup
    microkit_mr_set(0, 20);
    // try to call the monitor to backup trusted loading context
    microkit_msginfo info = microkit_ppcall(15, microkit_msginfo_new(0, 1));
    seL4_Error error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    // clean up trusted loading context...
    tsldr_miscutil_memset((void *)tsldr_context, 0, 0x1000);

    /* at this point we dont have access to the data section of tsldr */
    TSLDR_DBG_PRINT("[@trampoline] jumping to the client payload...\n");

    /*
     * At this point, the client information is embedded in the address space,
     * while the trusted loader and all older stacks are gone for good...
     * It's fine to just jump to the new stack/function for the real container
     */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)client_elf;
    entry_fn_t entry_fn = (entry_fn_t) ehdr->e_entry;

    TSLDR_DBG_PRINT("[@trampoline] stack: %x, entry function: %x\n", (unsigned long)container_stack_top, (unsigned long)entry_fn);
    tsldr_main_jump_with_stack((void *)container_stack_top, entry_fn);
}

void notified(microkit_channel ch)
{
    ;
}
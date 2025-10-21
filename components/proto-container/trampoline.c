#include <microkit.h>
#include <stdint.h>
#include <elf.h>
#include <elf_utils.h>

typedef void (*entry_fn_t)(void);

void init(void)
{
    uintptr_t acgroup_metadata  = 0xA01000;
    uintptr_t tsldr_metadata    = 0xA00000;
    uintptr_t tsldr_program     = 0x200000;
    uintptr_t tsldr_context     = 0xE00000;
    uintptr_t tsldr_stack_bottom        = 0x0FFFFFFF000;
    uintptr_t container_stack_bottom    = 0x00FFFBFF000;
    uintptr_t container_stack_top       = 0x00FFFC00000;
    uintptr_t client_elf = 0x2000000;

    microkit_dbg_puts("[@trampoline] Entry of trampoline.\n");

    /* say goodbye to the old stack */
    custom_memset((void *)tsldr_stack_bottom, 0, 0x1000);

    /* clean up trusted loader metadata... */
    custom_memset((void *)tsldr_metadata, 0, 0x1000);

    /* clean up access rights group metadata */
    // is disposable...
    custom_memset((void *)acgroup_metadata, 0, 0x1000);

    /* clean up trusted loader... */
    custom_memset((void *)tsldr_program, 0, 0x800000);

    /* clean up container stack... */
    custom_memset((void *)container_stack_bottom, 0, 0x1000);

    // syscall for tsldr_context cleanup
    microkit_mr_set(0, 20);
    // try to call the monitor to backup trusted loading context
    microkit_msginfo info = microkit_ppcall(15, microkit_msginfo_new(0, 1));
    seL4_Error error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }
    // clean up trusted loading context...
    custom_memset((void *)tsldr_context, 0, 0x1000);

    /* at this point we dont have access to the data section of tsldr */
    microkit_dbg_puts("[@trampoline] Exit of trampoline.\n");

    /*
     * At this point, the client information is embedded in the address space,
     * while the trusted loader and all older stacks are gone for good...
     * It's fine to just jump to the new stack/function for the real container
     */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)client_elf;
    entry_fn_t entry_fn = (entry_fn_t) ehdr->e_entry;

    asm volatile (
        "mov sp, %[new_stack]\n\t" /* set new SP */
        "br  %[func]\n\t"          /* branch directly, never return */
        :
        : [new_stack] "r" (container_stack_top),
          [func] "r" (entry_fn)
        : "x30", "memory"
    );
    __builtin_unreachable();
}

void notified(microkit_channel ch)
{
    ;
}
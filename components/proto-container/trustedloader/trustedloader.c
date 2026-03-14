
#include <acrtutils.h>
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

#if 0
seL4_Error tsldr_parse_rights(Elf64_Ehdr *ehdr, char *ref_section[], seL4_Word *size)
{
    if (ref_section == NULL || size == NULL) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid args to parse access rights\n");
        return seL4_InvalidArgument;
    }

    Elf64_Shdr *shdr = (Elf64_Shdr *)((char*)ehdr + ehdr->e_shoff);
    const char *shstrtab = (char*)ehdr + shdr[ehdr->e_shstrndx].sh_offset;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        const char *section_name = shstrtab + shdr[i].sh_name;
        if (custom_strcmp(section_name, ".access_rights") == 0) {
            *ref_section = (char*)ehdr + shdr[i].sh_offset;
            *size = shdr[i].sh_size;
            break;
        }
    }

    if (*ref_section == NULL) {
        microkit_dbg_printf(LIB_NAME_MACRO ".access_rights section not found in ELF\n");
        // halt...
        while (1);
        return seL4_InvalidArgument;
    }

    return seL4_NoError;
}
#endif


void tsldr_main_populate_all_rights(trusted_loader_t *loader, void *data)
{
    if (!loader || !data) {
        microkit_dbg_printf(LIB_NAME_MACRO "invalid loader pointer given\n");
        microkit_internal_crash(-1);
    }

    seL4_Word num = tsldr_acrtutil_check_access_rights_table(data);
    seL4_Word *p = (seL4_Word *)data;

    tsldr_acrtutil_populate_all_rights(loader, ++p, num);

    microkit_dbg_printf(LIB_NAME_MACRO "finished up access rights integrity checking\n");
}


seL4_Error tsldr_populate_allowed(trusted_loader_t *loader)
{
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid loader pointer given\n");
        return seL4_InvalidArgument;
    }
    AccessRights *rights = &loader->access_rights;
    // Reset allowed lists
    custom_memset(loader->allowed_channels, 0, sizeof(loader->allowed_channels));
    custom_memset(loader->allowed_irqs, 0, sizeof(loader->allowed_irqs));
    loader->num_allowed_mappings = 0;

    for (uint32_t i = 0; i < rights->num_entries; i++) {
        const AccessRightEntry *entry = &rights->entries[i];
        switch (entry->type) {
            case ACCESS_TYPE_CHANNEL:
                if (entry->data < MICROKIT_MAX_CHANNELS && tsldr_acrtutil_check_channel(entry->data, NULL)) {
                    loader->allowed_channels[entry->data] = true;
                    microkit_dbg_printf(LIB_NAME_MACRO "Allowed channel ID: %d\n", (unsigned long long)entry->data);
                } else {
                    microkit_dbg_printf(LIB_NAME_MACRO "Invalid channel ID: %d\n", (unsigned long long)entry->data);
                    return seL4_InvalidArgument;
                }
                break;

            case ACCESS_TYPE_IRQ:
                if (entry->data < MICROKIT_MAX_CHANNELS && tsldr_acrtutil_check_irq(entry->data)) {
                    loader->allowed_irqs[entry->data] = true;
                    microkit_dbg_printf(LIB_NAME_MACRO "Allowed IRQ ID: %d\n", (unsigned long long)entry->data);
                } else {
                    microkit_dbg_printf(LIB_NAME_MACRO "Invalid IRQ ID: %d\n", (unsigned long long)entry->data);
                    return seL4_InvalidArgument;
                }
                break;

            case ACCESS_TYPE_MEMORY:
                if (loader->num_allowed_mappings < MICROKIT_MAX_CHANNELS) {
                    seL4_Word vaddr = entry->data;
                    MemoryMapping *mapping = (MemoryMapping *)tsldr_acrtutil_check_mapping(vaddr);
                    if (mapping != NULL) {
                        loader->allowed_mappings[loader->num_allowed_mappings++] = mapping;
                        microkit_dbg_printf(LIB_NAME_MACRO "Allowed memory vaddr: 0x%x\n", (unsigned long long)vaddr);
                    } else {
                        microkit_dbg_printf(LIB_NAME_MACRO "Mapping not found for vaddr: 0x%x\n", (unsigned long long)vaddr);
                        return seL4_InvalidArgument;
                    }
                } else {
                    microkit_dbg_printf(LIB_NAME_MACRO "Number of allowed mappings exceeded\n");
                    return seL4_InvalidArgument;
                }
                break;

            default:
                microkit_dbg_printf(LIB_NAME_MACRO "Unknown access type: %d\n", (unsigned int)entry->type);
                return seL4_InvalidArgument;
        }
    }

    return seL4_NoError;
}

void tsldr_main_init_metadata(tsldr_md_array_t *array, size_t id, uintptr_t local_metadata_base)
{
    if (!array || !local_metadata_base) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid array pointer given\n");
        return;
    }
    if (id >= 16 || id < 0) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid template PD child ID given: %d\n", id);
        return;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");
    tsldr_md_t *target_md = &array->md_array[id];
    //microkit_dbg_printf(LIB_NAME_MACRO "%d %d\n", target_md->child_id, target_md->system_hash);
    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");

    /* initialise trusted loader metadata */
    custom_memset((tsldr_md_t *)local_metadata_base, 0, sizeof(tsldr_md_t));
    custom_memcpy((tsldr_md_t *)local_metadata_base, target_md, sizeof(tsldr_md_t));

    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");
    // one trusted loader in a proto-container may work for this container solely...
    /* copy corresponding metadata context for the trusted loader lib */
    ((tsldr_md_t *)local_metadata_base)->init = true;

    microkit_dbg_printf(LIB_NAME_MACRO "child_id: %d\n", ((tsldr_md_t *)local_metadata_base)->child_id);
}

void tsldr_main_try_init_loader(trusted_loader_t *c, size_t id)
{
    if (!c) {
        microkit_dbg_puts(LIB_NAME_MACRO "Try to init null loader\n");
        return;
    }
    if (c->init != true) {
        c->child_id = id;
        c->init = true;
        c->restore = false;
    }
}

void tsldr_main_remove_caps(trusted_loader_t *loader)
{
    if (!loader) {
        microkit_dbg_puts("tsldr_main_remove_caps:\n");
        microkit_dbg_puts(" invalid loader pointer given\n");
        microkit_internal_crash(-1);
    }
    /* set the flag to restore cap during restart */
    if (loader->restore == false) {
        microkit_dbg_puts("tsldr_main_remove_caps:\n");
        microkit_dbg_puts(" need to restore access rights in next round\n");
        loader->restore = true;
    }
    tsldr_acrtutil_revoke_channels(loader);
    tsldr_acrtutil_revoke_irqs(loader);
    tsldr_acrtutil_restore_mappings(loader);
}

void tsldr_main_restore_caps(trusted_loader_t *loader)
{
    if (!loader) {
        microkit_dbg_puts("tsldr_main_restore_caps:\n");
        microkit_dbg_puts(" invalid loader pointer given\n");
        microkit_internal_crash(-1);
    }
    /* if no need to restore caps */
    if (loader->restore == false) {
        microkit_dbg_puts("tsldr_main_restore_caps:\n");
        microkit_dbg_puts(" first run, no need to restore anything\n");
        return;
    }
    tsldr_acrtutil_restore_channels(loader);
    tsldr_acrtutil_restore_irqs(loader);
    tsldr_acrtutil_revoke_mappings(loader);
}


void tsldr_main_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack)
{
    microkit_dbg_printf(LIB_NAME_MACRO "Entry of trusted loader epilogue\n");

    tsldr_caputil_pd_deprivilege();

    // FIXME: currently the size of exec section is fixed
    custom_memset((void *)client_exec, 0, 0x1000);

    // TODO: refresh the client stack...
    // -> the client should use a different stack with the trusted loader

    microkit_dbg_printf(LIB_NAME_MACRO "Exit of trusted loader epilogue\n");
}


void tsldr_main_loading_prologue(void *metadata_base, trusted_loader_t *loader)
{
    tsldr_md_t *md = (tsldr_md_t *)metadata_base;
    if (!md->init) {
        microkit_dbg_printf("[@protocon] trusted loading metadata is not prepared...\n");
        microkit_internal_crash(-1);
    }
    microkit_dbg_printf("[@protocon]" "trusted loading metadata is ready...\n");
    microkit_dbg_printf(LIB_NAME_MACRO "trusted loader init prologue\n");

    /* do some id activation here before actually parsing access rights... */
    tsldr_main_try_init_loader(loader, md->child_id);
}


#if defined(CONFIG_ARCH_X86_64)
__attribute__((noreturn))
void tsldr_main_jump_with_stack(void *new_stack, void (*entry)(void))
{
    __asm__ volatile(
        "mov %rdi, %rsp\n\t"   /* new_stack in rdi */
        "jmp *%rsi\n\t"        /* entry in rsi */
    );
    __builtin_unreachable();
}
#elif defined(CONFIG_ARCH_AARCH64)
__attribute__((noreturn))
void tsldr_main_jump_with_stack(void *new_stack, void (*entry)(void))
{
    /* jump tp trampoline */
    asm volatile (
        "mov sp, %[new_stack]\n\t" /* set new SP */
        "br  %[func]\n\t"          /* branch directly, never return */
        :
        : [new_stack] "r" (new_stack),
          [func] "r" (entry)
        : "x30", "memory"
    );
    __builtin_unreachable();
}
#else
#error "Unsupported architecture for 'tsldr_main_jump_with_stack'"
#endif


void tsldr_main_check_elf_integrity(uintptr_t elf)
{
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf;
    /* check elf integrity */
    if (custom_memcmp(ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        //microkit_dbg_printf("[@protocon]" "Data in shared memory region must be an ELF file\n");
        microkit_internal_crash(-1);
    //} else {
    //    microkit_dbg_printf("[@protocon]" "Data in shared memory region is an ELF file\n");
    }
    microkit_dbg_printf("[@protocon] " "verified ELF header\n");
}


void tsldr_main_self_loading(void *metadata_base, void *acrt_stat_base, trusted_loader_t *context, uintptr_t client_elf, uintptr_t client_exec_region, uintptr_t trampoline_elf, uintptr_t trampoline_stack_top)
{
    tsldr_main_loading_prologue(metadata_base, context);


    /* start to parse client elf information */
    tsldr_main_check_elf_integrity(client_elf);
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)client_elf;
    tsldr_main_check_elf_integrity(trampoline_elf);
    Elf64_Ehdr *trampoline_ehdr = (Elf64_Ehdr *)trampoline_elf;


    /* populate the access rights to the loader */
    tsldr_main_populate_all_rights(context, acrt_stat_base);


    tsldr_main_restore_caps(context);

    /* (really) populate allowed access rights */
    seL4_Error error = tsldr_populate_allowed(context);
    if (error != seL4_NoError) {
        microkit_internal_crash(-1);
    }

    tsldr_main_remove_caps(context);

    tsldr_main_loading_epilogue(client_exec_region, (uintptr_t)0x0);

    load_elf((void *)ehdr->e_entry, ehdr);
    microkit_dbg_printf("[@protocon]" "Load client elf to the targeting memory region\n");

    load_elf((void *)trampoline_ehdr->e_entry, trampoline_ehdr);
    microkit_dbg_printf("[@protocon]" "Load trampoline elf to the targeting memory region\n");

    /* -- now we are ready to jump to the trampoline -- */

    microkit_dbg_printf("[@protocon]" "Switch to the trampoline's code to execute\n");
    tsldr_main_jump_with_stack((void *)trampoline_stack_top, (entry_fn_t)trampoline_ehdr->e_entry);   
}




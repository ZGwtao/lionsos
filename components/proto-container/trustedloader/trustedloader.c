
#include <acrtutils.h>
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

extern uintptr_t tsldr_metadata;

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


void encode_access_rights_to(void *base,
                            const uint64_t *channel_ids, size_t n_channels,
                            const uint64_t *irq_ids,     size_t n_irqs,
                            const uint64_t *memory_vaddrs,size_t n_vaddrs)
{
    AccessRightEntry *p = (AccessRightEntry *)base;

    for (size_t i = 0; i < n_channels; ++i) {
        p->type = (uint8_t)TYPE_CHANNEL;
        p->data = (seL4_Word)channel_ids[i];
        p++;
    }
    for (size_t i = 0; i < n_irqs; ++i) {
        p->type = (uint8_t)TYPE_IRQ;
        p->data = (seL4_Word)irq_ids[i];
        p++;
    }
    for (size_t i = 0; i < n_vaddrs; ++i) {
        p->type = (uint8_t)TYPE_MEMORY;
        p->data = (seL4_Word)memory_vaddrs[i];
        p++;
    }
}

seL4_Word tsldr_acrtutil_check_access_rights_table(void *base)
{
    if (!base) {
        microkit_dbg_puts(" tsldr_acrtutil_check_access_rights_table:\n");
        microkit_dbg_puts(" invalid pointer given\n");
        microkit_internal_crash(-1);
    }

    size_t *p = (size_t *)base;
    seL4_Word acrt_num = *p;

    microkit_dbg_puts(" tsldr_acrtutil_check_access_rights_table:\n");
    microkit_dbg_puts(" number of access rights checked '");
    microkit_dbg_put32(acrt_num);
    microkit_dbg_puts("'\n");

    return acrt_num;
}


seL4_Error tsldr_populate_rights(trusted_loader_t *loader, void *data)
{
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "invalid loader pointer given\n");
        return seL4_InvalidArgument;
    }

    seL4_Word num = tsldr_acrtutil_check_access_rights_table(data);
    seL4_Word *p = (seL4_Word *)data;

    tsldr_acrtutil_populate_all_rights(loader, ++p, num);

    return seL4_NoError;
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

void tsldr_init_metadata(tsldr_md_array_t *array, size_t id)
{
    if (!array) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid array pointer given\n");
        return;
    }
    if (id >= 64 || id < 0) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid template PD child ID given: %d\n", id);
        return;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");
    tsldr_md_t *target_md = &array->md_array[id];
    //microkit_dbg_printf(LIB_NAME_MACRO "%d %d\n", target_md->child_id, target_md->system_hash);
    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");

    /* initialise trusted loader metadata */
    custom_memset((tsldr_md_t *)tsldr_metadata, 0, sizeof(tsldr_md_t));
    custom_memcpy((tsldr_md_t *)tsldr_metadata, target_md, sizeof(tsldr_md_t));

    microkit_dbg_printf(LIB_NAME_MACRO "=>>\n");
    // one trusted loader in a proto-container may work for this container solely...
    /* copy corresponding metadata context for the trusted loader lib */
    ((tsldr_md_t *)tsldr_metadata)->init = true;

    microkit_dbg_printf(LIB_NAME_MACRO "child_id: %d\n", ((tsldr_md_t *)tsldr_metadata)->child_id);
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
    }
}

void tsldr_remove_caps(trusted_loader_t *loader)
{
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid loader pointer given\n");
        return;
    }

    /* set the flag to restore cap during restart */
    if (!loader->flags.flag_restore_caps)
        loader->flags.flag_restore_caps = true;

    tsldr_acrtutil_revoke_channels(loader);
    tsldr_acrtutil_revoke_irqs(loader);
    tsldr_acrtutil_restore_mappings(loader);

}

void tsldr_restore_caps(trusted_loader_t *loader)
{
    microkit_dbg_printf(LIB_NAME_MACRO "Entry of caps restore\n");
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid loader pointer given\n");
        return;
    }

    /* if no need to restore caps */
    if (!loader->flags.flag_restore_caps) {
        microkit_dbg_printf(LIB_NAME_MACRO "No caps to restore at this point\n");
        return;
    }

    tsldr_acrtutil_restore_channels(loader);
    tsldr_acrtutil_restore_irqs(loader);
    tsldr_acrtutil_revoke_mappings(loader);

    microkit_dbg_printf(LIB_NAME_MACRO "Exit of caps restore\n");
}


seL4_Error tsldr_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack)
{
    microkit_dbg_printf(LIB_NAME_MACRO "Entry of trusted loader epilogue\n");

    tsldr_caputil_pd_deprivilege();

    // FIXME: currently the size of exec section is fixed
    custom_memset((void *)client_exec, 0, 0x1000);

    // TODO: refresh the client stack...
    // -> the client should use a different stack with the trusted loader

    microkit_dbg_printf(LIB_NAME_MACRO "Exit of trusted loader epilogue\n");
    return seL4_NoError;
}


seL4_Error tsldr_loading_prologue(trusted_loader_t *loader)
{
    microkit_dbg_printf(LIB_NAME_MACRO "trusted loader init prologue\n");

    if (!loader->flags.flag_bootstrap) {
        /* set flag to prevent re-initialisation */
        loader->flags.flag_bootstrap = true;
        microkit_dbg_printf(LIB_NAME_MACRO "Bootstrap trusted loader\n");

    } else {
        microkit_dbg_printf(LIB_NAME_MACRO "Restart trusted loader\n");
    }
    return seL4_NoError;
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



void tsldr_main_self_loading(void *metadata_base, void *acrt_stat_base, trusted_loader_t *context, uintptr_t client_elf, uintptr_t client_exec_region, uintptr_t trampoline_elf, uintptr_t trampoline_stack_top)
{
    microkit_dbg_printf("[@protocon]" "Entered init\n");

    tsldr_md_t *md = (tsldr_md_t *)metadata_base; /* tsldr_metadata */
    if (!md->init) {
        microkit_internal_crash(-1);
    }
    microkit_dbg_printf("[@protocon]" "trusted loading metadata is ready...\n");

    seL4_Error error = tsldr_loading_prologue(context);
    if (error != seL4_NoError) {
        microkit_dbg_printf("[@protocon]" "trusted loading prologue fails!\n");
        microkit_internal_crash(error);
    }

    tsldr_main_try_init_loader(context, md->child_id);

#if 1
    /* start to parse client elf information */
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)client_elf;
    /* check elf integrity */
    if (custom_memcmp(ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        microkit_dbg_printf("[@protocon]" "Data in shared memory region must be an ELF file\n");
        microkit_internal_crash(-1);
    } else {
        microkit_dbg_printf("[@protocon]" "Data in shared memory region is an ELF file\n");
    }
    microkit_dbg_printf("[@protocon]" "Verified ELF header\n");
#endif
    Elf64_Ehdr *trampoline_ehdr = (Elf64_Ehdr *)trampoline_elf;
    /* check elf integrity */
    if (custom_memcmp(trampoline_ehdr->e_ident, (const unsigned char*)ELFMAG, SELFMAG) != 0) {
        microkit_dbg_printf("[@protocon]" "Data in trampoline region must be an ELF file\n");
        microkit_internal_crash(-1);
    } else {
        microkit_dbg_printf("[@protocon]" "Data in trampoline region is an ELF file\n");
    }
    microkit_dbg_printf("[@protocon]" "Verified ELF header\n");

#if 0
    char *section = (char *)acgroup_metadata;
    seL4_Word section_size = 0;

    /* parse access rights table */
    error = tsldr_parse_rights(ehdr, &section, &section_size);
    if (error) {
        microkit_internal_crash(error);
    }
#endif
    /* populate the access rights to the loader */
    error = tsldr_populate_rights(context, acrt_stat_base);
    if (error) {
        microkit_internal_crash(-1);
    }
    microkit_dbg_printf("[@protocon]" "Finished up access rights integrity checking\n");

    tsldr_restore_caps(context);

    /* (really) populate allowed access rights */
    error = tsldr_populate_allowed(context);
    if (error != seL4_NoError) {
        microkit_internal_crash(-1);
    }

    tsldr_remove_caps(context);

    tsldr_loading_epilogue(client_exec_region, (uintptr_t)0x0);

    load_elf((void *)ehdr->e_entry, ehdr);
    microkit_dbg_printf("[@protocon]" "Load client elf to the targeting memory region\n");

    load_elf((void *)trampoline_ehdr->e_entry, trampoline_ehdr);
    microkit_dbg_printf("[@protocon]" "Load trampoline elf to the targeting memory region\n");

    /* -- now we are ready to jump to the trampoline -- */

    microkit_dbg_printf("[@protocon]" "Switch to the trampoline's code to execute\n");
    entry_fn_t entry_fn = (entry_fn_t) trampoline_ehdr->e_entry;

    tsldr_main_jump_with_stack((void *)trampoline_stack_top, entry_fn);   
}




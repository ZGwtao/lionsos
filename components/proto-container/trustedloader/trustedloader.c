
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


/* move the access rights information to the loader context */
// initialise trusted loader context
// the input is now comming from the acg data
// and the trusted loading library can use the data
// from the trusted loader context
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

void tsldr_init(trusted_loader_t *loader, size_t id)
{
    if (!loader) {
        microkit_dbg_puts(LIB_NAME_MACRO "Try to init null loader\n");
        return;
    }
    loader->child_id = id;
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


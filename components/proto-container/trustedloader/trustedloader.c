
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

extern uintptr_t tsldr_metadata;

MemoryMapping *tsldr_find_mapping_by_vaddr(trusted_loader_t *loader, seL4_Word vaddr, bool sldr, void *data)
{
    if (!data) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid data pointer given\n");
        return NULL;
    }

    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid loader pointer given\n");
        return NULL;
    }
    /* tsldr metadata */
    tsldr_md_t *md = (tsldr_md_t *)data;
    if (md->init != true || loader->flags.init != true) {
        microkit_dbg_printf(LIB_NAME_MACRO "Uninitialised trusted loader\n");
        return NULL;
    }
    for (seL4_Word i = 0; i < MICROKIT_MAX_CHANNELS; i++) {
        if (md->mappings[i].vaddr == vaddr) {
            return &md->mappings[i];
        }
    }

    return NULL;
}

static seL4_Word find_channel_by_index(trusted_loader_t *loader, seL4_Word index_data, uint8_t *cstate)
{
    tsldr_md_t *md = (tsldr_md_t *)tsldr_metadata;
    if (md->init != true || loader->flags.init != true) {
        microkit_dbg_printf(LIB_NAME_MACRO "Uninitialised trusted loader\n");
        return 0;
    }
    if (cstate != NULL) {
        *cstate = md->cstate[index_data];
    }
    return md->channels[index_data];
}

static seL4_Word find_irq_by_index(trusted_loader_t *loader, seL4_Word index_data)
{
    tsldr_md_t *md = (tsldr_md_t *)tsldr_metadata;
    if (md->init != true || loader->flags.init != true) {
        microkit_dbg_printf(LIB_NAME_MACRO "Uninitialised trusted loader\n");
        return 0;
    }
    return md->irqs[index_data];
}

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

// Write a u64 in little-endian regardless of host endianness.
static inline void write_u64_le(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32);
    p[5] = (uint8_t)(v >> 40);
    p[6] = (uint8_t)(v >> 48);
    p[7] = (uint8_t)(v >> 56);
}

// Writes: channels, then irqs, then virtual addrs.
// Returns total bytes written on success; 0 on insufficient capacity.
void encode_access_rights_to(void *base,
                            const uint64_t *channel_ids, size_t n_channels,
                            const uint64_t *irq_ids,     size_t n_irqs,
                            const uint64_t *memory_vaddrs,size_t n_vaddrs)
{
    uint8_t *p = (uint8_t *)base;

    // Channels
    for (size_t i = 0; i < n_channels; ++i) {
        *p++ = (uint8_t)TYPE_CHANNEL;
        write_u64_le(p, channel_ids[i]); p += 8;
    }
    // IRQs
    for (size_t i = 0; i < n_irqs; ++i) {
        *p++ = (uint8_t)TYPE_IRQ;
        write_u64_le(p, irq_ids[i]); p += 8;
    }
    // Memory virtual addresses
    for (size_t i = 0; i < n_vaddrs; ++i) {
        *p++ = (uint8_t)TYPE_MEMORY;
        write_u64_le(p, memory_vaddrs[i]); p += 8;
    }
}


/* move the access rights information to the loader context */
seL4_Error tsldr_populate_rights(trusted_loader_t *loader, const unsigned char *data, size_t len)
{
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "invalid loader pointer given\n");
        return seL4_InvalidArgument;
    }
    /* specify where to store access rights */
    AccessRights *rights = &loader->access_rights;
    // clean up access rights at each trusted loading time...
    custom_memset((void *)rights, 0, sizeof(AccessRights));

    access_rights_table_t *acg = (access_rights_table_t *)data;
    // init number of entries available...
    rights->num_entries = acg->len;

    microkit_dbg_printf(LIB_NAME_MACRO "Number of access rights: %d\n", rights->num_entries);

    // Check if the number of access rights exceeds the maximum allowed
    if (rights->num_entries > MAX_ACCESS_RIGHTS) {
        microkit_dbg_printf(LIB_NAME_MACRO "Number of access rights (%d) exceeds maximum allowed (%d)\n", rights->num_entries, MAX_ACCESS_RIGHTS);
        return seL4_InvalidArgument;
    }

    const unsigned char *access_rights_table = data + NUM_ENTRIES_SIZE;

    // Parse each access right entry
    for (uint32_t i = 0; i < rights->num_entries; i++) {
        AccessRightEntry *entry = &rights->entries[i];
        entry->type = (AccessType)*(access_rights_table + i * ACCESS_RIGHT_ENTRY_SIZE);
        entry->data = *((seL4_Word*)(access_rights_table + i * ACCESS_RIGHT_ENTRY_SIZE + sizeof(uint8_t)));
        microkit_dbg_printf(LIB_NAME_MACRO "Parsed access right %d: type=%d, data=0x%x\n", i, entry->type, (unsigned long long)entry->data);
    }

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
                if (entry->data < MICROKIT_MAX_CHANNELS && find_channel_by_index(loader, entry->data, NULL)) {
                    loader->allowed_channels[entry->data] = true;
                    microkit_dbg_printf(LIB_NAME_MACRO "Allowed channel ID: %d\n", (unsigned long long)entry->data);
                } else {
                    microkit_dbg_printf(LIB_NAME_MACRO "Invalid channel ID: %d\n", (unsigned long long)entry->data);
                    return seL4_InvalidArgument;
                }
                break;

            case ACCESS_TYPE_IRQ:
                if (entry->data < MICROKIT_MAX_CHANNELS && find_irq_by_index(loader, entry->data)) {
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
                    MemoryMapping *mapping = tsldr_find_mapping_by_vaddr(loader, vaddr, true, (void *)tsldr_metadata);
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

void tsldr_remove_caps(trusted_loader_t *loader, bool self_loading)
{
    if (!loader) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid loader pointer given\n");
        return;
    }

    /* set the flag to restore cap during restart */
    if (!loader->flags.flag_restore_caps)
        loader->flags.flag_restore_caps = true;

    //seL4_Error error;

    // Delete disallowed channel capabilities
    for (seL4_Word channel_id = 0; channel_id < MICROKIT_MAX_CHANNELS; channel_id++) {
        // try to record channel state: pp or notification
        uint8_t cstate = 0;
        // ...
        if (loader->allowed_channels[channel_id] || !find_channel_by_index(loader, channel_id, &cstate)) {
            continue;
        }
        // seL4_Word channel_base_cap = CNODE_NTFN_BASE_CAP;
        // if cstate is true, we should use ppc...
        if (cstate) {
            tsldr_caputil_revoke_ppc_cap(channel_id);
        } else {
            tsldr_caputil_revoke_notification_cap(channel_id);
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Deleted channel cap: channel_id=%d\n", channel_id);
    }

    // Delete disallowed IRQ capabilities
    for (seL4_Word irq_id = 0; irq_id < MICROKIT_MAX_CHANNELS; irq_id++) {
        if (loader->allowed_irqs[irq_id] || !find_irq_by_index(loader, irq_id)) {
            continue;
        }
        tsldr_caputil_revoke_irq_cap(irq_id);

        microkit_dbg_printf(LIB_NAME_MACRO "Deleted IRQ cap: irq_id=%d\n", irq_id);
    }

    tsldr_caputil_pd_grant_vspace_access();


    // Map only the allowed memory regions
    for (seL4_Word i = 0; i < loader->num_allowed_mappings; i++) {
        const MemoryMapping *mapping = loader->allowed_mappings[i];
        microkit_dbg_printf(LIB_NAME_MACRO "Mapping allowed memory: vaddr=0x%x\n", mapping->vaddr);

        seL4_CapRights_t rights = seL4_AllRights;
        // FIXME
        //rights.words[0] = mapping->rights;

        /* move target page from background CNode to current CNode */
        seL4_CPtr page_index = mapping->page;
        for (int i = 0; i < mapping->number_of_pages; ++i) {
            // for each mapping, map all pages in this region...
            page_index = mapping->page + i;

            tsldr_caputil_pd_grant_page_access(page_index, mapping->vaddr + i * mapping->page_size, rights, mapping->attrs);
        }

        microkit_dbg_printf(LIB_NAME_MACRO "Mapped allowed memory: page=0x%x vaddr=0x%x\n", mapping->page, mapping->vaddr);
    }

    tsldr_caputil_pd_revoke_vspace_access();
}

void tsldr_restore_caps(trusted_loader_t *loader, bool self_loading)
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

    // Restore disallowed channel capabilities
    for (seL4_Word channel_id = 0; channel_id < MICROKIT_MAX_CHANNELS; channel_id++) {
        // try to record channel state: pp or notification
        uint8_t cstate = 0;
        // ...
        if (loader->allowed_channels[channel_id] || !find_channel_by_index(loader, channel_id, &cstate)) {
            continue;
        }
        // if cstate is true, we should use ppc...
        if (cstate) {
            tsldr_caputil_restore_ppc_cap(channel_id);
        } else {
            tsldr_caputil_restore_notification_cap(channel_id);
        }

        microkit_dbg_printf(LIB_NAME_MACRO "Restored channel cap: channel_id=%d\n", channel_id);
    }

    // Restore disallowed IRQ capabilities
    for (seL4_Word irq_id = 0; irq_id < MICROKIT_MAX_CHANNELS; irq_id++) {
        if (loader->allowed_irqs[irq_id] || !find_irq_by_index(loader, irq_id)) {
            continue;
        }
        tsldr_caputil_restore_irq_cap(irq_id);

        microkit_dbg_printf(LIB_NAME_MACRO "Restored IRQ cap: irq_id=%d\n", irq_id);
    }

    tsldr_caputil_pd_grant_vspace_access();

    // Unmapped allowed memory mappings
    for (seL4_Word i = 0; i < loader->num_allowed_mappings; i++) {
        const MemoryMapping *mapping = loader->allowed_mappings[i];
        microkit_dbg_printf(LIB_NAME_MACRO "Unmapping mapping: vaddr=0x%x\n", mapping->vaddr);

        tsldr_caputil_pd_revoke_page_access(mapping->page);

        microkit_dbg_printf(LIB_NAME_MACRO "Unmapped mapping: vaddr=0x%x\n", mapping->vaddr);
    }

    tsldr_caputil_pd_revoke_vspace_access();

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

seL4_Error tsldr_grant_cspace_access(size_t child_id)
{
    microkit_dbg_printf(LIB_NAME_MACRO "child id: %d\n", child_id);

    /* sanity check */
    seL4_Error error = seL4_CNode_Delete(
        PD_TEMPLATE_CHILD_CSPACE_BASE + child_id,
        CNODE_SELF_CAP,
        PD_CAP_BITS
    );

    error = seL4_CNode_Delete(
        PD_TEMPLATE_CHILD_CSPACE_BASE + child_id,
        CNODE_BACKGROUND_CAP,
        PD_CAP_BITS
    );

    /* bring back cap to background CNode and template PD CNode */
    error = seL4_CNode_Copy(
        PD_TEMPLATE_CHILD_CSPACE_BASE + child_id,
        CNODE_SELF_CAP, /* self means the child itself */
        PD_CAP_BITS,
        PD_TEMPLATE_CNODE_ROOT,
        PD_TEMPLATE_CHILD_CSPACE_BASE + child_id,
        PD_CAP_BITS,
        seL4_AllRights
    );
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore CNode cap for the child\n");
        return error;
    }

    error = seL4_CNode_Copy(
        PD_TEMPLATE_CHILD_CSPACE_BASE + child_id,
        CNODE_BACKGROUND_CAP,  /* the background CNode in the container's CNode */
        PD_CAP_BITS,
        PD_TEMPLATE_CNODE_ROOT,
        PD_TEMPLATE_CHILD_BNODE_BASE + child_id, /* the background CNode in the monitor's CNode */
        PD_CAP_BITS,
        seL4_AllRights
    );
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore background CNode cap for the child\n");
        return error;
    }
    return seL4_NoError;
}
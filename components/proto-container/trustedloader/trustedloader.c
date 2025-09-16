
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "<libtrustedlo> "

extern uintptr_t tsldr_metadata;

MemoryMapping *tsldr_find_mapping_by_vaddr(trusted_loader_t *loader, seL4_Word vaddr, bool sldr, void *data)
{
    if (!data) {
        microkit_dbg_printf(LIB_NAME_MACRO "Invalid data pointer given\n");
        return NULL;
    }
    /* self-loading */
    if (sldr) {
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
    } else { /* loading from monitor */
        MemoryMapping *mappings = (MemoryMapping *)data;
        for (seL4_Word i = 0; i < MICROKIT_MAX_CHANNELS; i++) {
            if (mappings[i].vaddr == vaddr) {
                return mappings + i;
            }
        }
    }

    return NULL;
}

static seL4_Word find_channel_by_index(trusted_loader_t *loader, seL4_Word index_data)
{
    tsldr_md_t *md = (tsldr_md_t *)tsldr_metadata;
    if (md->init != true || loader->flags.init != true) {
        microkit_dbg_printf(LIB_NAME_MACRO "Uninitialised trusted loader\n");
        return 0;
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
        return seL4_InvalidArgument;
    }

    return seL4_NoError;
}


seL4_Error tsldr_populate_rights(trusted_loader_t *loader, const unsigned char *signed_message, size_t len)
{
    if (!loader) {
        microkit_dbg_puts("[trusred loader]: invalid loader pointer given\n");
        return seL4_InvalidArgument;
    }
    /* specify where to store access rights */
    AccessRights *rights = &loader->access_rights;
    custom_memset((void *)rights, 0, sizeof(AccessRights));

    // Calculate the minimum required size: signature + system_hash + num_access_rights
    size_t min_required_size = loader->signature_len + loader->hash_len + NUM_ENTRIES_SIZE;

    if (len < min_required_size) {
        microkit_dbg_puts("[trusted loader]: Signed message length is too short.\n");
        return seL4_InvalidArgument;
    }

    const unsigned char *signature = signed_message;
    const unsigned char *data = signed_message + loader->signature_len;

    custom_memcpy(&rights->system_hash, data, loader->hash_len);
    custom_memcpy(&rights->num_entries, data + loader->hash_len, NUM_ENTRIES_SIZE);

    microkit_dbg_printf(LIB_NAME_MACRO "System hash (from access rights section of ELF file): 0x%x\n", rights->system_hash);
    microkit_dbg_printf(LIB_NAME_MACRO "Number of access rights: %d\n", rights->num_entries);

    // Check if the number of access rights exceeds the maximum allowed
    if (rights->num_entries > MAX_ACCESS_RIGHTS) {
        microkit_dbg_printf(LIB_NAME_MACRO "Number of access rights (%d) exceeds maximum allowed (%d)\n", rights->num_entries, MAX_ACCESS_RIGHTS);
        return seL4_InvalidArgument;
    }

    // Verify system_hash matches
    if (rights->system_hash != loader->system_hash) {
        microkit_dbg_printf(LIB_NAME_MACRO "System hash mismatch: expected 0x%x, found 0x%x\n",
                           (unsigned long)loader->system_hash,
                           (unsigned long)rights->system_hash);
        return seL4_InvalidArgument;
    }

    microkit_dbg_printf(LIB_NAME_MACRO "Extracted system hash and trusted loader's system hash matched successfully\n");

    // Calculate the expected total size based on the number of access rights
    size_t data_size = loader->hash_len + NUM_ENTRIES_SIZE + (rights->num_entries * ACCESS_RIGHT_ENTRY_SIZE);
    
    if (len < loader->signature_len + data_size) {
        microkit_dbg_printf(LIB_NAME_MACRO "Verified data size (%d) is less than expected size (%d)\n", len, loader->signature_len + data_size);
        return seL4_InvalidArgument;
    }

    // Print the public key in hex
    microkit_dbg_printf(LIB_NAME_MACRO "Public key: 0x");
    for (int i = 0; i < PUBLIC_KEY_BYTES; i++) {
        microkit_dbg_printf("%x", loader->public_key[i]);
    }
    microkit_dbg_printf("\n");

    // Print the signature in hex
    microkit_dbg_printf(LIB_NAME_MACRO "Signature: 0x");
    for (int i = 0; i < loader->signature_len; i++) {
        microkit_dbg_printf("%x", signature[i]);
    }
    microkit_dbg_printf("\n");

    // Print the data in hex (optional, can be removed in production)
    microkit_dbg_printf(LIB_NAME_MACRO "Data (size %d bytes): 0x", data_size);
    for (size_t i = 0; i < data_size; i++) {
        microkit_dbg_printf("%x", data[i]);
    }
    microkit_dbg_printf("\n");

    // Perform signature verification
    int valid_signature = loader->verify_func(signature, data, data_size, loader->public_key);

    if (valid_signature != 1) {
        microkit_dbg_printf(LIB_NAME_MACRO "ed25519_verify failed. Invalid signature.\n");
        return seL4_InvalidArgument;
    }

    microkit_dbg_printf(LIB_NAME_MACRO "ed25519_verify succeeded. Signature is valid.\n");

    const unsigned char *access_rights_table = data + loader->hash_len + NUM_ENTRIES_SIZE;

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
                if (entry->data < MICROKIT_MAX_CHANNELS && find_channel_by_index(loader, entry->data)) {
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
                        loader->allowed_mappings[loader->num_allowed_mappings++] = *mapping;
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

void tsldr_init_metadata(tsldr_md_t *tsldr_metadata_patched)
{
    /* initialise trusted loader metadata */
    custom_memset((tsldr_md_t *)tsldr_metadata, 0, sizeof(tsldr_md_t));
    custom_memcpy((tsldr_md_t *)tsldr_metadata, tsldr_metadata_patched, sizeof(tsldr_md_t));
    ((tsldr_md_t *)tsldr_metadata)->init = true;
}

void tsldr_init(trusted_loader_t *loader, crypto_verify_fn fn, seL4_Word hash_val, size_t hash_len, size_t signature_len)
{
    if (!loader) {
        microkit_dbg_puts(LIB_NAME_MACRO "Try to init null loader\n");
        return;
    }
    loader->verify_func = fn;
    loader->system_hash = hash_val;
    loader->hash_len = hash_len;
    loader->signature_len = signature_len;
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

    seL4_Error error;

    // Delete disallowed channel capabilities
    for (seL4_Word channel_id = 0; channel_id < MICROKIT_MAX_CHANNELS; channel_id++) {
        if (loader->allowed_channels[channel_id] || !find_channel_by_index(loader, channel_id)) {
            continue;
        }

        if (self_loading) {
            error = seL4_CNode_Delete(
                CNODE_SELF_CAP,
                CNODE_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS
            );
        } else {
            error = seL4_CNode_Delete(
                PD_TEMPLATE_CHILD_CNODE,
                CNODE_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS
            );
        }

        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to delete channel cap: channel_id=%d error=%d\n", channel_id, error);
            microkit_internal_crash(error);
        }

        microkit_dbg_printf(LIB_NAME_MACRO "Deleted channel cap: channel_id=%d\n", channel_id);   
    }

    // Delete disallowed IRQ capabilities
    for (seL4_Word irq_id = 0; irq_id < MICROKIT_MAX_CHANNELS; irq_id++) {
        if (loader->allowed_irqs[irq_id] || !find_irq_by_index(loader, irq_id)) {
            continue;
        }

        if (self_loading) {
            error = seL4_CNode_Delete(
                CNODE_SELF_CAP,
                CNODE_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS
            );
        } else {
            error = seL4_CNode_Delete(
                PD_TEMPLATE_CHILD_CNODE,
                CNODE_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS
            );
        }

        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to delete IRQ cap: irq_id=%d error=%d\n", irq_id, error);
            microkit_internal_crash(error);
        }

        microkit_dbg_printf(LIB_NAME_MACRO "Deleted IRQ cap: irq_id=%d\n", irq_id);
    }

    if (self_loading) {
        error = seL4_CNode_Move(
            CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS,
            CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS);
        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace to current CNode for manipulation\n");
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to current CNode for manipulation\n");
    }

    // Map only the allowed memory regions
    for (seL4_Word i = 0; i < loader->num_allowed_mappings; i++) {
        const MemoryMapping *mapping = &loader->allowed_mappings[i];
        microkit_dbg_printf(LIB_NAME_MACRO "Mapping allowed memory: vaddr=0x%x\n", mapping->vaddr);

        seL4_CapRights_t rights = seL4_AllRights;
        rights.words[0] = mapping->rights;

        if (self_loading) {
            /* move target page from background CNode to current CNode */
            seL4_CPtr page_index = mapping->page - CNODE_CHILD_BASE_MAPPING_CAP;
            error = seL4_CNode_Move(
                CNODE_SELF_CAP, CNODE_BASE_MAPPING_CAP + page_index, PD_CAP_BITS,
                CNODE_BACKGROUND_CAP, BACKGROUND_MAPPING_BASE_CAP + page_index, PD_CAP_BITS);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page to current CNode for mapping\n");
            }
            microkit_dbg_printf(LIB_NAME_MACRO "Move target page to current CNode for mapping\n");

            /* map target page at current CNode */
            error = seL4_ARM_Page_Map(
                CNODE_BASE_MAPPING_CAP + page_index,
                CNODE_VSPACE_CAP,
                mapping->vaddr,
                rights,
                mapping->attrs
            );
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to map memory: vaddr=0x%x error=%d\n", mapping->vaddr, error);
                microkit_internal_crash(error);
            }

            /* backing up the mapped page */
            error = seL4_CNode_Move(
                CNODE_BACKGROUND_CAP, BACKGROUND_MAPPING_BASE_CAP + page_index, PD_CAP_BITS,
                CNODE_SELF_CAP, CNODE_BASE_MAPPING_CAP + page_index, PD_CAP_BITS);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page back to background CNode for backup\n");
            }
            microkit_dbg_printf(LIB_NAME_MACRO "Move target page back to background CNode for backup\n");
        } else {
            error = seL4_ARM_Page_Map(
                mapping->page,
                PD_TEMPLATE_CHILD_VSPACE,
                mapping->vaddr,
                rights,
                mapping->attrs
            );
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to map memory: vaddr=0x%x error=%d\n", mapping->vaddr, error);
                microkit_internal_crash(error);
            }
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Mapped allowed memory: page=0x%x vaddr=0x%x\n", mapping->page, mapping->vaddr);
    }

    if (self_loading) {
        error = seL4_CNode_Move(
            CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS,
            CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS);
        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace back to background CNode for backup\n");
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to background CNode for backup\n");
    }
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

    seL4_Error error;

    // Restore disallowed channel capabilities
    for (seL4_Word channel_id = 0; channel_id < MICROKIT_MAX_CHANNELS; channel_id++) {
        if (loader->allowed_channels[channel_id] || !find_channel_by_index(loader, channel_id)) {
            continue;
        }

        if (self_loading) {
            error = seL4_CNode_Copy(
                CNODE_SELF_CAP,
                CNODE_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS,
                CNODE_BACKGROUND_CAP,
                BACKGROUND_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS,
                seL4_AllRights
            );
        } else {
            error = seL4_CNode_Copy(
                PD_TEMPLATE_CHILD_CNODE,
                CNODE_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS,
                PD_TEMPLATE_CBG_CNODE,
                BACKGROUND_NTFN_BASE_CAP + channel_id,
                PD_CAP_BITS,
                seL4_AllRights
            );
        }
        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore channel cap: channel_id=%d error=%d\n", channel_id, error);
            microkit_internal_crash(error);
        }

        microkit_dbg_printf(LIB_NAME_MACRO "Restored channel cap: channel_id=%d\n", channel_id);
    }

    // Restore disallowed IRQ capabilities
    for (seL4_Word irq_id = 0; irq_id < MICROKIT_MAX_CHANNELS; irq_id++) {
        if (loader->allowed_irqs[irq_id] || !find_irq_by_index(loader, irq_id)) {
            continue;
        }

        if (self_loading) {
            error = seL4_CNode_Copy(
                CNODE_SELF_CAP,
                CNODE_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS,
                CNODE_BACKGROUND_CAP,
                BACKGROUND_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS,
                seL4_AllRights
            );
        } else {
            error = seL4_CNode_Copy(
                PD_TEMPLATE_CHILD_CNODE,
                CNODE_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS,
                PD_TEMPLATE_CBG_CNODE,
                BACKGROUND_IRQ_BASE_CAP + irq_id,
                PD_CAP_BITS,
                seL4_AllRights
            );
        }

        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore IRQ cap: irq_id=%d error=%d\n", irq_id, error);
            microkit_internal_crash(error);
        }
        
        microkit_dbg_printf(LIB_NAME_MACRO "Restored IRQ cap: irq_id=%d\n", irq_id);
    }

    if (self_loading) {
        error = seL4_CNode_Move(
            CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS,
            CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS);
        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace to current CNode for manipulation\n");
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to current CNode for manipulation\n");
    }

    // Unmapped allowed memory mappings
    for (seL4_Word i = 0; i < loader->num_allowed_mappings; i++) {
        const MemoryMapping *mapping = &loader->allowed_mappings[i];
        microkit_dbg_printf(LIB_NAME_MACRO "Unmapping mapping: vaddr=0x%x\n", mapping->vaddr);

        if (self_loading){
            /* move target page from background CNode to current CNode */
            seL4_CPtr page_index = mapping->page - CNODE_CHILD_BASE_MAPPING_CAP;
            error = seL4_CNode_Move(
                CNODE_SELF_CAP, CNODE_BASE_MAPPING_CAP + page_index, PD_CAP_BITS,
                CNODE_BACKGROUND_CAP, BACKGROUND_MAPPING_BASE_CAP + page_index, PD_CAP_BITS);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page to current CNode for unmapping\n");
            }
            microkit_dbg_printf(LIB_NAME_MACRO "Move target page to current CNode for unmapping\n");

            error = seL4_ARM_Page_Unmap(CNODE_BASE_MAPPING_CAP + page_index);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to unmap mapping: vaddr=0x%x error=%d\n", mapping->vaddr, error);
                microkit_internal_crash(error);
            }
            microkit_dbg_printf(LIB_NAME_MACRO "Unmap succeed\n");

            /* backing up the mapped page */
            error = seL4_CNode_Move(
                CNODE_BACKGROUND_CAP, BACKGROUND_MAPPING_BASE_CAP + page_index, PD_CAP_BITS,
                CNODE_SELF_CAP, CNODE_BASE_MAPPING_CAP + page_index, PD_CAP_BITS);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page back to background CNode for backup\n");
            }
            microkit_dbg_printf(LIB_NAME_MACRO "Move target page back to background CNode for backup\n");
        } else {
            error = seL4_ARM_Page_Unmap(mapping->page);
            if (error != seL4_NoError) {
                microkit_dbg_printf(LIB_NAME_MACRO "Failed to unmap mapping: vaddr=0x%x error=%d\n", mapping->vaddr, error);
                microkit_internal_crash(error);
            }
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Unmapped mapping: vaddr=0x%x\n", mapping->vaddr);
    }

    if (self_loading) {
        error = seL4_CNode_Move(
            CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS,
            CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS);
        if (error != seL4_NoError) {
            microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace back to background CNode for backup\n");
        }
        microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to background CNode for backup\n");
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Exit of caps restore\n");
}


seL4_Error tsldr_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack)
{
    microkit_dbg_printf(LIB_NAME_MACRO "Entry of trusted loader epilogue\n");

    seL4_Error error;

    error = seL4_CNode_Move(
        CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS,
        CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace to current CNode for manipulation\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to current CNode for manipulation\n");

    error = seL4_CNode_Move(
        CNODE_SELF_CAP, CNODE_TSLDR_CONTEXT_CAP, PD_CAP_BITS,
        CNODE_BACKGROUND_CAP, BACKGROUND_TSLDR_CONTEXT_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page to current CNode for unmapping\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target page to current CNode for unmapping\n");
    
    error = seL4_ARM_Page_Unmap(CNODE_TSLDR_CONTEXT_CAP);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to map context to initialise loader\n");
        return error;
    }

    /* backing up the mapped page */
    error = seL4_CNode_Move(
        CNODE_BACKGROUND_CAP, BACKGROUND_TSLDR_CONTEXT_CAP, PD_CAP_BITS,
        CNODE_SELF_CAP, CNODE_TSLDR_CONTEXT_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page back to background CNode for backup\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target page back to background CNode for backup\n");

    error = seL4_CNode_Move(
        CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS,
        CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace back to background CNode for backup\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to background CNode for backup\n");

    error = seL4_CNode_Delete(CNODE_SELF_CAP, CNODE_BACKGROUND_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Unable to remove cap of background CNode during epilogue\n");
        return error;
    }

    /* self-unauthorising */
    error = seL4_CNode_Delete(CNODE_SELF_CAP, CNODE_SELF_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Unable to remove cap of current CNode during epilogue\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Clean up access to the CNode for template PD\n");

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

    seL4_Error error = seL4_CNode_Move(
        CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS,
        CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace to current CNode for manipulation\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to current CNode for manipulation\n");

    error = seL4_CNode_Move(
        CNODE_SELF_CAP, CNODE_TSLDR_CONTEXT_CAP, PD_CAP_BITS,
        CNODE_BACKGROUND_CAP, BACKGROUND_TSLDR_CONTEXT_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page to current CNode for unmapping\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target page to current CNode for unmapping\n");
    
    error = seL4_ARM_Page_Map(
        CNODE_TSLDR_CONTEXT_CAP,
        CNODE_VSPACE_CAP,
        0xE00000,
        seL4_AllRights,
        2
    );
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to map context to initialise loader\n");
        return error;
    }

    /* backing up the mapped page */
    error = seL4_CNode_Move(
        CNODE_BACKGROUND_CAP, BACKGROUND_TSLDR_CONTEXT_CAP, PD_CAP_BITS,
        CNODE_SELF_CAP, CNODE_TSLDR_CONTEXT_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move target page back to background CNode for backup\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target page back to background CNode for backup\n");

    error = seL4_CNode_Move(
        CNODE_BACKGROUND_CAP, BACKGROUND_VSPACE_CAP, PD_CAP_BITS,
        CNODE_SELF_CAP, CNODE_VSPACE_CAP, PD_CAP_BITS);
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to move vspace back to background CNode for backup\n");
        return error;
    }
    microkit_dbg_printf(LIB_NAME_MACRO "Move target VSpace to background CNode for backup\n");

    if (!loader->flags.flag_bootstrap) {
        /* set flag to prevent re-initialisation */
        loader->flags.flag_bootstrap = true;
        microkit_dbg_printf(LIB_NAME_MACRO "Bootstrap trusted loader\n");

    } else {
        microkit_dbg_printf(LIB_NAME_MACRO "Restart trusted loader\n");


    }

    return seL4_NoError;
}

seL4_Error tsldr_grant_cspace_access(void)
{
    /* bring back cap to background CNode and template PD CNode */
    seL4_Error error = seL4_CNode_Copy(
        PD_TEMPLATE_CHILD_CNODE,
        CNODE_SELF_CAP, /* self means the child itself */
        PD_CAP_BITS,
        PD_TEMPLATE_CNODE_ROOT,
        PD_TEMPLATE_CHILD_CNODE,
        PD_CAP_BITS,
        seL4_AllRights
    );
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore CNode cap for the child\n");
        return error;
    }

    error = seL4_CNode_Copy(
        PD_TEMPLATE_CHILD_CNODE,
        CNODE_BACKGROUND_CAP,  /* the background CNode in the container's CNode */
        PD_CAP_BITS,
        PD_TEMPLATE_CNODE_ROOT,
        PD_TEMPLATE_CBG_CNODE, /* the background CNode in the monitor's CNode */
        PD_CAP_BITS,
        seL4_AllRights
    );
    if (error != seL4_NoError) {
        microkit_dbg_printf(LIB_NAME_MACRO "Failed to restore background CNode cap for the child\n");
        return error;
    }
    return seL4_NoError;
}
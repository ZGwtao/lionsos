
#include <acrtutils.h>
#include <caputils.h>
#include <miscutils.h>
#include <libtrustedlo.h>

inline uintptr_t tsldr_acrtutil_check_mapping(seL4_Word vaddr, void *mdinfo)
{
    tsldr_mdinfo_t *md = (tsldr_mdinfo_t *)mdinfo;
    for (int i = 0; i < MICROKIT_MAX_CHANNELS; i++) {
        if (md->mappings[i].vaddr == vaddr) {
            return (uintptr_t)&md->mappings[i];
        }
    }
    return 0x0;
}

inline uint8_t tsldr_acrtutil_check_channel(seL4_Word channel, uint8_t *cstate, void *mdinfo)
{
    tsldr_mdinfo_t *md = (tsldr_mdinfo_t *)mdinfo;
    if (cstate != NULL) {
        *cstate = md->cstate[channel];
    }
    return md->channels[channel];
}

inline uint8_t tsldr_acrtutil_check_irq(seL4_Word irq, void *mdinfo)
{
    tsldr_mdinfo_t *md = (tsldr_mdinfo_t *)mdinfo;
    return md->irqs[irq];
}



/* Restore disallowed channel capabilities from last run */
void tsldr_acrtutil_restore_channels(void *data, void *mdinfo)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    for (seL4_Word channel = 0; channel < MICROKIT_MAX_CHANNELS; channel++) {
        /*
         * If the channel id points to an allowed channel,
         * we don't need to restore it as it stays in the CNode
         */
        if (loader->allowed_channels[channel]) {
            continue;
        }
        /* try to record channel state: pp or notification */
        uint8_t is_ppc = 0;

        /* the channel id given is invalid, skip it */
        if (tsldr_acrtutil_check_channel(channel, &is_ppc, mdinfo) == false) {
            continue;
        }
        if (is_ppc)
            tsldr_caputil_restore_ppc_cap(channel);
        else
            tsldr_caputil_restore_notification_cap(channel);

        microkit_dbg_puts(" tsldr_acrtutil_restore_channels:\n");
        microkit_dbg_puts(" restore channel '");
        microkit_dbg_put32(channel);
        microkit_dbg_puts("'\n");

    }
}

/* Restore disallowed IRQ capabilities from last run */
void tsldr_acrtutil_restore_irqs(void *data, void *mdinfo)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    for (seL4_Word irq = 0; irq < MICROKIT_MAX_CHANNELS; irq++) {
        /*
         * If the IRQ id points to an allowed interrupt number,
         * we don't need to restore it as it stays in the CNode
         */
        if (loader->allowed_irqs[irq] || !tsldr_acrtutil_check_irq(irq, mdinfo)) {
            continue;
        }
        
        tsldr_caputil_restore_irq_cap(irq);

        microkit_dbg_puts(" tsldr_acrtutil_restore_irqs:\n");
        microkit_dbg_puts(" restore IRQ '");
        microkit_dbg_put32(irq);
        microkit_dbg_puts("'\n");

    }
}

void tsldr_acrtutil_restore_mappings(void *data)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    tsldr_caputil_pd_grant_vspace_access();

    for (seL4_Word i = 0; i < loader->mp_cnt; i++) {
        tsldr_mapping_t *m = (tsldr_mapping_t *)loader->allowed_mappings[i];

        seL4_CapRights_t rights = seL4_AllRights;
        // FIXME
        // rights.words[0] = mapping->rights;
        for (int i = 0; i < m->number_of_pages; ++i) {
            tsldr_caputil_pd_grant_page_access(m->page + i, m->vaddr + i * m->page_size, rights, m->attrs);
        }

        microkit_dbg_puts(" tsldr_acrtutil_restore_mappings:\n");
        microkit_dbg_puts(" restore (map) mapping '");
        microkit_dbg_put32(m->page);
        microkit_dbg_puts("' at vaddr '");
        // TODO: add put64 here for vaddr
        microkit_dbg_puts("'\n");
    }

    tsldr_caputil_pd_revoke_vspace_access();

}


void tsldr_acrtutil_revoke_channels(void *data, void *mdinfo)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    for (seL4_Word channel = 0; channel < MICROKIT_MAX_CHANNELS; channel++) {

        /*  If the channel is allowed, keep it */
        if (loader->allowed_channels[channel]) {
            continue;
        }

        /* try to record channel state: pp or notification */
        uint8_t is_ppc = 0;

        /* the channel id given is invalid, skip it as no need to delete it */
        if (tsldr_acrtutil_check_channel(channel, &is_ppc, mdinfo) == false) {
            continue;
        }
        if (is_ppc) {
            tsldr_caputil_revoke_ppc_cap(channel);
        } else {
            tsldr_caputil_revoke_notification_cap(channel);
        }

        microkit_dbg_puts(" tsldr_acrtutil_revoke_channels:\n");
        microkit_dbg_puts(" revoke channel '");
        microkit_dbg_put32(channel);
        microkit_dbg_puts("'\n");

    }
}

void tsldr_acrtutil_revoke_irqs(void *data, void *mdinfo)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    for (seL4_Word irq = 0; irq < MICROKIT_MAX_CHANNELS; irq++) {

        if (loader->allowed_irqs[irq] || !tsldr_acrtutil_check_irq(irq, mdinfo)) {
            continue;
        }
        tsldr_caputil_revoke_irq_cap(irq);

        microkit_dbg_puts(" tsldr_acrtutil_revoke_irqs:\n");
        microkit_dbg_puts(" revoke IRQ '");
        microkit_dbg_put32(irq);
        microkit_dbg_puts("'\n");

    }
}

void tsldr_acrtutil_revoke_mappings(void *data)
{
    /* initialise trusted loader context */
    tsldr_context_t *loader = (tsldr_context_t *)data;

    tsldr_caputil_pd_grant_vspace_access();

    for (seL4_Word i = 0; i < loader->mp_cnt; i++) {
        /*
         * for those mapping areas that are already mapped,
         * remove them before next run to create an empty PD
         */
        tsldr_mapping_t *m = (tsldr_mapping_t *)loader->allowed_mappings[i];

        tsldr_caputil_pd_revoke_page_access(m->page);

        microkit_dbg_puts(" tsldr_acrtutil_revoke_mappings:\n");
        microkit_dbg_puts(" revoke (unmap) mapping '");
        microkit_dbg_put32(m->page);
        microkit_dbg_puts("' at vaddr '");
        // TODO: add put64 here for vaddr
        microkit_dbg_puts("'\n");

    }

    tsldr_caputil_pd_revoke_vspace_access();

}



void tsldr_acrtutil_populate_all_rights(void *context_data, void *src_data, seL4_Word num)
{
    if (num > MAX_ACCESS_RIGHTS) {
        microkit_dbg_puts(" tsldr_acrtutil_populate_all_rights:\n");
        microkit_dbg_puts(" number of access rights given is too big '");
        microkit_dbg_put32(num);
        microkit_dbg_puts("'\n");
        return;
    }

    tsldr_context_t *loader = (tsldr_context_t *)context_data;
    tsldr_acrt_entry_t *input_base = (tsldr_acrt_entry_t *)(src_data);

    tsldr_acrt_table_t *rights_table = NULL;
    tsldr_acrt_entry_t *rights_entries = NULL;
    
    rights_table = &loader->acrt_required_table;
    tsldr_miscutil_memset((void *)rights_table, 0, sizeof(tsldr_acrt_table_t));
    rights_table->num_entries = num;

    for (int i = 0; i < rights_table->num_entries; ++i) {

        rights_entries = &rights_table->entries[i];
        rights_entries->type = input_base->type;
        rights_entries->data = input_base->data;
        input_base += 1;

        microkit_dbg_puts(" tsldr_acrtutil_populate_all_rights:\n");
        microkit_dbg_puts(" poplated access rights '");
        microkit_dbg_put32(i);
        microkit_dbg_puts("' with type '");
        microkit_dbg_put32(rights_entries->type);
        microkit_dbg_puts("' and data '");
        microkit_dbg_put32(rights_entries->data);
        microkit_dbg_puts("'\n");
    }
}


void tsldr_acrtutil_encode_rights(void *base, seL4_Word channels[], size_t n_channels, seL4_Word irqs[], size_t n_irqs, seL4_Word mappings[], size_t n_mps)
{
    tsldr_acrt_entry_t *p = (tsldr_acrt_entry_t *)base;
    for (size_t i = 0; i < n_channels; ++i) {
        p->type = (uint8_t)TYPE_CHANNEL;
        p->data = channels[i];
        p++;
    }
    for (size_t i = 0; i < n_irqs; ++i) {
        p->type = (uint8_t)TYPE_IRQ;
        p->data = irqs[i];
        p++;
    }
    for (size_t i = 0; i < n_mps; ++i) {
        p->type = (uint8_t)TYPE_MEMORY;
        p->data = mappings[i];
        p++;
    }
}


void tsldr_acrtutil_add_rights_to_whitelist(void *data, void *input, void *mdinfo)
{
    tsldr_context_t *loader = (tsldr_context_t *)data;
    tsldr_acrt_entry_t *entry = (tsldr_acrt_entry_t *)input;

    switch (entry->type) {
        case TYPE_CHANNEL:
            TSLDR_ASSERT(entry->data < MICROKIT_MAX_CHANNELS);
            TSLDR_ASSERT(tsldr_acrtutil_check_channel(entry->data, NULL, mdinfo));
            loader->allowed_channels[entry->data] = true;
            break;

        case TYPE_IRQ:
            TSLDR_ASSERT(entry->data < MICROKIT_MAX_CHANNELS);
            TSLDR_ASSERT(tsldr_acrtutil_check_irq(entry->data, mdinfo));
            loader->allowed_irqs[entry->data] = true;
            break;

        case TYPE_MEMORY:
            TSLDR_ASSERT(loader->mp_cnt < MICROKIT_MAX_CHANNELS);
            uintptr_t m = tsldr_acrtutil_check_mapping(entry->data, mdinfo);
            TSLDR_ASSERT(m);
            loader->allowed_mappings[loader->mp_cnt++] = (seL4_Word)m;
            break;

        default:
            microkit_dbg_puts(" tsldr_acrtutil_add_rights_to_whitelist:\n");
            microkit_dbg_puts(" unknown access rights '");
            microkit_dbg_put32(entry->type);
            microkit_internal_crash(-1);
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


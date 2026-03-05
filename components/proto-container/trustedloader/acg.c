
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

extern int acg_stat_map[MAX_PERM_CL_NUM][MAX_PERC_AK_NUM];

extern acgrp_arr_list_t *acgroup_metadata_base;

//
// initialise the global acgroup state map
//  => everything is read from the microkit patched metadata
//
void init_acg_state_map(void)
{
    acgrp_arr_list_t *ptr_spec_ar = (acgrp_arr_list_t *)microkit_template_spec_ar;
    microkit_dbg_printf(LIB_NAME_MACRO "%d\n", ptr_spec_ar->len);

    acgrp_array_t *acg_arr_ptr;
    size_t pd_num = ptr_spec_ar->len;

    microkit_dbg_printf(LIB_NAME_MACRO "number of available PDs that have acg: %d\n", pd_num);

    for (int i = 0; i < pd_num; ++i) {
        // fetch a client PD that contains acgroups
        acg_arr_ptr = &ptr_spec_ar->list[i];
        microkit_dbg_printf(LIB_NAME_MACRO "[acg_arr] - PD idx: %d\n", acg_arr_ptr->pd_idx);
        //assert(acg_arr_ptr->pd_idx <= MAX_PERM_CL_NUM);

        for (int j = 0; j < acg_arr_ptr->grp_num; ++j) {
            // check each acgroup
            acgrp_t *grp_ptr = &acg_arr_ptr->array[j];
            // if this is a valid group (which means initiliased)
            if (grp_ptr->grp_init != false) {
                // ensure this is a valid type...
                //assert(grp_ptr->grp_type <= MAX_PERC_AK_NUM);

                // FIXME here..
                int cur_num = acg_stat_map[i][grp_ptr->grp_type];
                // check if we have enough connections of a type
                if (cur_num >= MAX_PERK_NUM) {
                    // halt...
                    microkit_dbg_printf(LIB_NAME_MACRO "current number of %d type acg in PD%d is %d\n", grp_ptr->grp_type, i, cur_num);
                    microkit_internal_crash(-1);
                }
                // FIXME here...
                acg_stat_map[i][grp_ptr->grp_type]++;

                microkit_dbg_printf(LIB_NAME_MACRO "[acg_arr][acg: %d]: grp id:   %d\n", j, grp_ptr->grp_idx);
                microkit_dbg_printf(LIB_NAME_MACRO "[acg_arr][acg: %d]: grp type: %d\n", j, grp_ptr->grp_type);

                // iterate all available mapings of this acg...
                StrippedMapping *map_ptr = grp_ptr->mappings;
                for (int k = 0; k < 4; ++k) {
                    if (!map_ptr[k].vaddr) {
                        continue;
                    }
                    microkit_dbg_printf(LIB_NAME_MACRO "  =>: mappings[%d] vaddr: 0x%x, pn: %d, size: 0x%x\n",
                                        k, map_ptr[k].vaddr, map_ptr[k].number_of_pages, map_ptr[k].page_size);
                }
                uint8_t *e_ptr = grp_ptr->channels;
                for (int k = 0; k < 4; ++k) {
                    if (e_ptr[k] >= 62) {
                        continue;
                    }
                    microkit_dbg_printf(LIB_NAME_MACRO "  =>: channel[%d]: %d\n", k, e_ptr[k]);
                }
            }
        }
    }
}

typedef void (*patch_elf_connection_fn)(void *elf_base, char data_file[], uintptr_t vaddr);

void funq(int cid, acg_req_t *req, uintptr_t payload_base, patch_elf_connection_fn fn)
{
    // fill access rights group metadata now for the payload...
    // then the trusted loader will revoke unnecessary capabilities beside the ones we can to establish...
    access_rights_table_t *acg = (access_rights_table_t *)((unsigned char *)acgroup_metadata_base + 0x1000 * cid);

    // so the trusted loader will not care how these access rights entry sit
    // all we have to do is specifying a number of total rights while put them after the number
    // now the job is to collect all access rights from the acgroup from the given acg
    // but still, we need to choose a subset from the acgroup ...

    // this is the current alternative to choose a subset from...
    acgrp_array_t *acg_arr_ptr = &((acgrp_arr_list_t *)microkit_template_spec_ar)->list[cid];
    microkit_dbg_printf(LIB_NAME_MACRO "pd index of the given acg arr: %d\n", acg_arr_ptr->pd_idx);
    microkit_dbg_printf(LIB_NAME_MACRO "number of acgs in the acg arr: %d\n", acg_arr_ptr->grp_num);

    size_t num_channels = 0;
    size_t num_mappings = 0;
    // IRQ TODO

    seL4_Word channels[100];
    seL4_Word mappings[100];
    // IRQ TODO

    // get the subset from the above according to the instructions given in req...
    acgrp_t *grp_array = acg_arr_ptr->array;
    // check all available acgroups...
    for (int i = 0; i < acg_arr_ptr->grp_num; ++i) {
        if (!grp_array[i].grp_init) {
            continue;
        }
        uint8_t type = grp_array[i].grp_type;
        if (!req->acg_per_type_num[type]) {
            continue;
        }
        //
        for (int j = 0; j < 4; ++j) {
            if (grp_array[i].channels[j] >= 62) {
                continue;
            }
            channels[num_channels++] = grp_array[i].channels[j];
        }
        // IRQ TODO
        for (int j = 0; j < 4; ++j) {
            if (!grp_array[i].mappings[j].vaddr) {
                continue;
            }
            mappings[num_mappings++] = grp_array[i].mappings[j].vaddr;
        }
        // update the payload with given data path...
        fn((void *)payload_base, grp_array[i].data_path, req->acg_attr[type][req->acg_per_type_num[type] - 1]);

        microkit_dbg_printf(LIB_NAME_MACRO "update section with offset: 0x%x with %s\n", req->acg_attr[type][req->acg_per_type_num[type] - 1], grp_array[i].data_path);

        // update number of element under given type
        req->acg_per_type_num[type]--;
    }

    acg->len = num_channels + num_mappings;
    encode_access_rights_to((unsigned char *)acg + sizeof(size_t), channels, num_channels, NULL, 0, mappings, num_mappings);
}
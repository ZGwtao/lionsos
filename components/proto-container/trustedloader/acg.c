
#include <libtrustedlo.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

extern int acg_stat_map[MAX_PERM_CL_NUM][MAX_PERC_AK_NUM];
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

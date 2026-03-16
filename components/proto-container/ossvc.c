#include <ossvc.h>
#include <acrtutils.h>
#include <libtrustedlo.h>
#include <protocon.h>
#include <string.h>

#define LIB_NAME_MACRO "    => [@trustedlo] "

extern int monitor_svc_dist_map[PC_CHILD_PER_MONITOR_MAX_NUM][SVC_TYPE_MAX_NUM];


void monitor_ossvc_populate_all_svc_of_unipd(protocon_svcdb_t *svcdb, int map[])
{
    protocon_svc_t *curr_svc;
    for (int i = 0; i < svcdb->svc_num; ++i) {
        /* Iterate all OS services of a PD */
        curr_svc = &svcdb->array[i];
        if (curr_svc->svc_init == false) {
            continue;
        }
        /* Determine what type the OS service is */
        int svc_type = curr_svc->svc_type;
        /* Check the number of OS service of the same type */
        int num_curr_type = map[svc_type];
        if (num_curr_type >= SVC_PER_TYPE_MAX_NUM) {
            microkit_dbg_puts("Too many OS services of the same type\n");
            microkit_internal_crash(-1);
        }
        /* Pin the OS service on the map */
        map[curr_svc->svc_type]++;
    }
}

void monitor_init_ossvc_map()
{
    monitor_svcdb_t *svcdb_list = (monitor_svcdb_t *)microkit_template_spec_ar;

    for (int i = 0; i < svcdb_list->len; ++i) {

        protocon_svcdb_t *curr_svcdb = &svcdb_list->list[i];

        monitor_ossvc_populate_all_svc_of_unipd(curr_svcdb, monitor_svc_dist_map[i]);
    }
}


void monitor_patch_payload_with_ossvc__worker_func(protocon_svc_t *svc, protocon_svc_req_t *req, tsldr_acrtreq_t *req_acrt, patch_elf_connection_fn fn, uintptr_t payload_base)
{
    if (!svc->svc_init) {
        return;
    }
    uint8_t type = svc->svc_type;
    if (!req->num_svc_per_type[type]) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        if (svc->channels[i] >= MICROKIT_MAX_CHANNELS) {
            continue;
        }
        seL4_Word channel = req_acrt->num_req_channels;
        req_acrt->channels[channel] = (seL4_Word)svc->channels[i];
        req_acrt->num_req_channels++;
    }
    for (int i = 0; i < 4; ++i) {
        if (!svc->mappings[i].vaddr) {
            continue;
        }
        seL4_Word mapping = req_acrt->num_req_mappings;
        req_acrt->mappings[mapping] = (seL4_Word)svc->mappings[i].vaddr;
        req_acrt->num_req_mappings++;
    }

    // the third arg is vaddr for loading the datafile in the target elf??
    fn((void *)payload_base, svc->data_path, req->data_per_svc_instance[type][req->num_svc_per_type[type] - 1]);

    req->num_svc_per_type[type]--;
}


typedef void (*patch_elf_connection_fn)(void *elf_base, char data_file[], uintptr_t vaddr);

void monitor_patch_payload_with_ossvc_info(int cid, protocon_svc_req_t *req, uintptr_t payload_base, uintptr_t monitor_svcdb_base, patch_elf_connection_fn fn)
{
    // so the trusted loader will not care how these access rights entry sit
    // all we have to do is specifying a number of total rights while put them after the number
    // now the job is to collect all access rights from the acgroup from the given acg
    // but still, we need to choose a subset from the acgroup ...

    protocon_svcdb_t *svcdb = &((monitor_svcdb_t *)microkit_template_spec_ar)->list[cid];

    TSLDR_DBG_PRINT(LIB_NAME_MACRO "pd index of the given acg arr: %d\n", svcdb->pd_idx);
    TSLDR_DBG_PRINT(LIB_NAME_MACRO "number of acgs in the acg arr: %d\n", svcdb->svc_num);

    tsldr_acrtreq_t req_acrt;

    // get the subset from the above according to the instructions given in req...
    protocon_svc_t *curr_svc = svcdb->array;

    // check all available acgroups...
    for (int i = 0; i < svcdb->svc_num; ++i) {
        monitor_patch_payload_with_ossvc__worker_func(&curr_svc[i], req, &req_acrt, fn, payload_base);
    }

    seL4_Word *svc_num_ptr = (seL4_Word *)((char *)monitor_svcdb_base + 0x1000 * cid);
    unsigned char *svc_data_ptr = (unsigned char*)(svc_num_ptr + 1);

    *svc_num_ptr = req_acrt.num_req_channels + req_acrt.num_req_mappings + req_acrt.num_req_irqs;

    tsldr_main_monitor_encode_required_rights(svc_data_ptr, &req_acrt);
}


static inline seL4_Word monitor_match_ossvc_request_with_unipd(protocon_svc_req_t *req, int svc_dist_map[])
{
    seL4_Word mask = 0;
    for (int i = 0; i < SVC_TYPE_MAX_NUM; ++i) {
        mask |= (req->num_svc_per_type[i] > svc_dist_map[i]);
    }
    return mask;
}

int monitor_match_ossvc_request__worker_func(protocon_svc_req_t *req, protocon_lifecycle_state_t *protocon_states)
{
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if (protocon_states[i] == PROTOCON_ACTIVE) {
            continue;
        }
        seL4_Word mask = monitor_match_ossvc_request_with_unipd(req, monitor_svc_dist_map[i]);
        if (mask == 0) {
            return i;
        }
    }
    return PC_CHILD_PER_MONITOR_MAX_NUM;
}


void monitor_ossvc_parse_req_from_elf_section(void *elf_base, void *sh, protocon_svc_req_t *req)
{
    // parse the interface section ...
    // i.e., get the user-defined section for declaring what acgroups are wanted
    protocon_svc_desc_t *ib = (protocon_svc_desc_t *)(elf_base + (uint64_t)((Elf64_Shdr *)sh)->sh_offset);

    // the list of numbers of requested acgroups
    const uint8_t *nums = &ib->t1_num;
    // the corresponding types which map to the above list of numbers
    const protocon_svc_type_t *types = &ib->type1;
    const uintptr_t (*ifaces[PC_SVC_TYPE_MAX_NUM])[PC_SVC_PER_PD_MAX_NUM] = {
        &ib->t1_iface, &ib->t2_iface, &ib->t3_iface, &ib->t4_iface,
        &ib->t5_iface, &ib->t6_iface, &ib->t7_iface, &ib->t8_iface
    };

    for (int i = 0; i < PC_SVC_TYPE_MAX_NUM; ++i) {
        if (nums[i] == 0) { // pass...
            continue;
        }
        // fetch the number of interfaces...
        uint8_t n = nums[i] > PC_SVC_PER_PD_MAX_NUM ? PC_SVC_PER_PD_MAX_NUM : nums[i];

        req->num_svc_per_type[types[i]] = n;
        // fetch interface array
        const uintptr_t *arr = *ifaces[i];
        // check interface type and establish connections...

        switch(types[i]) {
        case FS_IFACE:
        case TIMER_IFACE:
        case SERIAL_IFACE: {
            for (uint8_t j = 0; j < n; ++j) {
                req->data_per_svc_instance[types[i]][j] = arr[j];
            }
            break;
        }
        default:
            TSLDR_DBG_PRINT(LIB_NAME_MACRO "Unsupported interface type: %d", types[i]);
            break;
        };
    }
}


int monitor_match_ossvc_request_with_available_pd(void *elf_base, void *sh, protocon_svc_req_t *req, protocon_lifecycle_state_t *protocon_states)
{
    monitor_ossvc_parse_req_from_elf_section(elf_base, sh, req);

    int cid = monitor_match_ossvc_request__worker_func(req, protocon_states);
    return cid;
}

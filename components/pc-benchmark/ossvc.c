#include <ossvc.h>
#include <acrtutils.h>
#include <libtrustedlo.h>
#include <protocon.h>
#include <string.h>

#ifndef PROGNAME
#define PROGNAME "[@pcbench_monitor] "
#endif

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

// FIXME: we should pass the map as an argument to avoid the dependency on the global variable,
//  but it is not a big deal for now as we are still in the early stage of prototyping
void monitor_init_ossvc_map()
{
    // we will populate the global map that records the distribution of OS services for each dynamic PD (protocon)
    // we just simply get the information from the microkit-patched region (microkit_monitor_ossvc_database) 
    monitor_svcdb_t *svcdb_list = (monitor_svcdb_t *)microkit_monitor_ossvc_database;
    // for all dynamic PDs, try to populate the map with this loop
    for (int i = 0; i < svcdb_list->len; ++i) {
        // get the pointer to the OS service database of this PD,
        protocon_svcdb_t *curr_svcdb = &svcdb_list->list[i];
        // for each dynamic PD, we will populate the map with the information of all OS services of this PD
        monitor_ossvc_populate_all_svc_of_unipd(curr_svcdb, monitor_svc_dist_map[i]);
    }
}


void monitor_worker_func__patch_payload_by_ptr(void *elf_base, char data_file[], uintptr_t vaddr)
{
    int err = 0;
    seL4_Word target_sh = tsldr_miscutil_fetch_elf_section_with_vaddr(elf_base, vaddr);
    if (!target_sh) {
        // the reason we allow early return in here is:
        //  a broken client program will only break a dynamic PD's execution
        //  we can still load a broken elf into a dynamic PD but keep the rest of the system safe
        // so, if unfortunately the client breaks something in its user-defined section
        // it is none of the monitor or dynamic PD's business, as we just need to restore a faulting PD...
        TSLDR_DBG_PRINT(PROGNAME "Failed to find the target section (vaddr '%x') to patch with\n", vaddr);
        return;
    }
#if 0
    pico_vfs_readfile2buf((void *)target_sh, data_file, &err);
    if (err != seL4_NoError) {
        TSLDR_DBG_PRINT(PROGNAME "Failed to patch payload with datafile '%s' at: %x", data_file, vaddr);
        // FIXME: we do nothing here, but should it behave like this?
    }
#endif
}

void monitor_patch_payload_with_ossvc__worker_func(protocon_svc_t *svc, protocon_svc_req_t *req, tsldr_acrtreq_t *req_acrt, uintptr_t payload_base)
{
    if (!svc->svc_init) {
        return;
    }
    uint8_t type = svc->svc_type;
    if (!req->num_svc_per_type[type]) {
        return;
    }
    // maximumlly, we allow each OS svc to have at most:
    //  - 4 channels
    //  - 4 irqs (not implemented here)
    //  - *4 x86ioports (not implemented in microkit)
    //  - 4 mappings (4 pieaces of memory regions)
    // these low-level access rights should be enough to describe an OS service
    for (int i = 0; i < 4; ++i) {
        if (svc->channels[i] >= MICROKIT_MAX_CHANNELS) {
            continue;
        }
        seL4_Word channel = req_acrt->num_req_channels;
        req_acrt->channels[channel] = (seL4_Word)svc->channels[i];
        req_acrt->num_req_channels++;
    }
    /* TODO: irq, and x86ioports... */
    for (int i = 0; i < 4; ++i) {
        if (!svc->mappings[i].vaddr) {
            continue;
        }
        seL4_Word mapping = req_acrt->num_req_mappings;
        req_acrt->mappings[mapping] = (seL4_Word)svc->mappings[i].vaddr;
        req_acrt->num_req_mappings++;
    }

    seL4_Word ptr_of_target_section_in_payload = \
        req->data_per_svc_instance[type][req->num_svc_per_type[type] - 1];

    // the third arg is vaddr for loading the datafile in the target elf??
    monitor_worker_func__patch_payload_by_ptr((void *)payload_base, svc->data_path, ptr_of_target_section_in_payload);

    req->num_svc_per_type[type]--;
}

void monitor_patch_payload_with_ossvc_info(int cid, protocon_svc_req_t *req, uintptr_t payload_base, uintptr_t monitor_svcdb_base)
{
    protocon_svcdb_t *svcdb = &((monitor_svcdb_t *)microkit_monitor_ossvc_database)->list[cid];

    TSLDR_DBG_PRINT(LIB_NAME_MACRO "pd index of the given os svcdb: %d\n", svcdb->pd_idx);
    TSLDR_DBG_PRINT(LIB_NAME_MACRO "number of svcs in the os svcdb: %d\n", svcdb->svc_num);

    // the request variable, which should be filled out with the low-level access rights information
    // we use it to record the access rights of the required OS services (i.e., svcs from above)
    // we will then send this thing to the trusted loading functions for actual trusted loading
    // the reason we need it is that the trusted loader does not handle high-level information
    // so we put an information flow transition that turns requested OS services into low-level details
    tsldr_acrtreq_t req_acrt;

    // the array that records all svcs of this pd
    protocon_svc_t *curr_svc = svcdb->array;

    // check all available os services, and patch each of them accordingly
    //  - patch the elf with information that describes the required os services
    // this is a part of the process of elf preparation
    for (int i = 0; i < svcdb->svc_num; ++i) {
        // basically it is similar to tell the client program where to access the OS services
        // (we will put the pointers to access the svcs in the given place specified by the client)
        monitor_patch_payload_with_ossvc__worker_func(&curr_svc[i], req, &req_acrt, payload_base);
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
    // if mask is zero, it means all requested OS services are no less than what have been provided
    // so that means we have a match between a dynamic PD and a set of OS services request
    return mask;
}

static inline int monitor_match_ossvc_request__worker_func(protocon_svc_req_t *req, protocon_lifecycle_state_t *protocon_states)
{
    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if (protocon_states[i] == PROTOCON_ACTIVE) {
            continue;
        }
        // check each dynamic pd and see if any of them matches with the OS service request
        seL4_Word mask = monitor_match_ossvc_request_with_unipd(req, monitor_svc_dist_map[i]);
        if (mask == 0) {
            return i;
        }
    }
    return PC_CHILD_PER_MONITOR_MAX_NUM;
}

static inline void monitor_ossvc_init_req_per_type(protocon_svc_req_t *req, protocon_svc_type_t svc_type, uint8_t svc_num_per_type, seL4_Word svc_data_list[])
{
    switch(svc_type) {
    // these OS services are what we support for now, but can extend later
    // possibly more than eight types of OS services?
    // but if an application requires more OS service types than 8,
    // probably it is a sign that the application is too heavy to be put in a dynamic PD
    case FS_IFACE:
    case TIMER_IFACE:
    case SERIAL_IFACE: {
        for (uint8_t i = 0; i < svc_num_per_type; ++i) {
            seL4_Word svc_data = svc_data_list[i];
            req->data_per_svc_instance[svc_type][i] = svc_data;
        }
        break;
    }
    default:
        TSLDR_DBG_PRINT("Unsupported SVC type: %d\n", svc_type);
        break;
    };
}


void monitor_ossvc_parse_req_from_elf_section(void *elf_base, void *sh, protocon_svc_req_t *req)
{
    // parse the interface section ...
    // i.e., get the user-defined section for declaring what OS services are requested
    protocon_svc_desc_t *ib = (protocon_svc_desc_t *)(elf_base + (uint64_t)((Elf64_Shdr *)sh)->sh_offset);

    // the list of numbers of requested OS services
    const uint8_t *svc_req_num_per_type = &ib->t1_num;
    // the corresponding types which map to the above list of numbers
    const protocon_svc_type_t *svc_req_types = &ib->type1;

    seL4_Word (*svc_per_type_data_map[PC_SVC_TYPE_MAX_NUM])[PC_SVC_PER_PD_MAX_NUM] = {
        &ib->t1_iface, &ib->t2_iface, &ib->t3_iface, &ib->t4_iface,
        &ib->t5_iface, &ib->t6_iface, &ib->t7_iface, &ib->t8_iface
    };

    for (int i = 0; i < PC_SVC_TYPE_MAX_NUM; ++i) {
        if (svc_req_num_per_type[i] == 0) { // pass...
            continue;
        }
        protocon_svc_type_t curr_type = svc_req_types[i];

        // fetch the number of svc requested...
        uint8_t n = svc_req_num_per_type[i] > PC_SVC_PER_PD_MAX_NUM ? PC_SVC_PER_PD_MAX_NUM : svc_req_num_per_type[i];
        req->num_svc_per_type[curr_type] = n;

        // for each of the OS service, turns it into an OS service request,
        // which will then go into requests of low-level access rights
        seL4_Word *svc_data_list = *svc_per_type_data_map[i];
        monitor_ossvc_init_req_per_type(req, curr_type, n, svc_data_list);
    }
}


int monitor_match_ossvc_request_with_available_pd(void *elf_base, void *sh, protocon_svc_req_t *req, protocon_lifecycle_state_t *protocon_states)
{
    monitor_ossvc_parse_req_from_elf_section(elf_base, sh, req);

    return monitor_match_ossvc_request__worker_func(req, protocon_states);
}

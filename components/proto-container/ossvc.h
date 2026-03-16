#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

// maximum per monitor client container number
#define MAX_PERM_CL_NUM 16
// maximum kinds of connection (acgroup kinds) each container has
#define MAX_PERC_AK_NUM 8
// maximum number of one connection in a container...
#define MAX_PERK_NUM    8

#define SVC_TYPE_MAX_NUM (8)

#define SVC_PER_TYPE_MAX_NUM (8)

typedef struct {
    seL4_Word vaddr;
    seL4_Word number_of_pages;
    seL4_Word page_size;
} StrippedMapping;


typedef struct {
    // whether or not a valid acg
    bool svc_init;
    // corresponding to the XML gid
    uint8_t svc_idx;
    // the type of this acg
    uint8_t svc_type;
    // channels
    uint8_t channels[4];
    // irqs
    uint8_t irqs[4];
    // mappings
    StrippedMapping mappings[4];
    // data_path
    char data_path[64];
} protocon_svc_t;

typedef struct {
    // specify which PD this array belongs to
    uint8_t pd_idx;
    // number of available acgrp in the array
    uint8_t svc_num;
    // array of acgroups
    protocon_svc_t array[16];
} protocon_svcdb_t;

typedef struct {
    // overall length of this region
    size_t len;
    // list of acgrp arrays
    protocon_svcdb_t list[16];
} monitor_svcdb_t;

typedef enum {
    
    PROTOCON_ACTIVE = 1,
    PROTOCON_PASSIVE,

} protocon_lifecycle_state_t;


typedef struct {

    int num_svc_per_type[SVC_TYPE_MAX_NUM];

    // for each type of ossvc, there are at most SVC_PER_TYPE_MAX_NUM instances
    // for each instance, uintptr_t is the data word for them...

    seL4_Word data_per_svc_instance[SVC_TYPE_MAX_NUM][SVC_PER_TYPE_MAX_NUM];

} protocon_svc_req_t;


void monitor_init_ossvc_map();


typedef void (*patch_elf_connection_fn)(void *elf_base, char data_file[], uintptr_t vaddr);

void monitor_patch_payload_with_ossvc_info(int cid, protocon_svc_req_t *req, uintptr_t payload_base, uintptr_t monitor_svcdb_base, patch_elf_connection_fn fn);


int monitor_match_ossvc_request_with_available_pd(void *elf_base, void *sh, protocon_svc_req_t *req, protocon_lifecycle_state_t *protocon_states);


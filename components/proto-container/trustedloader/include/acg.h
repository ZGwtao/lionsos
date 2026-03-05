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

typedef struct {
    seL4_Word vaddr;
    seL4_Word number_of_pages;
    seL4_Word page_size;
} StrippedMapping;


typedef struct {
    // whether or not a valid acg
    bool grp_init;
    // corresponding to the XML gid
    uint8_t grp_idx;
    // the type of this acg
    uint8_t grp_type;
    // channels
    uint8_t channels[4];
    // irqs
    uint8_t irqs[4];
    // mappings
    StrippedMapping mappings[4];
    // data_path
    char data_path[64];
} acgrp_t;

typedef struct {
    // specify which PD this array belongs to
    uint8_t pd_idx;
    // number of available acgrp in the array
    uint8_t grp_num;
    // array of acgroups
    acgrp_t array[16];
} acgrp_array_t;

typedef struct {
    // overall length of this region
    size_t len;
    // list of acgrp arrays
    acgrp_array_t list[16];
} acgrp_arr_list_t;


// we use this to parse non-revokable capabilities
typedef struct {
    // number of available entries...
    size_t len;
    // ...
} access_rights_table_t;


void init_acg_state_map(void);

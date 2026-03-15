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



typedef struct {
    // records how many connection a PD can have under a type
    int acg_per_type_num[MAX_PERC_AK_NUM];
    //
    uintptr_t acg_attr[MAX_PERC_AK_NUM][MAX_PERK_NUM];
} acg_req_t;


void init_acg_state_map(void);


typedef void (*patch_elf_connection_fn)(void *elf_base, char data_file[], uintptr_t vaddr);

void funq(int cid, acg_req_t *req, uintptr_t payload_base, patch_elf_connection_fn fn);


// maximum 8 interface per OS service type
#define PC_MAX_IFACE_NUM    8
// maximum 8 interface type
#define PC_MAX_IFACE_TYPE   8

typedef enum {
    FS_IFACE = 0,
    SERIAL_IFACE,
    NETWORK_IFACE,
    TIMER_IFACE,
    I2C_IFACE,
    RESERVED, /* could be more than this... */
    UNUSED,
} pc_svc_iface_t;

typedef struct {
    uint8_t t1_num;
    uint8_t t2_num;
    uint8_t t3_num;
    uint8_t t4_num;
    uint8_t t5_num; /* reserved */
    uint8_t t6_num; /* reserved */
    uint8_t t7_num; /* reserved */
    uint8_t t8_num; /* reserved */
/* type hex? */
    pc_svc_iface_t type1;
    pc_svc_iface_t type2;
    pc_svc_iface_t type3;
    pc_svc_iface_t type4;
    pc_svc_iface_t type5;
    pc_svc_iface_t type6;
    pc_svc_iface_t type7;
    pc_svc_iface_t type8;
/* ptrs */
    uintptr_t t1_iface[PC_MAX_IFACE_NUM];
    uintptr_t t2_iface[PC_MAX_IFACE_NUM];
    uintptr_t t3_iface[PC_MAX_IFACE_NUM];
    uintptr_t t4_iface[PC_MAX_IFACE_NUM];
    uintptr_t t5_iface[PC_MAX_IFACE_NUM];
    uintptr_t t6_iface[PC_MAX_IFACE_NUM];
    uintptr_t t7_iface[PC_MAX_IFACE_NUM];
    uintptr_t t8_iface[PC_MAX_IFACE_NUM];

} template_pd_iface_t; /* template PD interface */

#define IFACE_SECTION_NAME   ".template_pd_iface"



int fetch_iface_section_info(void *elf_base, void *sh, acg_req_t *req);

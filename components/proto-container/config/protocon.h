
#pragma once

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
} protocon_svc_type_t;


#define PC_CHILD_PER_MONITOR_MAX_NUM (16)

#define PC_SVC_DESC_SECTION_NAME ".pc_svc_desc"


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
    protocon_svc_type_t type1;
    protocon_svc_type_t type2;
    protocon_svc_type_t type3;
    protocon_svc_type_t type4;
    protocon_svc_type_t type5;
    protocon_svc_type_t type6;
    protocon_svc_type_t type7;
    protocon_svc_type_t type8;
/* ptrs */
    uintptr_t t1_iface[PC_MAX_IFACE_NUM];
    uintptr_t t2_iface[PC_MAX_IFACE_NUM];
    uintptr_t t3_iface[PC_MAX_IFACE_NUM];
    uintptr_t t4_iface[PC_MAX_IFACE_NUM];
    uintptr_t t5_iface[PC_MAX_IFACE_NUM];
    uintptr_t t6_iface[PC_MAX_IFACE_NUM];
    uintptr_t t7_iface[PC_MAX_IFACE_NUM];
    uintptr_t t8_iface[PC_MAX_IFACE_NUM];

} protocon_svc_desc_t; /* template PD interface */


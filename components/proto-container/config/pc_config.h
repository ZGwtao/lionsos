/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// The number of worker thread, if changes are needed, initialisation of the thread pool must be changes as well
#define PC_WORKER_THREAD_NUM 2

#define PC_THREAD_NUM (PC_WORKER_THREAD_NUM + 1)

#define PC_WORKER_THREAD_STACKSIZE 0x40000

// maximum 8 interface per OS service type
#define PC_MAX_IFACE_NUM    8
// maximum 8 interface type
#define PC_MAX_IFACE_TYPE   8

typedef enum {
    FS_IFACE,
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


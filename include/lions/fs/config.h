/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

#define LIONS_FS_MAX_CLIENTS 128

#define LIONS_FS_MAGIC_LEN 8

typedef struct fs_connection_resource {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    /* the shared region is for pathname transferring */
    region_resource_t share;
    uint16_t queue_len;
    uint8_t id;
} fs_connection_resource_t;

typedef struct fs_server_config {
    char magic[LIONS_FS_MAGIC_LEN];
    /* the only client is the virtualiser */
    fs_connection_resource_t virt;
} fs_server_config_t;

typedef struct fs_client_config {
    char magic[LIONS_FS_MAGIC_LEN];
    /* instead of server, connect to virtualiser for request dispatching and load-balancing */
    fs_connection_resource_t virt;
    /*
     * the connection channel is for command routing/dispatching, while the shared data
     * is for data I/O, which is needed by the block(storage driver)
     *
     * no need for a data region, because the client connects to the blk driver
     * via shared-mem-based data region directly
     */
} fs_client_config_t;

typedef struct fs_virt_config_client {
    fs_connection_resource_t conn;
    uint32_t cid;
} fs_virt_client_config_t;

typedef struct fs_virt_config_server {
    fs_connection_resource_t conn;
} fs_virt_server_config_t;

/* copy-paste from the block virtualiser */
typedef struct fs_virt_config {
    uint64_t num_clients;
    char magic[LIONS_FS_MAGIC_LEN];
    fs_virt_server_config_t server;
    fs_virt_client_config_t clients[LIONS_FS_MAX_CLIENTS];
} fs_virt_config_t;

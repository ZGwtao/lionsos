/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

#define LIONS_FS_MAGIC_LEN 8
static uint8_t LIONS_FS_MAGIC[LIONS_FS_MAGIC_LEN] = { 0x4C, 0x69, 0x6F, 0x6E, 0x73, 0x4F, 0x53, 0x01 };

typedef struct fs_connection_resource {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    region_resource_t pathname_share;
    region_resource_t share;
    uint16_t queue_len;
    uint8_t id;
} fs_connection_resource_t;

typedef struct fs_server_config {
    uint8_t magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t client;
} fs_server_config_t;

typedef struct fs_client_config {
    uint8_t magic[LIONS_FS_MAGIC_LEN];
    fs_connection_resource_t server;
} fs_client_config_t;

static bool fs_config_check_magic(void *config)
{
    uint8_t *magic = (uint8_t *)config;
    for (int i = 0; i < LIONS_FS_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FS_MAGIC[i]) {
            return false;
        }
    }

    return true;
}

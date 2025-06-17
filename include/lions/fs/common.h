/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <lions/fs/config.h>

static char LIONS_FS_MAGIC[LIONS_FS_MAGIC_LEN] = { 'L', 'i', 'o', 'n', 's', 'O', 'S', 0x1 };

static bool fs_config_check_magic(void *config)
{
    char *magic = (char *)config;
    for (int i = 0; i < LIONS_FS_MAGIC_LEN; i++) {
        if (magic[i] != LIONS_FS_MAGIC[i]) {
            return false;
        }
    }
    return true;
}
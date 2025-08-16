/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/fs/protocol.h>
#include <sddf/serial/queue.h>
#include <microkit.h>

#define FS_BUFFER_SIZE      0x8000
#define FS_PARTITION_NUM    1

/* partition ID (max 256) */
typedef uint8_t part_id_t;

/* signalling channels for fs instances */
typedef struct fs_signal_rt {
    fs_queue_t *fs_command_queue;
    fs_queue_t *fs_completion_queue;
    microkit_channel fs_server_id;
    char *fs_pathname_share;
    char *fs_share;
} fs_signal_rt_t;

int fs_request_allocate(uint64_t *request_id);
void fs_request_free(uint64_t request_id);
void fs_request_flag_set(uint64_t request_id);

int fs_buffer_allocate(ptrdiff_t *buffer);
void fs_buffer_free(ptrdiff_t buffer);
void *fs_buffer_ptr(ptrdiff_t buffer);

int fs_pbuf_allocate(ptrdiff_t *buffer);
void fs_pbuf_free(ptrdiff_t buffer);
void *fs_pbuf_ptr(ptrdiff_t buffer);

void fs_process_completions(void);

void fs_command_issue(fs_cmd_t cmd);
void fs_command_complete(uint64_t request_id, fs_cmd_t *cmd, fs_cmpl_t *cmpl);
int fs_command_blocking(fs_cmpl_t *cmpl, fs_cmd_t cmd);

bool fs_sanitize_pathname_wrap(const char fname[], char **pfname_plocal);
int fs_sanitize_pathname_pair_wrap(const char f1[], const char f2[], char **pf1_plocal, char **pf2_plocal);
void fs_sanitize_pathname(const char path[], size_t len, char *path_plocal[], part_id_t *wp, bool *valid);

void fs_switch_partition(uint8_t part_id);
uint8_t fs_retrieve_partition();
/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <stdint.h>
#include <lions/fs/protocol.h>

#define FS_BUFFER_SIZE      0x8000
#define REQUEST_ID_MAXIMUM  (FS_QUEUE_CAPACITY - 1)
#define NUM_BUFFERS         FS_QUEUE_CAPACITY * 4

struct request_metadata {
    fs_cmd_t command;
    fs_cmpl_t completion;
    bool used;
    bool complete;
} request_metadata[FS_QUEUE_CAPACITY];

struct buffer_metadata {
    bool used;
} buffer_metadata[FS_QUEUE_CAPACITY];

int fs_request_allocate(uint64_t *request_id);
void fs_request_free(uint64_t request_id);
void fs_request_flag_set(uint64_t request_id);

int fs_buffer_allocate(ptrdiff_t *buffer);
void fs_buffer_free(ptrdiff_t buffer);
void *fs_buffer_ptr(ptrdiff_t buffer);

void fs_process_completions(void);

void fs_command_issue(fs_cmd_t cmd);
void fs_command_complete(uint64_t request_id, fs_cmd_t *cmd, fs_cmpl_t *cmpl);
int fs_command_blocking(fs_cmpl_t *cmpl, fs_cmd_t cmd);

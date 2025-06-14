/*
 * Copyright 2024, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <microkit.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include "micropython.h"
#include "fs_helpers.h"

extern fs_signal_rt_t *curr_fs_chann;
extern fs_signal_rt_t fs_chann_table[];

void fs_switch_partition(uint8_t part_id) {
    curr_fs_chann = &fs_chann_table[part_id - 1];
}

uint8_t fs_retrieve_partition() {
    return (curr_fs_chann - fs_chann_table) + 1;
}

#define REQUEST_ID_MAXIMUM (FS_QUEUE_CAPACITY - 1)
struct request_metadata {
    fs_cmd_t command;
    fs_cmpl_t completion;
    bool used;
    bool complete;
} request_metadata[FS_QUEUE_CAPACITY];

#define NUM_BUFFERS FS_QUEUE_CAPACITY * 4
struct buffer_metadata {
    bool used;
} buffer_metadata[FS_QUEUE_CAPACITY];

int fs_request_allocate(uint64_t *request_id) {
    for (uint64_t i = 0; i < NUM_BUFFERS; i++) {
        if (!request_metadata[i].used) {
            request_metadata[i].used = true;
            *request_id = i;
            return 0;
        }
    }
    return 1;
}

void fs_request_free(uint64_t request_id) {
    assert(request_id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[request_id].used);
    request_metadata[request_id].used = false;
    request_metadata[request_id].complete = false;
}

int fs_buffer_allocate(ptrdiff_t *buffer) {
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (!buffer_metadata[i].used) {
            buffer_metadata[i].used = true;
            *buffer = i * FS_BUFFER_SIZE;
            return 0;
        }
    }
    return 1;
}

void fs_buffer_free(ptrdiff_t buffer) {
    uint64_t i = buffer / FS_BUFFER_SIZE;
    assert(i < NUM_BUFFERS);
    assert(buffer_metadata[i].used);
    buffer_metadata[i].used = false;
}

void *fs_buffer_ptr(ptrdiff_t buffer) {
    return curr_fs_chann->fs_share + buffer;
}

void fs_process_completions(void) {
    fs_msg_t message;
    uint64_t to_consume = fs_queue_length_consumer(curr_fs_chann->fs_completion_queue);
    for (uint64_t i = 0; i < to_consume; i++) {
        fs_cmpl_t completion = fs_queue_idx_filled(curr_fs_chann->fs_completion_queue, i)->cmpl;

        if (completion.id > REQUEST_ID_MAXIMUM) {
            printf("received bad fs completion: invalid request id: %lu\n", completion.id);
            continue;
        }

        request_metadata[completion.id].completion = completion;
        request_metadata[completion.id].complete = true;
        fs_request_flag_set(completion.id);
    }
    fs_queue_publish_consumption(curr_fs_chann->fs_completion_queue, to_consume);
}

void fs_command_issue(fs_cmd_t cmd) {
    assert(cmd.id <= REQUEST_ID_MAXIMUM);
    assert(request_metadata[cmd.id].used);

    fs_msg_t message = { .cmd = cmd };
    assert(fs_queue_length_producer(curr_fs_chann->fs_command_queue) != FS_QUEUE_CAPACITY);
    *fs_queue_idx_empty(curr_fs_chann->fs_command_queue, 0) = message;
    fs_queue_publish_production(curr_fs_chann->fs_command_queue, 1);
    microkit_notify(curr_fs_chann->fs_server_id);
    request_metadata[cmd.id].command = cmd;
}

void fs_command_complete(uint64_t request_id, fs_cmd_t *command, fs_cmpl_t *completion) {
    assert(request_metadata[request_id].complete);
    if (command != NULL) {
        *command = request_metadata[request_id].command;
    }
    if (completion != NULL) {
        *completion = request_metadata[request_id].completion;
    }
}

int fs_command_blocking(fs_cmpl_t *completion, fs_cmd_t cmd) {
    uint64_t request_id;
    int err = fs_request_allocate(&request_id);
    if (err) {
        return -1;
    }
    cmd.id = request_id;

    fs_command_issue(cmd);
    while (!request_metadata[request_id].complete) {
        microkit_cothread_wait_on_channel(curr_fs_chann->fs_server_id);
    }

    fs_command_complete(request_id, NULL, completion);
    fs_request_free(request_id);
    return 0;
}

/*
 * Take abs path, determine if there is a partition id in the pathname.
 * If there is a partition ID, return the partition ID to (*wp), and set
 * valid to be TRUE, otherwise set valid to FALSE
 *
 * Valid partition number should be 1 to 255?
 */
void fs_sanitize_pathname(const char path[], size_t len, char *path_plocal[], part_id_t *wp, bool *valid)
{
    /* the start of partition-local path name */
    char *path_local_off;
    /* partition ID string */
    char part_id[10] = {0};
    size_t len_pre;
    size_t len_new;
    int _p_id;

    path_local_off = (char *)path;
    len_pre = 0;

    if (valid == NULL || wp == NULL || path_plocal == NULL) {
        /* internal vfs error */
        assert(0);
    }
    *valid = false;

    /* No partition ID is given */
    if (path[0] == ':') {
        return;
    }
    /* ignore heading spaces */
    for (size_t i = 0; i < len; ++i) {
        if (*(path_local_off) != ' ') {
            break;
        }
        path_local_off++;
    }
    len_new = strlen(path_local_off);

    /* ignore heading zeros */
    for (size_t i = 0; i < len_new; ++i) {
        if (*(path_local_off) != '0') {
            break;
        }
        path_local_off++;
    }
    len_new = strlen(path_local_off);

    /* at most the first 8 character to avoid */
    for (; len_pre < len_new && len_pre < 8; ++len_pre) {
        if (*(path_local_off + len_pre) == ':') {
            /* copy the partition ID */
            strncpy(part_id, path_local_off, len_pre);
            /* add tailing character */
            part_id[len_pre] = '\n';
            /* assume the partition ID is valid */
            *valid = true;
            break;
        }
    }
    if (*valid == false) {
        /* still not found */
        return;
    }
#if 0
    /* debug */
    printf("partition ID retrieved: %s", part_id);
#endif
    for (int i = 0; i < 8; ++i) {
        if (part_id[i] == '\n' && i > 0) {
            break;
        }
        if (part_id[i] > '9' || part_id[i] < '0') {
            /* number invalid */
            *valid = false;
            return;
        }
    }
    /* assign possible partition ID */
    _p_id = atoi(part_id);
#if 0
    printf("retrieved ID: %d\n", _p_id);
#endif
    if (_p_id >= 256 || _p_id < 1) {
        /* still invalid (1~255) */
        *valid = false;
        return;
    }
    /* assign valid partition ID */
    *wp = (part_id_t)_p_id;
    *path_plocal = (path_local_off + len_pre + 1);
}
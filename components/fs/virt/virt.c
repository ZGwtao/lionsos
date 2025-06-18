/*
 * Copyright 2025, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <microkit.h>
#include <libmicrokitco.h>
#include <sddf/blk/queue.h>
#include <sddf/blk/storage_info.h>
#include <sddf/blk/config.h>
#include <lions/fs/protocol.h>
#include <lions/fs/common.h>
#include "decl.h"
#include "ff.h"
#include "diskio.h"

__attribute__((__section__(".fs_virt_config"))) fs_virt_config_t fs_config;

fs_signal_rt_t fs_server_chann;
fs_signal_rt_t fs_client_chann_list[LIONS_FS_MAX_CLIENTS];

/* 
 * If a request is going to be consumed by a client[i], increase client[i] by 1. 
 * At the end of the loop, populate the request for all client whose client[i] > 0
 */
uint64_t client_response[LIONS_FS_MAX_CLIENTS];

/*
 * Receive request from the file system server, which means there should be a cmd
 * to respond. So the operation of handling server is checking the response queue,
 */
static void handle_server()
{
    fs_msg_t *msg_recv, *msg_send;
    fs_cmpl_t *cmpl;
    fs_queue_t *response_queue;
    fs_queue_t *client_queue;

    response_queue = fs_server_chann.fs_completion_queue;
    uint64_t to_consume = fs_queue_length_consumer(response_queue);

    int err;
    for (uint64_t i = 0; i < to_consume; ++i) {
        msg_recv = fs_queue_idx_filled(response_queue, i);

        /* what the file system transfers is a fs_msg_t */
        cmpl = msg_recv->cmpl;

        uint32_t cid;
        err = fs_cmpl_get_cid(cmpl, &cid);
        if (err) {
            /* internal FS error */
            assert(0);
        }
        /* Make sure the id is restored for client's bookkeeping */
        fs_cmpl_unset_cid(cmpl);
        if (err) {
            assert(0);
        }

        /* paste the cmpl to the client's completition buffer */
        client_queue = fs_client_chann_list[cid].fs_completion_queue;
        msg_send = fs_queue_idx_empty(client_queue, client_response[cid]++);
        /* not ptr, can just transfer via value */
        msg_send->cmpl = msg_recv->cmpl;
    }
    /* handled all responses from the server -> sent to clients to consume */
    fs_queue_publish_consumption(response_queue, to_consume);

    for (uint64_t i = 0; i < LIONS_FS_MAX_CLIENTS; ++i) {
        /* number of moves the tail of a queue has to make */
        if (client_response[i] == 0) {
            continue;
        }
        client_queue = fs_client_chann_list[i].fs_completion_queue;
        fs_queue_publish_production(client_queue, client_response[i])
    }
}

static bool handle_clients()
{
    ;
}

static inline void signal_server() {
    /* signal the server if and only if there are requests ready */
    microkit_notify(fs_server_chann.fs_signal_id);
}

static void signal_clients() {
    /* signal clients who should receive response */
    for (uint64_t i = 0; i < LIONS_FS_MAX_CLIENTS; ++i) {
        if (client_response[i] > 0) {
            microkit_notify(fs_client_chann_list[i].fs_signal_id);
        }
    }
}

void notified(microkit_channel ch) {
    for (uint64_t i = 0; i < LIONS_FS_MAX_CLIENTS; ++i) {
        client_response[i] = 0;
    }
#if 1
    LOG_FATFS("Notification received on channel:: %d\n", ch);
#endif
    if (ch == fs_server_chann.fs_signal_id) {
        handle_server();
    }
    /* find which client has request that ready to be handled */
    if (handle_clients()) {
        /* if there are requests from the clients to publish */
        signal_server();
    }
    /* if there are response for clients to consume */
    signal_clients();
}

void init(void)
{
    assert(fs_config_check_magic(&fs_config));

    /* initialise server connection */
    fs_server_chann.fs_command_queue    = fs_config.server.conn.command_queue.vaddr;
    fs_server_chann.fs_completion_queue = fs_config.server.conn.completion_queue.vaddr;
    fs_server_chann.fs_share            = fs_config.server.conn.share.vaddr;
    fs_server_chann.fs_signal_id        = fs_config.server.conn.id;

    /* initialise clients connection */
    for (uint64_t i = 0; i < fs_config.num_clients; ++i) {
        fs_client_chann_list[i].fs_command_queue    = fs.config.clients[i].conn.command_queue.vaddr;
        fs_client_chann_list[i].fs_completion_queue = fs.config.clients[i].conn.fs_completion_queue.vaddr;
        fs_client_chann_list[i].fs_share            = fs.config.clients[i].conn.fs_share.vaddr;
        fs_client_chann_list[i].fs_signal_id        = fs.config.clients[i].conn.fs_signal_id.vaddr;
    }
}
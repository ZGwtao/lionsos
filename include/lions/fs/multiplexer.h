
#pragma once

#include <lions/fs/protocol.h>

typedef struct {
    char *pathname_share;
    char *share;
} cd_region_t;

typedef struct {
    uint32_t id;
    uint32_t client;
    uint64_t type;
    fs_cmd_params_t params;
} mul_cmd_t;

typedef struct {
    uint32_t id;
    uint32_t client;
    uint64_t status;
    fs_cmpl_data_t data;
} mul_cmpl_t;

typedef union {
    mul_cmd_t cmd;
    mul_cmpl_t cmpl;
} mul_msg_t;

typedef struct {
    uint64_t head;
    uint64_t tail;
    uint8_t padding[48];
    mul_msg_t buffer[FS_QUEUE_CAPACITY];
} mul_queue_t;

static inline uint64_t mul_queue_length_consumer(mul_queue_t *queue) {
    return __atomic_load_n(&queue->tail, __ATOMIC_ACQUIRE) - queue->head;
}

static inline uint64_t mul_queue_length_producer(mul_queue_t *queue) {
    return queue->tail - __atomic_load_n(&queue->head, __ATOMIC_ACQUIRE);
}

static inline mul_msg_t *mul_queue_idx_filled(mul_queue_t *queue, uint64_t index) {
    index = queue->head + index;
    return &queue->buffer[index % FS_QUEUE_CAPACITY];
}

static inline mul_msg_t *mul_queue_idx_empty(mul_queue_t *queue, uint64_t index) {
    index = queue->tail + index;
    return &queue->buffer[index % FS_QUEUE_CAPACITY];
}

static inline void mul_queue_publish_consumption(mul_queue_t *queue, uint64_t amount_consumed) {
    __atomic_store_n(&queue->head, queue->head + amount_consumed, __ATOMIC_RELEASE);
}

static inline void mul_queue_publish_production(mul_queue_t *queue, uint64_t amount_produced) {
    __atomic_store_n(&queue->tail, queue->tail + amount_produced, __ATOMIC_RELEASE);
}

/* worker funtion for forwarding responses */
void msg_mul2fs_cmpl(mul_msg_t *src, fs_msg_t *tar)
{
    tar->cmpl.status = src->cmpl.status;
    tar->cmpl.data = src->cmpl.data;
    tar->cmpl.id = (uint64_t)src->cmpl.id;
}

/* worker function for forwarding requests */
void msg_fs2mul_cmd(fs_msg_t *src, mul_msg_t *tar, uint32_t cid)
{
    tar->cmd.id = (uint32_t)src->cmd.id;
    tar->cmd.params = src->cmd.params;
    tar->cmd.type = src->cmd.type;
    tar->cmd.client = cid;
}


#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PD_IO_QUEUE_OK          0
#define PD_IO_QUEUE_EMPTY      -1
#define PD_IO_QUEUE_FULL       -2
#define PD_IO_QUEUE_TOO_LARGE  -3
#define PD_IO_QUEUE_BAD_DESC   -4

typedef struct pd_io_buffer_desc {
    uint64_t offset;
    uint32_t len;
    uint32_t reserved;
} pd_io_buffer_desc_t;

typedef struct pd_io_queue {
    uint32_t head;
    uint32_t tail;
    uint32_t reserved0;
    uint32_t reserved1;
    pd_io_buffer_desc_t buffers[];
} pd_io_queue_t;

typedef struct pd_io_direction {
    pd_io_queue_t *free;
    pd_io_queue_t *active;
    uint8_t *data;
    size_t data_size;
    uint32_t capacity;
    uint32_t buffer_size;
} pd_io_direction_t;

typedef struct pd_io_link {
    /* Messages received by this endpoint. */
    pd_io_direction_t rx;
    /* Messages transmitted by this endpoint. */
    pd_io_direction_t tx;
} pd_io_link_t;

_Static_assert(sizeof(pd_io_buffer_desc_t) == 16,
               "pd_io_buffer_desc_t layout changed");
_Static_assert(sizeof(pd_io_queue_t) == 16,
               "pd_io_queue_t header layout changed");

static inline size_t pd_io_queue_bytes(uint32_t capacity)
{
    return sizeof(pd_io_queue_t) +
           (size_t)capacity * sizeof(pd_io_buffer_desc_t);
}

static inline uint32_t pd_io_load_relaxed(const uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}

static inline uint32_t pd_io_load_acquire(const uint32_t *p)
{
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static inline void pd_io_store_relaxed(uint32_t *p, uint32_t value)
{
    __atomic_store_n(p, value, __ATOMIC_RELAXED);
}

static inline void pd_io_store_release(uint32_t *p, uint32_t value)
{
    __atomic_store_n(p, value, __ATOMIC_RELEASE);
}

static inline void pd_io_queue_reset(pd_io_queue_t *queue)
{
    pd_io_store_relaxed(&queue->head, 0);
    pd_io_store_relaxed(&queue->tail, 0);
    pd_io_store_relaxed(&queue->reserved0, 0);
    pd_io_store_release(&queue->reserved1, 0);
}

static inline uint32_t pd_io_queue_length(const pd_io_queue_t *queue)
{
    uint32_t head = pd_io_load_acquire(&queue->head);
    uint32_t tail = pd_io_load_acquire(&queue->tail);
    return tail - head;
}

static inline bool pd_io_queue_empty(const pd_io_queue_t *queue)
{
    return pd_io_queue_length(queue) == 0;
}

static inline bool pd_io_queue_full(const pd_io_queue_t *queue,
                                    uint32_t capacity)
{
    return pd_io_queue_length(queue) >= capacity;
}

/*
 * Called only by the queue's producer.
 */
static inline int pd_io_queue_enqueue(pd_io_queue_t *queue,
                                      uint32_t capacity,
                                      pd_io_buffer_desc_t desc)
{
    uint32_t tail = pd_io_load_relaxed(&queue->tail);
    uint32_t head = pd_io_load_acquire(&queue->head);

    if ((tail - head) >= capacity) {
        return PD_IO_QUEUE_FULL;
    }

    queue->buffers[tail % capacity] = desc;
    pd_io_store_release(&queue->tail, tail + 1);
    return PD_IO_QUEUE_OK;
}

/*
 * Called only by the queue's consumer.
 */
static inline int pd_io_queue_dequeue(pd_io_queue_t *queue,
                                      uint32_t capacity,
                                      pd_io_buffer_desc_t *desc)
{
    uint32_t head = pd_io_load_relaxed(&queue->head);
    uint32_t tail = pd_io_load_acquire(&queue->tail);

    if (head == tail) {
        return PD_IO_QUEUE_EMPTY;
    }

    *desc = queue->buffers[head % capacity];
    pd_io_store_release(&queue->head, head + 1);
    return PD_IO_QUEUE_OK;
}

static inline void pd_io_direction_init(pd_io_direction_t *direction,
                                        pd_io_queue_t *free_queue,
                                        pd_io_queue_t *active_queue,
                                        void *data,
                                        size_t data_size,
                                        uint32_t capacity,
                                        uint32_t buffer_size)
{
    direction->free = free_queue;
    direction->active = active_queue;
    direction->data = data;
    direction->data_size = data_size;
    direction->capacity = capacity;
    direction->buffer_size = buffer_size;
}

/*
 * Must be called exactly once, before either endpoint starts using the link.
 * In this design the monitor is the shared-state initialisation owner.
 */
static inline int pd_io_direction_reset_and_fill(pd_io_direction_t *direction)
{
    if (direction->capacity == 0 ||
        direction->buffer_size == 0 ||
        (size_t)direction->capacity * direction->buffer_size >
            direction->data_size) {
        return PD_IO_QUEUE_BAD_DESC;
    }

    pd_io_queue_reset(direction->free);
    pd_io_queue_reset(direction->active);

    for (uint32_t i = 0; i < direction->capacity; i++) {
        pd_io_buffer_desc_t desc = {
            .offset = (uint64_t)i * direction->buffer_size,
            .len = 0,
            .reserved = 0,
        };

        int err = pd_io_queue_enqueue(direction->free,
                                    direction->capacity,
                                    desc);
        if (err != PD_IO_QUEUE_OK) {
            return err;
        }
    }

    return PD_IO_QUEUE_OK;
}

static inline bool pd_io_desc_valid(const pd_io_direction_t *direction,
                                    const pd_io_buffer_desc_t *desc)
{
    if (desc->len > direction->buffer_size) {
        return false;
    }

    if (desc->offset > direction->data_size) {
        return false;
    }

    return desc->len <= direction->data_size - (size_t)desc->offset;
}

/*
 * Obtain a free buffer, copy payload into it, and publish it on active.
 * The caller should notify the peer after PD_IO_QUEUE_OK.
 */
static inline int pd_io_direction_send(pd_io_direction_t *direction,
                                       const void *payload,
                                       uint32_t payload_len)
{
    if (payload_len > direction->buffer_size) {
        return PD_IO_QUEUE_TOO_LARGE;
    }

    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->free,
                                direction->capacity,
                                &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    desc.len = payload_len;
    if (!pd_io_desc_valid(direction, &desc)) {
        desc.len = 0;
        (void)pd_io_queue_enqueue(direction->free,
                                direction->capacity,
                                desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    memcpy(direction->data + desc.offset, payload, payload_len);

    err = pd_io_queue_enqueue(direction->active,
                            direction->capacity,
                            desc);
    if (err != PD_IO_QUEUE_OK) {
        desc.len = 0;
        (void)pd_io_queue_enqueue(direction->free,
                                direction->capacity,
                                desc);
        return err;
    }

    return PD_IO_QUEUE_OK;
}

/*
 * Consume one active message, copy it out, then recycle its descriptor.
 */
static inline int pd_io_direction_receive(pd_io_direction_t *direction,
                                          void *payload_out,
                                          uint32_t payload_capacity,
                                          uint32_t *payload_len_out)
{
    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->active,
                                direction->capacity,
                                &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    if (!pd_io_desc_valid(direction, &desc)) {
        desc.len = 0;
        (void)pd_io_queue_enqueue(direction->free,
                                direction->capacity,
                                desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (desc.len > payload_capacity) {
        desc.len = 0;
        (void)pd_io_queue_enqueue(direction->free,
                                direction->capacity,
                                desc);
        return PD_IO_QUEUE_TOO_LARGE;
    }

    memcpy(payload_out, direction->data + desc.offset, desc.len);
    *payload_len_out = desc.len;

    desc.len = 0;
    err = pd_io_queue_enqueue(direction->free,
                            direction->capacity,
                            desc);
    return err;
}

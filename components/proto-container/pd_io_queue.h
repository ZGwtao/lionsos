
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
#define PD_IO_QUEUE_BAD_HEADER -5

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

typedef struct {
    uint8_t source;
    uint8_t bitmap_targets;
    uint16_t payload_size;
} pd_io_header_t;

_Static_assert(sizeof(pd_io_header_t) == 4,
               "pd_io_header_t layout changed");

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
 * Recycle a descriptor back into the free queue.
 *
 * This helper assumes that the caller has already dequeued the descriptor
 * from the active queue or free queue and currently owns it.
 */
static inline int pd_io_direction_recycle(pd_io_direction_t *direction,
                                          pd_io_buffer_desc_t desc)
{
    desc.len = 0;
    desc.reserved = 0;

    return pd_io_queue_enqueue(direction->free,
                               direction->capacity,
                               desc);
}

/*
 * Obtain a free buffer, copy an unframed payload into it, and publish the
 * descriptor on the active queue.
 *
 * The caller should notify the peer after this function returns
 * PD_IO_QUEUE_OK.
 */
static inline int pd_io_direction_send_raw(pd_io_direction_t *direction,
                                           const void *payload,
                                           uint32_t payload_len)
{
    if (payload_len > direction->buffer_size) {
        return PD_IO_QUEUE_TOO_LARGE;
    }

    if (payload_len != 0 && payload == NULL) {
        return PD_IO_QUEUE_BAD_DESC;
    }

    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->free,
                                  direction->capacity,
                                  &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    desc.len = payload_len;
    desc.reserved = 0;

    if (!pd_io_desc_valid(direction, &desc)) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (payload_len != 0) {
        memcpy(direction->data + (size_t)desc.offset,
               payload,
               payload_len);
    }

    err = pd_io_queue_enqueue(direction->active,
                              direction->capacity,
                              desc);
    if (err != PD_IO_QUEUE_OK) {
        (void)pd_io_direction_recycle(direction, desc);
        return err;
    }

    return PD_IO_QUEUE_OK;
}

/*
 * Build a framed message consisting of:
 *
 *     pd_io_header_t
 *     payload bytes
 *
 * desc.len records the total framed-message length, while
 * header.payload_size records only the payload length.
 */
static inline int pd_io_direction_send(pd_io_direction_t *direction,
                                       uint8_t source,
                                       uint8_t bitmap_targets,
                                       const void *payload,
                                       uint32_t payload_len)
{
    if (payload_len > UINT16_MAX) {
        return PD_IO_QUEUE_TOO_LARGE;
    }

    if (payload_len != 0 && payload == NULL) {
        return PD_IO_QUEUE_BAD_DESC;
    }

    const uint32_t header_len = (uint32_t)sizeof(pd_io_header_t);

    if (header_len > direction->buffer_size ||
        payload_len > direction->buffer_size - header_len) {
        return PD_IO_QUEUE_TOO_LARGE;
    }

    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->free,
                                  direction->capacity,
                                  &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    desc.len = header_len + payload_len;
    desc.reserved = 0;

    if (!pd_io_desc_valid(direction, &desc)) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    pd_io_header_t header = {
        .source = source,
        .bitmap_targets = bitmap_targets,
        .payload_size = (uint16_t)payload_len,
    };

    uint8_t *buffer =
        direction->data + (size_t)desc.offset;

    /*
     * Use memcpy rather than casting buffer to pd_io_header_t *.
     * This avoids alignment and strict-aliasing assumptions.
     */
    memcpy(buffer, &header, sizeof(header));

    if (payload_len != 0) {
        memcpy(buffer + sizeof(header),
               payload,
               payload_len);
    }

    err = pd_io_queue_enqueue(direction->active,
                              direction->capacity,
                              desc);
    if (err != PD_IO_QUEUE_OK) {
        (void)pd_io_direction_recycle(direction, desc);
        return err;
    }

    return PD_IO_QUEUE_OK;
}

/*
 * Consume one unframed message, copy it out, and recycle its descriptor.
 */
static inline int pd_io_direction_receive_raw(
    pd_io_direction_t *direction,
    void *payload_out,
    uint32_t payload_capacity,
    uint32_t *payload_len_out)
{
    if (payload_len_out == NULL) {
        return PD_IO_QUEUE_BAD_DESC;
    }

    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->active,
                                  direction->capacity,
                                  &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    if (!pd_io_desc_valid(direction, &desc)) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (desc.len > payload_capacity) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_TOO_LARGE;
    }

    if (desc.len != 0 && payload_out == NULL) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (desc.len != 0) {
        memcpy(payload_out,
               direction->data + (size_t)desc.offset,
               desc.len);
    }

    *payload_len_out = desc.len;

    return pd_io_direction_recycle(direction, desc);
}

/*
 * Consume one framed message:
 *
 *     pd_io_header_t
 *     payload bytes
 *
 * The header is copied into header_out and only the payload bytes are copied
 * into payload_out.
 */
static inline int pd_io_direction_receive(
    pd_io_direction_t *direction,
    pd_io_header_t *header_out,
    void *payload_out,
    uint32_t payload_capacity,
    uint32_t *payload_len_out)
{
    if (header_out == NULL || payload_len_out == NULL) {
        return PD_IO_QUEUE_BAD_DESC;
    }

    pd_io_buffer_desc_t desc;
    int err = pd_io_queue_dequeue(direction->active,
                                  direction->capacity,
                                  &desc);
    if (err != PD_IO_QUEUE_OK) {
        return err;
    }

    if (!pd_io_desc_valid(direction, &desc)) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (desc.len < sizeof(pd_io_header_t)) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_HEADER;
    }

    const uint8_t *buffer =
        direction->data + (size_t)desc.offset;

    pd_io_header_t header;
    memcpy(&header, buffer, sizeof(header));

    uint32_t framed_payload_len =
        desc.len - (uint32_t)sizeof(pd_io_header_t);

    /*
     * The descriptor and the in-band header must agree on the payload size.
     */
    if ((uint32_t)header.payload_size != framed_payload_len) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_HEADER;
    }

    if (framed_payload_len > payload_capacity) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_TOO_LARGE;
    }

    if (framed_payload_len != 0 && payload_out == NULL) {
        (void)pd_io_direction_recycle(direction, desc);
        return PD_IO_QUEUE_BAD_DESC;
    }

    if (framed_payload_len != 0) {
        memcpy(payload_out,
               buffer + sizeof(pd_io_header_t),
               framed_payload_len);
    }

    *header_out = header;
    *payload_len_out = framed_payload_len;

    return pd_io_direction_recycle(direction, desc);
}

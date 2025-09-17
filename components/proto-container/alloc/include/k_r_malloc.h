
#pragma once

#include <stddef.h> /* For NULL */
#include <string.h> /* For memcpy */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/* A K&R malloc style allocation that can be 'put in a box' as it were. */

typedef union k_r_malloc_header {
    struct {
        union k_r_malloc_header *ptr;
        size_t size;
    } s;
    /* Force alignment */
    long long x;
} k_r_malloc_header_t;

typedef struct mspace_k_r_malloc {
    k_r_malloc_header_t base;
    k_r_malloc_header_t *freep;
    size_t cookie;
    k_r_malloc_header_t *(*morecore)(size_t cookie, struct mspace_k_r_malloc *k_r_malloc, size_t new_units);
} mspace_k_r_malloc_t;

void mspace_k_r_malloc_init(mspace_k_r_malloc_t *k_r_malloc, size_t cookie, k_r_malloc_header_t * (*morecore)(size_t cookie, mspace_k_r_malloc_t *k_r_malloc, size_t new_units));
void *mspace_k_r_malloc_alloc(mspace_k_r_malloc_t *k_r_malloc, size_t nbytes);
void mspace_k_r_malloc_free(mspace_k_r_malloc_t *k_r_malloc, void *ap);

struct mspace_fixed_pool_config {
    void *pool;
    size_t size;
};

typedef struct mspace_fixed_pool {
    uintptr_t pool_ptr;
    size_t remaining;
    mspace_k_r_malloc_t k_r_malloc;
} pool_cookie_t;

#define ALIGN_UP(x, n) (((x) + (n) - 1) & ~((n) - 1))

void mspace_fixed_pool_create(pool_cookie_t *fixed_pool, struct mspace_fixed_pool_config config);
pool_cookie_t *mspace_bootstrap_allocator(size_t pool_size, void *pool);


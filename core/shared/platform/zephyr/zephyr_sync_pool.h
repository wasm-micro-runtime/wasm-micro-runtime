/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_SYNC_POOL_H
#define WAMR_ZEPHYR_SYNC_POOL_H

#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if !defined(BH_ZEPHYR_MUTEX_POOL_COUNT)
#define BH_ZEPHYR_MUTEX_POOL_COUNT 16
#endif

#if !defined(BH_ZEPHYR_COND_POOL_COUNT)
#define BH_ZEPHYR_COND_POOL_COUNT 8
#endif

typedef struct wamr_zephyr_sync_pool {
    struct k_mutex *management_lock;
    struct k_mutex *mutexes;
    size_t mutex_count;
    struct k_condvar *condvars;
    size_t condvar_count;
} wamr_zephyr_sync_pool_t;

/*
 * Descriptors must be produced by WAMR_ZEPHYR_SYNC_POOL_DEFINE(), or otherwise
 * reference non-overlapping, statically registered Zephyr mutex and condition
 * objects of the declared types. Arbitrary forged, untracked, or wrong-type
 * pointers are outside this C interface's contract: Zephyr 3.7 provides no
 * public pre-initialization API for querying an object's registration and type.
 */

#define WAMR_ZEPHYR_SYNC_POOL_DEFINE(name, mutex_capacity, cond_capacity)    \
    BUILD_ASSERT((mutex_capacity) > 0, "WAMR mutex pool must not be empty"); \
    BUILD_ASSERT((cond_capacity) > 0,                                        \
                 "WAMR condition pool must not be empty");                   \
    BUILD_ASSERT((mutex_capacity) <= BH_ZEPHYR_MUTEX_POOL_COUNT,             \
                 "WAMR mutex pool exceeds bookkeeping capacity");            \
    BUILD_ASSERT((cond_capacity) <= BH_ZEPHYR_COND_POOL_COUNT,               \
                 "WAMR condition pool exceeds bookkeeping capacity");        \
    static struct k_mutex name##_management_lock;                            \
    static struct k_mutex name##_mutex_objects[mutex_capacity];              \
    static struct k_condvar name##_condvar_objects[cond_capacity];           \
    static const wamr_zephyr_sync_pool_t name = {                            \
        .management_lock = &name##_management_lock,                          \
        .mutexes = name##_mutex_objects,                                     \
        .mutex_count = (mutex_capacity),                                     \
        .condvars = name##_condvar_objects,                                  \
        .condvar_count = (cond_capacity),                                    \
    }

/*
 * Provision this pool once, from supervisor mode, for a suspended WAMR user
 * root. The caller must serialize initial provisioning; simultaneous first
 * calls are outside the API contract. A later sequential call is idempotent
 * only when both the descriptor and owner are identical.
 */
int
wamr_zephyr_sync_pool_prepare(const wamr_zephyr_sync_pool_t *pool,
                              k_tid_t wamr_user_thread);

#endif /* WAMR_ZEPHYR_SYNC_POOL_H */

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_THREAD_POOL_H
#define WAMR_ZEPHYR_THREAD_POOL_H

#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "platform_common.h"

#if !defined(BH_ZEPHYR_MPU_STACK_SIZE)
#define BH_ZEPHYR_MPU_STACK_SIZE APP_THREAD_STACK_SIZE_MIN
#endif

#if !defined(BH_ZEPHYR_MPU_STACK_COUNT)
#define BH_ZEPHYR_MPU_STACK_COUNT 4
#endif

typedef struct wamr_zephyr_thread_pool {
    struct k_thread *threads;
    k_thread_stack_t *stacks;
    size_t thread_count;
    size_t stack_size;
    size_t stack_stride;
} wamr_zephyr_thread_pool_t;

#define WAMR_ZEPHYR_THREAD_POOL_DEFINE(name, count, size)            \
    BUILD_ASSERT((count) > 0, "WAMR thread pool must not be empty"); \
    BUILD_ASSERT((count) <= BH_ZEPHYR_MPU_STACK_COUNT,               \
                 "WAMR thread pool exceeds bookkeeping capacity");   \
    static struct k_thread name##_thread_objects[count];             \
    K_THREAD_STACK_ARRAY_DEFINE(name##_stack_objects, count, size);  \
    static const wamr_zephyr_thread_pool_t name = {                  \
        .threads = name##_thread_objects,                            \
        .stacks = &name##_stack_objects[0][0],                       \
        .thread_count = count,                                       \
        .stack_size = size,                                          \
        .stack_stride = sizeof(name##_stack_objects[0]),             \
    }

/* Prepare once in supervisor mode for a genuine created user root. On
 * userspace builds, storage must be unused, statically registered thread and
 * stack arrays; stack_stride is the actual sizeof one stack-array element.
 * Capacity (including Zephyr's reserved bytes) is checked before publication.
 * An identical binding may be prepared again without resetting live objects.
 */
int
wamr_zephyr_thread_pool_prepare(const wamr_zephyr_thread_pool_t *pool,
                                k_tid_t wamr_user_thread);

#endif /* WAMR_ZEPHYR_THREAD_POOL_H */

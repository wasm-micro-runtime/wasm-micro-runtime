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

#if !defined(BH_ZEPHYR_THREAD_POOL_MAX_THREADS)
#define BH_ZEPHYR_THREAD_POOL_MAX_THREADS 4
#endif

typedef struct wamr_zephyr_thread_pool {
    struct k_thread *threads;
    k_thread_stack_t *stacks;
    size_t thread_count;
    size_t stack_size;
    size_t stack_stride;
} wamr_zephyr_thread_pool_t;

/* size is the usable native stack size of every worker, replacing the former
 * implicit APP_THREAD_STACK_SIZE_MIN budget. Nonzero requests up to size use
 * the whole slot; larger requests fail. Size for the largest enabled creator's
 * request, normally APP_THREAD_STACK_SIZE_DEFAULT, not only the old minimum.
 */
#define WAMR_ZEPHYR_THREAD_POOL_DEFINE(name, count, size)            \
    BUILD_ASSERT((count) > 0, "WAMR thread pool must not be empty"); \
    BUILD_ASSERT((count) <= BH_ZEPHYR_THREAD_POOL_MAX_THREADS,       \
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

/* Prepare once, serially in supervisor mode, for a genuine created root. On
 * userspace builds, storage must be unused, statically registered thread and
 * stack arrays; stack_stride is the actual sizeof one stack-array element.
 * Capacity (including Zephyr's reserved bytes) is checked before publication.
 * An identical binding may be prepared again without resetting live objects.
 * Provisioning survives quiescent runtime init/destroy cycles: the pool storage
 * and designated root must remain alive. There is no release or rebind API.
 * No pool is needed for initialization or execution that creates no workers;
 * creation without preparation returns BHT_ERROR.
 * Runtime-internal creators share this application pool. Prepare before guest
 * thread creation, wasm_runtime_spawn_thread(), or enabled debugger/JIT worker
 * creation (which can happen during module loading), not only direct API calls.
 */
int
wamr_zephyr_thread_pool_prepare(const wamr_zephyr_thread_pool_t *pool,
                                k_tid_t root_tid);

#endif /* WAMR_ZEPHYR_THREAD_POOL_H */

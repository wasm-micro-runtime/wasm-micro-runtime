/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>

#include "test_common.h"
#include "platform_api_vmcore.h"
#include "zephyr_thread_pool.h"

WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_test_threads, 4, 2048);
WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_replacement_threads, 1, 2048);
ZTEST_BMEM static uint8_t thread_test_pool[TEST_POOL_SIZE] __aligned(8);

#define UNMAPPED_SELF_THREAD_STACK_SIZE 1024U

static struct k_thread unmapped_self_thread;
K_THREAD_STACK_DEFINE(unmapped_self_thread_stack,
                      UNMAPPED_SELF_THREAD_STACK_SIZE);

static void
record_unmapped_self(void *arg1, void *arg2, void *arg3)
{
    korp_tid *result = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    *result = os_self_thread();
}

static int
run_unmapped_self_thread(korp_tid *result)
{
    k_tid_t tid;

    *result = NULL;
    tid = k_thread_create(&unmapped_self_thread, unmapped_self_thread_stack,
                          K_THREAD_STACK_SIZEOF(unmapped_self_thread_stack),
                          record_unmapped_self, result, NULL, NULL, 5, 0,
                          K_NO_WAIT);
    if (tid == NULL) {
        return BHT_ERROR;
    }

    return k_thread_join(tid, K_SECONDS(1)) == 0 ? BHT_OK : BHT_ERROR;
}

int
wamr_test_thread_pool_prepare(void)
{
#if defined(CONFIG_USERSPACE)
    static bool bounds_checked;

    if (!bounds_checked) {
        wamr_zephyr_thread_pool_t oversized = wamr_test_threads;

        /* Run before the first binding: replacement rejection must not hide
         * acceptance of a capacity claim larger than this registered stack.
         * Count one keeps every supplied object address genuine.
         */
        oversized.thread_count = 1U;
        oversized.stack_size = 1024U * 1024U;
        oversized.stack_stride = oversized.stack_size;
        zassert_equal(
            wamr_zephyr_thread_pool_prepare(&oversized, k_current_get()),
            BHT_ERROR, "oversized registered stack was accepted");

        oversized.stack_size = wamr_test_threads.stack_size;
        oversized.stack_stride = 1024U * 1024U;
        zassert_equal(
            wamr_zephyr_thread_pool_prepare(&oversized, k_current_get()),
            BHT_ERROR, "incorrect registered stack stride was accepted");
        bounds_checked = true;
    }
#endif
    return wamr_zephyr_thread_pool_prepare(&wamr_test_threads, k_current_get());
}

void
wamr_thread_test_before(void *fixture)
{
    RuntimeInitArgs args = { 0 };

    ARG_UNUSED(fixture);
    memset(thread_test_pool, 0xA5, sizeof(thread_test_pool));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = thread_test_pool;
    args.mem_alloc_option.pool.heap_size = sizeof(thread_test_pool);
    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "thread pool preparation failed");
    zassert_true(wasm_runtime_full_init(&args), "pool init failed");
}

ZTEST_SUITE(platform_thread_pool, NULL, NULL, NULL, NULL, NULL);

ZTEST(platform_thread_pool, test_prepare_validates_and_preserves_pool)
{
    wamr_zephyr_thread_pool_t malformed = wamr_test_threads;
    RuntimeInitArgs args = { 0 };

    zassert_equal(wamr_zephyr_thread_pool_prepare(NULL, k_current_get()),
                  BHT_ERROR, "null pool was accepted");
    zassert_equal(wamr_zephyr_thread_pool_prepare(&wamr_test_threads, NULL),
                  BHT_ERROR, "null target was accepted");

    malformed.thread_count = 0U;
    zassert_equal(wamr_zephyr_thread_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "empty pool was accepted");

    malformed = wamr_test_threads;
    malformed.stack_size = 0U;
    zassert_equal(wamr_zephyr_thread_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "zero-sized stack was accepted");

    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "initial pool preparation failed");
    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "identical pool preparation failed");
    zassert_equal(wamr_zephyr_thread_pool_prepare(&wamr_replacement_threads,
                                                  k_current_get()),
                  BHT_ERROR, "active pool was replaced");

    memset(test_pool, 0xA5, sizeof(test_pool));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = test_pool;
    args.mem_alloc_option.pool.heap_size = sizeof(test_pool);
    zassert_true(wasm_runtime_full_init(&args),
                 "runtime initialization after pool preparation failed");
    wasm_runtime_destroy();
}

ZTEST(platform_thread_pool,
      test_self_thread_rejects_preinit_and_unmapped_callers)
{
    RuntimeInitArgs args = { 0 };
    korp_tid preinit_identity = os_self_thread();
    korp_tid unmapped_identity = NULL;
    korp_tid destroyed_identity;
    int unmapped_result = BHT_ERROR;
    bool runtime_initialized;

    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "thread pool preparation failed");
    memset(test_pool, 0xA5, sizeof(test_pool));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = test_pool;
    args.mem_alloc_option.pool.heap_size = sizeof(test_pool);
    runtime_initialized = wasm_runtime_full_init(&args);
    if (runtime_initialized) {
        unmapped_result = run_unmapped_self_thread(&unmapped_identity);
        wasm_runtime_destroy();
    }
    destroyed_identity = os_self_thread();

    zassert_true(runtime_initialized, "runtime initialization failed");
    zassert_equal(unmapped_result, BHT_OK,
                  "unmapped Zephyr thread did not complete");
    zassert_is_null(preinit_identity,
                    "pre-init caller received a native thread identity");
    zassert_is_null(unmapped_identity,
                    "unmapped caller received a native thread identity");
    zassert_is_null(destroyed_identity,
                    "post-destroy caller received a native thread identity");
}

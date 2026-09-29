/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>

#include "test_common.h"
#include "platform_api_vmcore.h"
#include "zephyr_thread_pool.h"

WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_test_threads, WAMR_TEST_THREAD_COUNT,
                               WAMR_TEST_THREAD_STACK_SIZE);
WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_replacement_threads, 1,
                               WAMR_TEST_THREAD_STACK_SIZE);
static ZTEST_BMEM uint8_t test_pool[TEST_POOL_SIZE] __aligned(8);
static k_tid_t thread_pool_test_owner;

#define NATIVE_SELF_THREAD_STACK_SIZE 1024U

static struct k_thread native_self_thread;
K_THREAD_STACK_DEFINE(native_self_thread_stack, NATIVE_SELF_THREAD_STACK_SIZE);

static void
record_native_self(void *arg1, void *arg2, void *arg3)
{
    korp_tid *result = arg1;
    korp_tid *repeated = arg2;

    ARG_UNUSED(arg3);
    *result = os_self_thread();
    *repeated = os_self_thread();
}

static void *
unused_worker(void *arg)
{
    return arg;
}

/* Worker management is only available while the runtime is initialized. */
static bool
worker_management_rejected(void)
{
    korp_tid worker = NULL;
    korp_tid self = os_self_thread();

    return os_thread_create(&worker, unused_worker, NULL,
                            WAMR_TEST_THREAD_STACK_SIZE)
               == BHT_ERROR
           && worker == NULL && os_thread_join(self, NULL) == BHT_ERROR
           && os_thread_detach(self) == BHT_ERROR;
}

static int
run_native_self_thread(korp_tid *result, korp_tid *repeated, k_tid_t *native)
{
    k_tid_t tid;

    *result = NULL;
    *repeated = NULL;
    tid = k_thread_create(&native_self_thread, native_self_thread_stack,
                          K_THREAD_STACK_SIZEOF(native_self_thread_stack),
                          record_native_self, result, repeated, NULL, 5, 0,
                          K_NO_WAIT);
    *native = tid;
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
            wamr_zephyr_thread_pool_prepare(&oversized, thread_pool_test_owner),
            BHT_ERROR, "oversized registered stack was accepted");

        oversized.stack_size = wamr_test_threads.stack_size;
        oversized.stack_stride = 1024U * 1024U;
        zassert_equal(
            wamr_zephyr_thread_pool_prepare(&oversized, thread_pool_test_owner),
            BHT_ERROR, "incorrect registered stack stride was accepted");
        bounds_checked = true;
    }
#endif
    return wamr_zephyr_thread_pool_prepare(&wamr_test_threads,
                                           thread_pool_test_owner);
}

void *
wamr_test_thread_pool_setup(void)
{
    /* Suite setup runs on the persistent Ztest runner; individual case threads
     * are recreated and inherit the runner's object permissions.
     */
    if (thread_pool_test_owner == NULL) {
        thread_pool_test_owner = k_current_get();
    }
    zassert_equal(thread_pool_test_owner, k_current_get(),
                  "pool owner must stay alive across suites");
    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "stable-owner thread pool setup failed");
    return NULL;
}

static bool
init_test_runtime(void)
{
    RuntimeInitArgs args = { 0 };

    memset(test_pool, 0xA5, sizeof(test_pool));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = test_pool;
    args.mem_alloc_option.pool.heap_size = sizeof(test_pool);
    return wasm_runtime_full_init(&args);
}

void
pool_before(void *fixture)
{
    ARG_UNUSED(fixture);
    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "thread pool preparation failed");
    zassert_true(init_test_runtime(), "pool init failed");
}

void
pool_after(void *fixture)
{
    ARG_UNUSED(fixture);
    wasm_runtime_destroy();
}

ZTEST_SUITE(platform_thread_pool, NULL, wamr_test_thread_pool_setup, NULL, NULL,
            NULL);

ZTEST(platform_thread_pool, test_prepare_validates_and_preserves_pool)
{
    wamr_zephyr_thread_pool_t malformed = wamr_test_threads;

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
                                                  thread_pool_test_owner),
                  BHT_ERROR, "active pool was replaced");

    zassert_true(init_test_runtime(),
                 "runtime initialization after pool preparation failed");
    wasm_runtime_destroy();
}

/* Native Zephyr callers that WAMR did not create are identified by their own
 * k_tid_t: before initialization, while the runtime is live and after it is
 * destroyed. Such identities are never accepted as WAMR workers.
 */
ZTEST(platform_thread_pool, test_self_thread_identifies_native_callers)
{
    korp_tid self = (korp_tid)k_current_get();
    korp_tid preinit_identity = os_self_thread();
    bool preinit_rejected;
    bool destroyed_rejected;
    korp_tid native_identity = NULL;
    korp_tid native_repeated = NULL;
    korp_tid live_identity = NULL;
    korp_tid destroyed_identity;
    k_tid_t native_tid = NULL;
    int native_result = BHT_ERROR;
    int native_join = BHT_OK;
    int native_detach = BHT_OK;
    int self_join = BHT_OK;
    int self_detach = BHT_OK;
    bool runtime_initialized;

    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "thread pool preparation failed");
    /* A prepared pool alone does not enable worker management. */
    preinit_rejected = worker_management_rejected();
    runtime_initialized = init_test_runtime();
    if (runtime_initialized) {
        live_identity = os_self_thread();
        native_result = run_native_self_thread(&native_identity,
                                               &native_repeated, &native_tid);
        native_join = os_thread_join(native_identity, NULL);
        native_detach = os_thread_detach(native_identity);
        self_join = os_thread_join(live_identity, NULL);
        self_detach = os_thread_detach(live_identity);
        wasm_runtime_destroy();
    }
    destroyed_identity = os_self_thread();
    destroyed_rejected = worker_management_rejected();

    zassert_true(runtime_initialized, "runtime initialization failed");
    zassert_true(preinit_rejected,
                 "worker management was available before runtime init");
    zassert_true(destroyed_rejected,
                 "worker management was available after runtime destroy");
    zassert_equal(native_result, BHT_OK,
                  "native Zephyr thread did not complete");
    zassert_equal_ptr(preinit_identity, self,
                      "pre-init caller is not identified by its k_tid_t");
    zassert_equal_ptr(live_identity, self,
                      "initializer identity changed during runtime use");
    zassert_equal_ptr(destroyed_identity, self,
                      "post-destroy caller is not identified by its k_tid_t");
    zassert_equal_ptr(native_identity, (korp_tid)native_tid,
                      "native caller is not identified by its k_tid_t");
    zassert_equal_ptr(native_repeated, native_identity,
                      "native caller identity is not stable");
    zassert_not_equal(native_identity, self,
                      "distinct native callers share an identity");
    zassert_equal(native_join, BHT_ERROR,
                  "native caller identity was accepted by worker join");
    zassert_equal(native_detach, BHT_ERROR,
                  "native caller identity was accepted by worker detach");
    zassert_equal(self_join, BHT_ERROR,
                  "initializer identity was accepted by worker join");
    zassert_equal(self_detach, BHT_ERROR,
                  "initializer identity was accepted by worker detach");
}

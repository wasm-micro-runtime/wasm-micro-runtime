/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>

#include "test_common.h"
#include "platform_api_vmcore.h"
#include "zephyr_sync_pool.h"
#include "zephyr_thread_pool.h"

WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_test_threads, WAMR_TEST_THREAD_COUNT,
                               WAMR_TEST_THREAD_STACK_SIZE);
WAMR_ZEPHYR_THREAD_POOL_DEFINE(wamr_replacement_threads, 1,
                               WAMR_TEST_THREAD_STACK_SIZE);
WAMR_ZEPHYR_SYNC_POOL_DEFINE(wamr_test_sync, 8, 4);
WAMR_ZEPHYR_SYNC_POOL_DEFINE(wamr_replacement_sync, 1, 1);
static ZTEST_BMEM uint8_t test_pool[TEST_POOL_SIZE] __aligned(8);
static k_tid_t thread_pool_test_owner;

#define SYNC_POOL_PREPARE_THREAD_STACK_SIZE 1024U
#define UNMAPPED_SELF_THREAD_STACK_SIZE 1024U

static k_tid_t sync_pool_test_owner;

struct concurrent_sync_pool_prepare_context {
    const wamr_zephyr_sync_pool_t *pool;
    int result;
};

static struct concurrent_sync_pool_prepare_context concurrent_prepare_ctx[2];
static struct k_thread concurrent_prepare_threads[2];
static struct k_thread unmapped_self_thread;
K_THREAD_STACK_DEFINE(concurrent_prepare_stack_0,
                      SYNC_POOL_PREPARE_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(concurrent_prepare_stack_1,
                      SYNC_POOL_PREPARE_THREAD_STACK_SIZE);
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

int
wamr_test_sync_pool_prepare(void)
{
    return wamr_zephyr_sync_pool_prepare(&wamr_test_sync, k_current_get());
}

struct k_mutex *
wamr_test_sync_mutex(void)
{
    return &wamr_test_sync.mutexes[0];
}

struct k_condvar *
wamr_test_sync_condvar(void)
{
    return &wamr_test_sync.condvars[0];
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

ZTEST(platform_thread_pool,
      test_self_thread_rejects_preinit_and_unmapped_callers)
{
    korp_tid preinit_identity = os_self_thread();
    korp_tid unmapped_identity = NULL;
    korp_tid destroyed_identity;
    int unmapped_result = BHT_ERROR;
    bool runtime_initialized;

    zassert_equal(wamr_test_thread_pool_prepare(), BHT_OK,
                  "thread pool preparation failed");
    runtime_initialized = init_test_runtime();
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

static void
concurrent_sync_pool_prepare(void *arg1, void *arg2, void *arg3)
{
    struct concurrent_sync_pool_prepare_context *context = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    context->result =
        wamr_zephyr_sync_pool_prepare(context->pool, sync_pool_test_owner);
}

static void
run_concurrent_sync_pool_prepare(const wamr_zephyr_sync_pool_t *pool)
{
    k_tid_t first;
    k_tid_t second;

    memset(concurrent_prepare_ctx, 0, sizeof(concurrent_prepare_ctx));
    concurrent_prepare_ctx[0].pool = pool;
    concurrent_prepare_ctx[1].pool = pool;
    first = k_thread_create(
        &concurrent_prepare_threads[0], concurrent_prepare_stack_0,
        K_THREAD_STACK_SIZEOF(concurrent_prepare_stack_0),
        concurrent_sync_pool_prepare, &concurrent_prepare_ctx[0], NULL, NULL, 5,
        0, K_FOREVER);
    second = k_thread_create(
        &concurrent_prepare_threads[1], concurrent_prepare_stack_1,
        K_THREAD_STACK_SIZEOF(concurrent_prepare_stack_1),
        concurrent_sync_pool_prepare, &concurrent_prepare_ctx[1], NULL, NULL, 5,
        0, K_FOREVER);
    zassert_not_null(first, "first concurrent prepare thread creation failed");
    zassert_not_null(second,
                     "second concurrent prepare thread creation failed");
    k_thread_start(first);
    k_thread_start(second);
    zassert_equal(k_thread_join(first, K_SECONDS(1)), 0,
                  "first concurrent prepare thread did not join");
    zassert_equal(k_thread_join(second, K_SECONDS(1)), 0,
                  "second concurrent prepare thread did not join");
}

static void *
sync_pool_setup(void)
{
    /* Prepare on the persistent runner, including when concurrent cases run
     * before the descriptor-validation case.
     */
    sync_pool_test_owner = k_current_get();
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&wamr_test_sync, sync_pool_test_owner),
        BHT_OK, "sync pool setup failed");
    return NULL;
}

ZTEST_SUITE(platform_sync_pool, NULL, sync_pool_setup, NULL, NULL, NULL);

ZTEST(platform_sync_pool, test_prepare_validates_and_preserves_sync_pool)
{
    wamr_zephyr_sync_pool_t malformed = wamr_test_sync;
    bool runtime_initialized;

    zassert_equal(wamr_zephyr_sync_pool_prepare(NULL, k_current_get()),
                  BHT_ERROR, "null sync pool was accepted");
    zassert_equal(wamr_zephyr_sync_pool_prepare(&wamr_test_sync, NULL),
                  BHT_ERROR, "null sync pool owner was accepted");

    malformed.management_lock = NULL;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "null management lock was accepted");

    malformed = wamr_test_sync;
    malformed.mutexes = NULL;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "null mutex array was accepted");

    malformed = wamr_test_sync;
    malformed.condvars = NULL;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "null condvar array was accepted");

    malformed = wamr_test_sync;
    malformed.mutex_count = 0U;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "zero-sized mutex pool was accepted");

    malformed = wamr_test_sync;
    malformed.condvar_count = 0U;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "condition storage with zero count was accepted");

    malformed = wamr_test_sync;
    malformed.mutex_count = BH_ZEPHYR_MUTEX_POOL_COUNT + 1U;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "oversized mutex pool was accepted");

    malformed = wamr_test_sync;
    malformed.condvar_count = BH_ZEPHYR_COND_POOL_COUNT + 1U;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "oversized condvar pool was accepted");

    malformed = wamr_test_sync;
    malformed.management_lock = &wamr_test_sync.mutexes[1];
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "management lock aliased the mutex array");

    malformed = wamr_test_sync;
    malformed.management_lock = (struct k_mutex *)&wamr_test_sync.condvars[1];
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "management lock aliased the condition array");

    malformed = wamr_test_sync;
    malformed.mutex_count = 1U;
    malformed.condvars = (struct k_condvar *)wamr_test_sync.mutexes;
    malformed.condvar_count = 1U;
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "overlapping mutex and condition arrays were accepted");

    malformed = wamr_test_sync;
    malformed.management_lock =
        (struct k_mutex *)(UINTPTR_MAX - sizeof(struct k_mutex) + 2U);
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "overflowing management-lock range was accepted");

    malformed = wamr_test_sync;
    malformed.mutexes =
        (struct k_mutex *)(UINTPTR_MAX - sizeof(struct k_mutex) + 2U);
    malformed.mutex_count = 1U;
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "overflowing mutex range was accepted");

    malformed = wamr_test_sync;
    malformed.condvars =
        (struct k_condvar *)(UINTPTR_MAX - sizeof(struct k_condvar) + 2U);
    malformed.condvar_count = 1U;
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&malformed, sync_pool_test_owner),
        BHT_ERROR, "overflowing condition range was accepted");

    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&wamr_test_sync, sync_pool_test_owner),
        BHT_OK, "initial sync pool preparation failed");
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&wamr_test_sync, sync_pool_test_owner),
        BHT_OK, "identical sync pool preparation failed");
    zassert_equal(wamr_zephyr_sync_pool_prepare(&wamr_replacement_sync,
                                                sync_pool_test_owner),
                  BHT_ERROR, "active sync pool was replaced");

    runtime_initialized = init_test_runtime();
    if (runtime_initialized) {
        wasm_runtime_destroy();
    }
    zassert_true(runtime_initialized,
                 "runtime initialization after sync pool preparation failed");
}

ZTEST(platform_sync_pool,
      test_concurrent_prepare_is_idempotent_and_rejects_replacement)
{
    run_concurrent_sync_pool_prepare(&wamr_test_sync);
    zassert_equal(concurrent_prepare_ctx[0].result, BHT_OK,
                  "first concurrent identical prepare failed");
    zassert_equal(concurrent_prepare_ctx[1].result, BHT_OK,
                  "second concurrent identical prepare failed");

    run_concurrent_sync_pool_prepare(&wamr_replacement_sync);
    zassert_equal(concurrent_prepare_ctx[0].result, BHT_ERROR,
                  "first concurrent replacement was accepted");
    zassert_equal(concurrent_prepare_ctx[1].result, BHT_ERROR,
                  "second concurrent replacement was accepted");
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&wamr_test_sync, sync_pool_test_owner),
        BHT_OK, "rejected replacement changed the prepared pool");
}

WAMR_CONTEXT_TEST(platform_sync_pool, test_prepare_rejects_user_context)
{
#if defined(CONFIG_WAMR_TEST_USER_MODE)
    zassert_equal(wamr_test_sync_pool_prepare(), BHT_ERROR,
                  "user context prepared the sync pool");
#else
    ztest_test_skip();
#endif
}

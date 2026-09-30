/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <errno.h>

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define WAMR_TEST_POOL_STORAGE ZTEST_BMEM
#include "test_common.h"
#undef WAMR_TEST_POOL_STORAGE

#include "platform_api_extension.h"
#include "platform_api_vmcore.h"

#if defined(CONFIG_USERSPACE)
BUILD_ASSERT(!__builtin_types_compatible_p(korp_mutex, struct k_mutex *));
BUILD_ASSERT(!__builtin_types_compatible_p(korp_cond, struct k_condvar *));
#else
BUILD_ASSERT(__builtin_types_compatible_p(korp_cond, struct k_condvar));
#endif

#define WAMR_TEST_THREAD_STACK_SIZE 2048U
#define CONDITION_READY_TIMEOUT_MS 500
#define CONDITION_POLL_INTERVAL_MS 1

struct mutex_counter {
    korp_mutex mutex;
    atomic_t started;
    atomic_t release;
    int value;
};

struct mutex_counter_worker {
    struct mutex_counter *counter;
    bool timed_out;
    int lock_result;
    int unlock_result;
};

struct condition_waiter {
    korp_mutex *mutex;
    korp_cond *cond;
    atomic_t ready;
    bool waiting;
    bool woke;
    int result;
    uint64 timeout_us;
};

struct platform_sync_fixture {
    struct mutex_counter counter;
    struct mutex_counter_worker counter_workers[2];
    korp_mutex condition_mutex;
    korp_cond condition_cond;
    struct condition_waiter waiter;
    struct condition_waiter broadcast_waiters[2];
};

ZTEST_DMEM static struct platform_sync_fixture sync_results = { 0 };
ZTEST_DMEM static korp_mutex test_mutex = { 0 };

static bool
wait_for_atomic_value(atomic_t *value, atomic_val_t expected, int timeout_ms)
{
    for (int elapsed_ms = 0; elapsed_ms < timeout_ms; elapsed_ms++) {
        if (atomic_get(value) == expected) {
            return true;
        }
        k_sleep(K_MSEC(CONDITION_POLL_INTERVAL_MS));
    }

    return atomic_get(value) == expected;
}

static void
reset_mutex_counter(struct mutex_counter *counter,
                    struct mutex_counter_worker *workers)
{
    memset(counter, 0, sizeof(*counter));
    for (size_t i = 0; i < 2; i++) {
        memset(&workers[i], 0, sizeof(workers[i]));
        workers[i].counter = counter;
        workers[i].lock_result = BHT_ERROR;
        workers[i].unlock_result = BHT_ERROR;
    }
}

static void
reset_condition_waiter(struct condition_waiter *waiter, korp_mutex *mutex,
                       korp_cond *cond, uint64 timeout_us)
{
    memset(waiter, 0, sizeof(*waiter));
    waiter->mutex = mutex;
    waiter->cond = cond;
    waiter->result = BHT_ERROR;
    waiter->timeout_us = timeout_us;
}

static void *
increment_counter(void *arg)
{
    struct mutex_counter_worker *worker = arg;
    struct mutex_counter *counter = worker->counter;

    atomic_inc(&counter->started);
    if (!wait_for_atomic_value(&counter->release, 1,
                               CONDITION_READY_TIMEOUT_MS)) {
        worker->timed_out = true;
        return NULL;
    }
    for (int i = 0; i < 100; ++i) {
        worker->lock_result = os_mutex_lock(&counter->mutex);
        if (worker->lock_result != BHT_OK) {
            return NULL;
        }
        counter->value++;
        worker->unlock_result = os_mutex_unlock(&counter->mutex);
        if (worker->unlock_result != BHT_OK) {
            return NULL;
        }
    }
    return NULL;
}

static void *
wait_for_condition(void *arg)
{
    struct condition_waiter *waiter = arg;

    waiter->result = os_mutex_lock(waiter->mutex);
    if (waiter->result != BHT_OK) {
        return NULL;
    }

    waiter->waiting = true;
    atomic_set(&waiter->ready, 1);
    waiter->result = waiter->timeout_us == 0U
                         ? os_cond_wait(waiter->cond, waiter->mutex)
                         : os_cond_reltimedwait(waiter->cond, waiter->mutex,
                                                waiter->timeout_us);
    waiter->woke = waiter->result == BHT_OK;
    (void)os_mutex_unlock(waiter->mutex);

    return NULL;
}

static void
signal_waiter(struct condition_waiter *waiter)
{
    zassert_true(
        wait_for_atomic_value(&waiter->ready, 1, CONDITION_READY_TIMEOUT_MS),
        "waiter did not become ready");
    zassert_equal(os_mutex_lock(waiter->mutex), BHT_OK,
                  "parent failed to lock waiter mutex");
    zassert_true(waiter->waiting, "waiter did not publish its state");
    zassert_equal(os_cond_signal(waiter->cond), BHT_OK,
                  "condition signal failed");
    zassert_equal(os_mutex_unlock(waiter->mutex), BHT_OK,
                  "parent failed to unlock waiter mutex");
}

static void
broadcast_waiters(struct condition_waiter *first,
                  struct condition_waiter *second)
{
    zassert_true(
        wait_for_atomic_value(&first->ready, 1, CONDITION_READY_TIMEOUT_MS),
        "first waiter did not become ready");
    zassert_true(
        wait_for_atomic_value(&second->ready, 1, CONDITION_READY_TIMEOUT_MS),
        "second waiter did not become ready");
    zassert_equal(os_mutex_lock(first->mutex), BHT_OK,
                  "parent failed to lock waiter mutex");
    zassert_true(first->waiting, "first waiter did not publish its state");
    zassert_true(second->waiting, "second waiter did not publish its state");
    zassert_equal(os_cond_broadcast(first->cond), BHT_OK,
                  "condition broadcast failed");
    zassert_equal(os_mutex_unlock(first->mutex), BHT_OK,
                  "parent failed to unlock waiter mutex");
}

static void
join_waiter(korp_tid thread, struct condition_waiter *waiter)
{
    zassert_equal(os_thread_join(thread, NULL), BHT_OK, "thread join failed");
    zassert_equal(waiter->result, BHT_OK, "condition wait failed");
    zassert_true(waiter->woke, "waiter did not return from condition wait");
}

static void *
sync_setup(void)
{
    wamr_test_sync_pool_prepare_contract(k_current_get());
    return &sync_results;
}

ZTEST_SUITE(platform_sync, NULL, sync_setup, pool_before, pool_after, NULL);

WAMR_CONTEXT_TEST_F(platform_sync, test_mutex_lifecycle)
{
    int init_result = os_mutex_init(&test_mutex);
    int lock_result =
        init_result == BHT_OK ? os_mutex_lock(&test_mutex) : BHT_ERROR;
    int unlock_result =
        lock_result == BHT_OK ? os_mutex_unlock(&test_mutex) : BHT_ERROR;
    int destroy_result =
        init_result == BHT_OK ? os_mutex_destroy(&test_mutex) : BHT_ERROR;

    zassert_equal(init_result, BHT_OK, "mutex init failed");
    zassert_equal(lock_result, BHT_OK, "mutex lock failed");
    zassert_equal(unlock_result, BHT_OK, "mutex unlock failed");
    zassert_equal(destroy_result, BHT_OK, "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_mutex_serializes_two_threads)
{
    struct mutex_counter *counter = &sync_results.counter;
    struct mutex_counter_worker *workers = sync_results.counter_workers;
    korp_tid first;
    korp_tid second;

    reset_mutex_counter(counter, workers);
    zassert_equal(os_mutex_init(&counter->mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_thread_create(&first, increment_counter, &workers[0],
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "first thread creation failed");
    zassert_true(
        wait_for_atomic_value(&counter->started, 1, CONDITION_READY_TIMEOUT_MS),
        "first thread did not start");
    zassert_equal(os_thread_create(&second, increment_counter, &workers[1],
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "second thread creation failed");
    zassert_true(
        wait_for_atomic_value(&counter->started, 2, CONDITION_READY_TIMEOUT_MS),
        "second thread did not start");
    atomic_set(&counter->release, 1);
    zassert_equal(os_thread_join(first, NULL), BHT_OK,
                  "first thread join failed");
    zassert_equal(os_thread_join(second, NULL), BHT_OK,
                  "second thread join failed");
    for (size_t i = 0; i < ARRAY_SIZE(sync_results.counter_workers); i++) {
        zassert_false(workers[i].timed_out,
                      "worker %zu did not receive its release", i);
        zassert_equal(workers[i].lock_result, BHT_OK,
                      "worker %zu mutex lock failed", i);
        zassert_equal(workers[i].unlock_result, BHT_OK,
                      "worker %zu mutex unlock failed", i);
    }
    zassert_equal(counter->value, 200, "mutex did not serialize increments");
    zassert_equal(os_mutex_destroy(&counter->mutex), BHT_OK,
                  "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_condition_signal_wakes_waiter)
{
    struct condition_waiter *waiter = &sync_results.waiter;
    korp_tid thread;

    reset_condition_waiter(waiter, &sync_results.condition_mutex,
                           &sync_results.condition_cond, 0U);
    zassert_equal(os_mutex_init(waiter->mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(waiter->cond), BHT_OK, "condition init failed");
    zassert_equal(os_thread_create(&thread, wait_for_condition, waiter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    signal_waiter(waiter);
    join_waiter(thread, waiter);
    zassert_equal(os_cond_destroy(waiter->cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(waiter->mutex), BHT_OK,
                  "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_condition_timed_wait_returns)
{
    uint64 start_us;
    uint64 elapsed_us;
    int wait_result;

    zassert_equal(os_mutex_init(&sync_results.condition_mutex), BHT_OK,
                  "mutex init failed");
    zassert_equal(os_cond_init(&sync_results.condition_cond), BHT_OK,
                  "condition init failed");
    zassert_equal(os_mutex_lock(&sync_results.condition_mutex), BHT_OK,
                  "mutex lock failed");
    start_us = os_time_get_boot_us();
    wait_result = os_cond_reltimedwait(&sync_results.condition_cond,
                                       &sync_results.condition_mutex, 20000U);
    elapsed_us = os_time_get_boot_us() - start_us;
    zassert_equal(os_mutex_unlock(&sync_results.condition_mutex), BHT_OK,
                  "mutex unlock failed");
    zassert_true(elapsed_us >= 10000U, "timed wait returned too early");
    zassert_true(elapsed_us < 500000U, "timed wait exceeded its bound");
    zassert_equal(os_cond_destroy(&sync_results.condition_cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(&sync_results.condition_mutex), BHT_OK,
                  "mutex destroy failed");
#if defined(CONFIG_USERSPACE)
    zassert_equal(wait_result, ETIMEDOUT,
                  "timed condition wait did not report timeout");
#else
    zassert_equal(wait_result, BHT_OK, "timed condition wait failed");
#endif
}

WAMR_CONTEXT_TEST_F(platform_sync, test_mutex_is_reusable)
{
    int init_result = os_mutex_init(&test_mutex);
    int lock_results[2] = { BHT_ERROR, BHT_ERROR };
    int unlock_results[2] = { BHT_ERROR, BHT_ERROR };

    for (int i = 0; i < 2; ++i) {
        if (init_result == BHT_OK) {
            lock_results[i] = os_mutex_lock(&test_mutex);
        }
        if (lock_results[i] == BHT_OK) {
            unlock_results[i] = os_mutex_unlock(&test_mutex);
        }
    }
    int destroy_result =
        init_result == BHT_OK ? os_mutex_destroy(&test_mutex) : BHT_ERROR;

    zassert_equal(init_result, BHT_OK, "mutex init failed");
    zassert_equal(lock_results[0], BHT_OK, "first mutex lock failed");
    zassert_equal(unlock_results[0], BHT_OK, "first mutex unlock failed");
    zassert_equal(lock_results[1], BHT_OK, "second mutex lock failed");
    zassert_equal(unlock_results[1], BHT_OK, "second mutex unlock failed");
    zassert_equal(destroy_result, BHT_OK, "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_condition_broadcast_wakes_waiters)
{
    struct condition_waiter *first = &sync_results.broadcast_waiters[0];
    struct condition_waiter *second = &sync_results.broadcast_waiters[1];
    korp_tid first_thread;
    korp_tid second_thread;

    reset_condition_waiter(first, &sync_results.condition_mutex,
                           &sync_results.condition_cond, 0U);
    reset_condition_waiter(second, &sync_results.condition_mutex,
                           &sync_results.condition_cond, 0U);
    zassert_equal(os_mutex_init(first->mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(first->cond), BHT_OK, "condition init failed");
    zassert_equal(os_thread_create(&first_thread, wait_for_condition, first,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "first thread creation failed");
    zassert_equal(os_thread_create(&second_thread, wait_for_condition, second,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "second thread creation failed");
    broadcast_waiters(first, second);
    join_waiter(first_thread, first);
    join_waiter(second_thread, second);
    zassert_equal(os_cond_destroy(first->cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(first->mutex), BHT_OK,
                  "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_condition_is_reusable)
{
    struct condition_waiter *waiter = &sync_results.waiter;

    reset_condition_waiter(waiter, &sync_results.condition_mutex,
                           &sync_results.condition_cond, 0U);
    zassert_equal(os_mutex_init(waiter->mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(waiter->cond), BHT_OK, "condition init failed");
    for (int i = 0; i < 2; ++i) {
        korp_tid thread;

        reset_condition_waiter(waiter, waiter->mutex, waiter->cond, 0U);
        zassert_equal(os_thread_create(&thread, wait_for_condition, waiter,
                                       WAMR_TEST_THREAD_STACK_SIZE),
                      BHT_OK, "thread creation failed");
        signal_waiter(waiter);
        join_waiter(thread, waiter);
    }
    zassert_equal(os_cond_destroy(waiter->cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(waiter->mutex), BHT_OK,
                  "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_large_condition_timeout_is_clamped)
{
    struct condition_waiter *waiter = &sync_results.waiter;
    korp_tid thread;

    reset_condition_waiter(waiter, &sync_results.condition_mutex,
                           &sync_results.condition_cond,
                           (uint64)INT32_MAX * 1000ULL + 1000ULL);
    zassert_equal(os_mutex_init(waiter->mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(waiter->cond), BHT_OK, "condition init failed");
    zassert_equal(os_thread_create(&thread, wait_for_condition, waiter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    signal_waiter(waiter);
    join_waiter(thread, waiter);
    zassert_equal(os_cond_destroy(waiter->cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(waiter->mutex), BHT_OK,
                  "mutex destroy failed");
}

WAMR_CONTEXT_TEST(platform_sync, test_named_semaphore_api_reports_unsupported)
{
    zassert_is_null(os_sem_open("wamr", 0, 0, 1), NULL);
    zassert_equal(os_sem_close(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_wait(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_trywait(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_post(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_getvalue(NULL, NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_unlink("wamr"), BHT_ERROR, NULL);
}

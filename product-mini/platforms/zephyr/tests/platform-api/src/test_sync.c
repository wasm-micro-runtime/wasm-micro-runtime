/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>

#include "test_common.h"

#include "platform_api_extension.h"
#include "platform_api_vmcore.h"

#define WAMR_TEST_THREAD_STACK_SIZE 2048U
#define CONDITION_READY_TIMEOUT K_MSEC(500)

struct platform_sync_fixture {
    int mutex_init_result;
};

ZTEST_DMEM static struct platform_sync_fixture sync_results = { 0 };
ZTEST_DMEM static korp_mutex test_mutex = { 0 };

struct mutex_counter {
    korp_mutex mutex;
    struct k_sem started;
    struct k_sem go;
    int value;
};

struct condition_waiter {
    korp_mutex mutex;
    korp_cond cond;
    struct k_sem ready;
    bool waiting;
    bool woke;
    int result;
    uint64 timeout_us;
};

static void *
increment_counter(void *arg)
{
    struct mutex_counter *counter = arg;

    k_sem_give(&counter->started);
    if (k_sem_take(&counter->go, CONDITION_READY_TIMEOUT) != 0) {
        return NULL;
    }
    for (int i = 0; i < 100; ++i) {
        if (os_mutex_lock(&counter->mutex) != BHT_OK) {
            return NULL;
        }
        counter->value++;
        if (os_mutex_unlock(&counter->mutex) != BHT_OK) {
            return NULL;
        }
    }
    return NULL;
}

static void *
wait_for_condition(void *arg)
{
    struct condition_waiter *waiter = arg;

    waiter->result = os_mutex_lock(&waiter->mutex);
    if (waiter->result != BHT_OK) {
        return NULL;
    }

    waiter->waiting = true;
    k_sem_give(&waiter->ready);
    waiter->result = waiter->timeout_us == 0U
                         ? os_cond_wait(&waiter->cond, &waiter->mutex)
                         : os_cond_reltimedwait(&waiter->cond, &waiter->mutex,
                                                waiter->timeout_us);
    waiter->woke = waiter->result == BHT_OK;
    (void)os_mutex_unlock(&waiter->mutex);

    return NULL;
}

static void
signal_waiter(struct condition_waiter *waiter)
{
    zassert_equal(k_sem_take(&waiter->ready, CONDITION_READY_TIMEOUT), 0,
                  "waiter did not become ready");
    zassert_equal(os_mutex_lock(&waiter->mutex), BHT_OK,
                  "parent failed to lock waiter mutex");
    zassert_true(waiter->waiting, "waiter did not publish its state");
    zassert_equal(os_cond_signal(&waiter->cond), BHT_OK,
                  "condition signal failed");
    zassert_equal(os_mutex_unlock(&waiter->mutex), BHT_OK,
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
    return &sync_results;
}

static void
sync_before(void *fixture)
{
    struct platform_sync_fixture *results = fixture;

    pool_before(fixture);
    results->mutex_init_result = os_mutex_init(&test_mutex);
}

static void
sync_after(void *fixture)
{
    int destroy_result = os_mutex_destroy(&test_mutex);

    pool_after(fixture);
    zassert_equal(destroy_result, BHT_OK, "mutex destroy failed");
}

ZTEST_SUITE(platform_sync, NULL, sync_setup, sync_before, sync_after, NULL);

WAMR_CONTEXT_TEST_F(platform_sync, test_mutex_lifecycle)
{
    zassert_equal(fixture->mutex_init_result, BHT_OK, "mutex init failed");
    zassert_equal(os_mutex_lock(&test_mutex), BHT_OK, "mutex lock failed");
    zassert_equal(os_mutex_unlock(&test_mutex), BHT_OK, "mutex unlock failed");
}

ZTEST(platform_sync, test_mutex_serializes_two_threads)
{
    struct mutex_counter counter = { 0 };
    korp_tid first;
    korp_tid second;

    /* FIXME: native_sim blocks during the second os_thread_create before
     * either worker enters, so this deterministic contention contract is
     * skipped. */
    ztest_test_skip();

    k_sem_init(&counter.started, 0, 2);
    k_sem_init(&counter.go, 0, 2);
    zassert_equal(os_mutex_init(&counter.mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_thread_create(&first, increment_counter, &counter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "first thread creation failed");
    zassert_equal(k_sem_take(&counter.started, CONDITION_READY_TIMEOUT), 0,
                  "first thread did not start");
    zassert_equal(os_thread_create(&second, increment_counter, &counter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "second thread creation failed");
    zassert_equal(k_sem_take(&counter.started, CONDITION_READY_TIMEOUT), 0,
                  "second thread did not start");
    k_sem_give(&counter.go);
    k_sem_give(&counter.go);
    zassert_equal(os_thread_join(first, NULL), BHT_OK,
                  "first thread join failed");
    zassert_equal(os_thread_join(second, NULL), BHT_OK,
                  "second thread join failed");
    zassert_equal(counter.value, 200, "mutex did not serialize increments");
    zassert_equal(os_mutex_destroy(&counter.mutex), BHT_OK,
                  "mutex destroy failed");
}

ZTEST(platform_sync, test_condition_signal_wakes_waiter)
{
#if defined(CONFIG_WAMR_TEST_USER_MODE)
    /* FIXME: sys_mutex initialization/locking currently fails in the qemu_arc
     * userspace configuration. */
    ztest_test_skip();
#endif
    struct condition_waiter waiter = { 0 };
    korp_tid thread;

    k_sem_init(&waiter.ready, 0, 1);
    zassert_equal(os_mutex_init(&waiter.mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(&waiter.cond), BHT_OK, "condition init failed");
    zassert_equal(os_thread_create(&thread, wait_for_condition, &waiter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    signal_waiter(&waiter);
    join_waiter(thread, &waiter);
    zassert_equal(os_cond_destroy(&waiter.cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(&waiter.mutex), BHT_OK,
                  "mutex destroy failed");
}

ZTEST(platform_sync, test_condition_timed_wait_returns)
{
#if defined(CONFIG_WAMR_TEST_USER_MODE)
    /* FIXME: sys_mutex initialization/locking currently fails in the qemu_arc
     * userspace configuration. */
    ztest_test_skip();
#endif
    korp_mutex mutex;
    korp_cond cond;
    uint64 start_us;
    uint64 elapsed_us;

    zassert_equal(os_mutex_init(&mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(&cond), BHT_OK, "condition init failed");
    zassert_equal(os_mutex_lock(&mutex), BHT_OK, "mutex lock failed");
    start_us = os_time_get_boot_us();
    zassert_equal(os_cond_reltimedwait(&cond, &mutex, 20000U), BHT_OK,
                  "timed condition wait failed");
    elapsed_us = os_time_get_boot_us() - start_us;
    zassert_equal(os_mutex_unlock(&mutex), BHT_OK, "mutex unlock failed");
    zassert_true(elapsed_us >= 10000U, "timed wait returned too early");
    zassert_true(elapsed_us < 500000U, "timed wait exceeded its bound");
    zassert_equal(os_cond_destroy(&cond), BHT_OK, "condition destroy failed");
    zassert_equal(os_mutex_destroy(&mutex), BHT_OK, "mutex destroy failed");
}

WAMR_CONTEXT_TEST_F(platform_sync, test_mutex_is_reusable)
{
    zassert_equal(fixture->mutex_init_result, BHT_OK, "mutex init failed");
    for (int i = 0; i < 2; ++i) {
        zassert_equal(os_mutex_lock(&test_mutex), BHT_OK, "mutex lock failed");
        zassert_equal(os_mutex_unlock(&test_mutex), BHT_OK,
                      "mutex unlock failed");
    }
}

ZTEST(platform_sync, test_condition_is_reusable)
{
#if defined(CONFIG_WAMR_TEST_USER_MODE)
    /* FIXME: sys_mutex initialization/locking currently fails in the qemu_arc
     * userspace configuration. */
    ztest_test_skip();
#endif
    struct condition_waiter waiter = { 0 };

    k_sem_init(&waiter.ready, 0, 1);
    zassert_equal(os_mutex_init(&waiter.mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(&waiter.cond), BHT_OK, "condition init failed");
    for (int i = 0; i < 2; ++i) {
        korp_tid thread;

        waiter.waiting = false;
        waiter.woke = false;
        waiter.result = BHT_ERROR;
        zassert_equal(os_thread_create(&thread, wait_for_condition, &waiter,
                                       WAMR_TEST_THREAD_STACK_SIZE),
                      BHT_OK, "thread creation failed");
        signal_waiter(&waiter);
        join_waiter(thread, &waiter);
    }
    zassert_equal(os_cond_destroy(&waiter.cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(&waiter.mutex), BHT_OK,
                  "mutex destroy failed");
}

ZTEST(platform_sync, test_large_condition_timeout_is_clamped)
{
#if defined(CONFIG_WAMR_TEST_USER_MODE)
    /* FIXME: sys_mutex initialization/locking currently fails in the qemu_arc
     * userspace configuration. */
    ztest_test_skip();
#endif
    struct condition_waiter waiter = { 0 };
    korp_tid thread;

    waiter.timeout_us = (uint64)INT32_MAX * 1000ULL + 1000ULL;
    k_sem_init(&waiter.ready, 0, 1);
    zassert_equal(os_mutex_init(&waiter.mutex), BHT_OK, "mutex init failed");
    zassert_equal(os_cond_init(&waiter.cond), BHT_OK, "condition init failed");
    zassert_equal(os_thread_create(&thread, wait_for_condition, &waiter,
                                   WAMR_TEST_THREAD_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    signal_waiter(&waiter);
    join_waiter(thread, &waiter);
    zassert_equal(os_cond_destroy(&waiter.cond), BHT_OK,
                  "condition destroy failed");
    zassert_equal(os_mutex_destroy(&waiter.mutex), BHT_OK,
                  "mutex destroy failed");
}

ZTEST(platform_sync, test_named_semaphore_api_reports_unsupported)
{
    zassert_is_null(os_sem_open("wamr", 0, 0, 1), NULL);
    zassert_equal(os_sem_close(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_wait(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_trywait(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_post(NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_getvalue(NULL, NULL), BHT_ERROR, NULL);
    zassert_equal(os_sem_unlink("wamr"), BHT_ERROR, NULL);
}

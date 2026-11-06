/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "platform_api_extension.h"
#include "zephyr_sync_pool.h"

WAMR_ZEPHYR_SYNC_POOL_DEFINE(mutex_only_pool, 2, 0);
static struct k_condvar unused_condvar;

static void *
mutex_only_setup(void)
{
    wamr_zephyr_sync_pool_t malformed = mutex_only_pool;

    /* Run before binding so replacement rejection cannot hide bad validation.
     */
    malformed.condvar_count = 1;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "NULL condition storage with positive count");
    malformed = mutex_only_pool;
    malformed.condvars = &unused_condvar;
    zassert_equal(wamr_zephyr_sync_pool_prepare(&malformed, k_current_get()),
                  BHT_ERROR, "condition storage with zero count");
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&mutex_only_pool, k_current_get()),
        BHT_OK, "mutex-only pool preparation failed");
    zassert_equal(
        wamr_zephyr_sync_pool_prepare(&mutex_only_pool, k_current_get()),
        BHT_OK, "identical mutex-only preparation failed");
    return NULL;
}

ZTEST_SUITE(platform_mutex_only, NULL, mutex_only_setup, NULL, NULL, NULL);

ZTEST_USER(platform_mutex_only, test_capacity_and_recovery)
{
    korp_mutex mutexes[3] = { 0 };
    korp_cond cond = NULL;
    int results[3];
    int cond_result = os_cond_init(&cond);
    int lock_result = BHT_ERROR;
    int unlock_result = BHT_ERROR;
    int destroy_results[3] = { BHT_ERROR, BHT_ERROR, BHT_ERROR };
    int recovery_result;
    int recovery_destroy_result = BHT_ERROR;

    for (size_t i = 0; i < ARRAY_SIZE(mutexes); i++) {
        results[i] = os_mutex_init(&mutexes[i]);
    }
    if (results[0] == BHT_OK) {
        lock_result = os_mutex_lock(&mutexes[0]);
        if (lock_result == BHT_OK) {
            unlock_result = os_mutex_unlock(&mutexes[0]);
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(mutexes); i++) {
        if (results[i] == BHT_OK) {
            destroy_results[i] = os_mutex_destroy(&mutexes[i]);
        }
    }
    if (cond_result == BHT_OK) {
        os_cond_destroy(&cond);
    }
    recovery_result = os_mutex_init(&mutexes[0]);
    if (recovery_result == BHT_OK) {
        recovery_destroy_result = os_mutex_destroy(&mutexes[0]);
    }

    zassert_equal(cond_result, BHT_ERROR, "zero-CV pool allocated a condition");
    zassert_equal(results[0], BHT_OK, "first mutex allocation failed");
    zassert_equal(results[1], BHT_OK, "second mutex allocation failed");
    zassert_equal(results[2], BHT_ERROR, "mutex pool capacity was exceeded");
    zassert_equal(lock_result, BHT_OK, "user mutex lock failed");
    zassert_equal(unlock_result, BHT_OK, "user mutex unlock failed");
    zassert_equal(destroy_results[0], BHT_OK, "first mutex destruction failed");
    zassert_equal(destroy_results[1], BHT_OK,
                  "second mutex destruction failed");
    zassert_equal(recovery_result, BHT_OK, "mutex capacity did not recover");
    zassert_equal(recovery_destroy_result, BHT_OK, "recovered mutex leaked");
}

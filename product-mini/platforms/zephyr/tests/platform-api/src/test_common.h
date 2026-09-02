/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H
#define WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H

#include <stdint.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "wasm_export.h"

#define TEST_POOL_SIZE (128U * 1024U)

#if defined(CONFIG_WAMR_TEST_USER_MODE)
#define WAMR_CONTEXT_TEST(suite, name) ZTEST_USER(suite, name)
#define WAMR_CONTEXT_TEST_F(suite, name) ZTEST_USER_F(suite, name)
#else
#define WAMR_CONTEXT_TEST(suite, name) ZTEST(suite, name)
#define WAMR_CONTEXT_TEST_F(suite, name) ZTEST_F(suite, name)
#endif

static uint8_t test_pool[TEST_POOL_SIZE] __aligned(8);

static void
pool_before(void *fixture)
{
    RuntimeInitArgs args = { 0 };

    ARG_UNUSED(fixture);
    memset(test_pool, 0xA5, sizeof(test_pool));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = test_pool;
    args.mem_alloc_option.pool.heap_size = sizeof(test_pool);
    zassert_true(wasm_runtime_full_init(&args), "pool init failed");
}

static void
pool_after(void *fixture)
{
    ARG_UNUSED(fixture);
    wasm_runtime_destroy();
}

#endif /* WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H */

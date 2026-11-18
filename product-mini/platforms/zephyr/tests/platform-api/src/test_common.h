/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H
#define WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "platform_api_extension.h"
#include "wasm_export.h"

#define TEST_POOL_SIZE (128U * 1024U)
#define WAMR_TEST_THREAD_COUNT 4U
#define WAMR_TEST_THREAD_STACK_SIZE 2048U

int
wamr_test_thread_pool_prepare(void);
void *
wamr_test_thread_pool_setup(void);
int
wamr_test_sync_pool_prepare(void);
void
pool_before(void *fixture);
void
pool_after(void *fixture);
struct k_mutex *
wamr_test_sync_mutex(void);
struct k_condvar *
wamr_test_sync_condvar(void);

#if defined(CONFIG_WAMR_TEST_USER_MODE)
#define WAMR_CONTEXT_TEST(suite, name) ZTEST_USER(suite, name)
#define WAMR_CONTEXT_TEST_F(suite, name) ZTEST_USER_F(suite, name)
#else
#define WAMR_CONTEXT_TEST(suite, name) ZTEST(suite, name)
#define WAMR_CONTEXT_TEST_F(suite, name) ZTEST_F(suite, name)
#endif

#endif /* WAMR_ZEPHYR_PLATFORM_API_TEST_COMMON_H */

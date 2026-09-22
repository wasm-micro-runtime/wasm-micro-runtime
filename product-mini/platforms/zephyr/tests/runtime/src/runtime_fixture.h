/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_RUNTIME_FIXTURE_H
#define WAMR_ZEPHYR_RUNTIME_FIXTURE_H

#include <stdint.h>

#include <zephyr/toolchain.h>

#include "runtime_user_workflow.h"

#define RUNTIME_POOL_SIZE WAMR_TEST_RUNTIME_POOL_SIZE

struct loaded_runtime {
    uint8_t pool[RUNTIME_POOL_SIZE] __aligned(8);
    struct wamr_test_runtime runtime;
};

#endif /* WAMR_ZEPHYR_RUNTIME_FIXTURE_H */

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_TEST_RUNTIME_USER_WORKFLOW_H
#define WAMR_ZEPHYR_TEST_RUNTIME_USER_WORKFLOW_H

#include <stdbool.h>
#include <stdint.h>

#include "wasm_export.h"

#define WAMR_TEST_RUNTIME_POOL_SIZE (128U * 1024U)
#define WAMR_TEST_ERROR_SIZE 128U
#define WAMR_TEST_WASM_STACK_SIZE 4096U
#define WAMR_TEST_WASM_HEAP_SIZE 4096U

struct wamr_test_runtime {
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;
    bool initialized;
};

bool
wamr_test_runtime_init(struct wamr_test_runtime *runtime, uint8_t *pool,
                       uint32_t pool_size);
bool
wamr_test_runtime_load(struct wamr_test_runtime *runtime, uint8_t *module_bytes,
                       uint32_t module_size, char *error, uint32_t error_size);
bool
wamr_test_runtime_start_copy(struct wamr_test_runtime *runtime, uint8_t *pool,
                             uint32_t pool_size, const uint8_t *source,
                             uint32_t source_size, char *error,
                             uint32_t error_size);
bool
wamr_test_runtime_start_copy_with_heap(struct wamr_test_runtime *runtime,
                                       uint8_t *pool, uint32_t pool_size,
                                       const uint8_t *source,
                                       uint32_t source_size,
                                       uint32_t wasm_heap_size, char *error,
                                       uint32_t error_size);
bool
wamr_test_runtime_call_add(struct wamr_test_runtime *runtime, uint32_t *result);
bool
wamr_test_run_add_copy(struct wamr_test_runtime *runtime, uint8_t *pool,
                       uint32_t pool_size, const uint8_t *source,
                       uint32_t source_size, uint32_t *result, char *error,
                       uint32_t error_size);
void
wamr_test_runtime_stop(struct wamr_test_runtime *runtime);

#endif /* WAMR_ZEPHYR_TEST_RUNTIME_USER_WORKFLOW_H */

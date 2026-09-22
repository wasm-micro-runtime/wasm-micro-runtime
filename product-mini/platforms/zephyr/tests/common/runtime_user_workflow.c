/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <string.h>

#include "runtime_user_workflow.h"

bool
wamr_test_runtime_init(struct wamr_test_runtime *runtime, uint8_t *pool,
                       uint32_t pool_size)
{
    RuntimeInitArgs args = { 0 };
    bool initialized;

    runtime->module = NULL;
    runtime->instance = NULL;
    runtime->exec_env = NULL;
    runtime->initialized = false;

    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = pool_size;
    args.running_mode = Mode_Interp;
    initialized = wasm_runtime_full_init(&args);
    runtime->initialized = initialized;
    return initialized;
}

static bool
wamr_test_runtime_load_with_heap(struct wamr_test_runtime *runtime,
                                 uint8_t *module_bytes, uint32_t module_size,
                                 uint32_t wasm_heap_size, char *error,
                                 uint32_t error_size)
{
    runtime->module =
        wasm_runtime_load(module_bytes, module_size, error, error_size);
    if (runtime->module == NULL) {
        wamr_test_runtime_stop(runtime);
        return false;
    }

    runtime->instance =
        wasm_runtime_instantiate(runtime->module, WAMR_TEST_WASM_STACK_SIZE,
                                 wasm_heap_size, error, error_size);
    if (runtime->instance == NULL) {
        wamr_test_runtime_stop(runtime);
        return false;
    }

    runtime->exec_env = wasm_runtime_create_exec_env(runtime->instance,
                                                     WAMR_TEST_WASM_STACK_SIZE);
    if (runtime->exec_env == NULL) {
        wamr_test_runtime_stop(runtime);
        return false;
    }

    return true;
}

bool
wamr_test_runtime_load(struct wamr_test_runtime *runtime, uint8_t *module_bytes,
                       uint32_t module_size, char *error, uint32_t error_size)
{
    return wamr_test_runtime_load_with_heap(runtime, module_bytes, module_size,
                                            WAMR_TEST_WASM_HEAP_SIZE, error,
                                            error_size);
}

bool
wamr_test_runtime_start_copy_with_heap(struct wamr_test_runtime *runtime,
                                       uint8_t *pool, uint32_t pool_size,
                                       const uint8_t *source,
                                       uint32_t source_size,
                                       uint32_t wasm_heap_size, char *error,
                                       uint32_t error_size)
{
    uint32_t image_size = (source_size + 7U) & ~7U;
    uint32_t heap_size;
    uint8_t *module_bytes;

    runtime->module = NULL;
    runtime->instance = NULL;
    runtime->exec_env = NULL;
    runtime->initialized = false;
    if (source_size == 0U || image_size < source_size
        || image_size >= pool_size) {
        return false;
    }

    heap_size = pool_size - image_size;
    module_bytes = &pool[heap_size];
    memcpy(module_bytes, source, source_size);

    if (!wamr_test_runtime_init(runtime, pool, heap_size)) {
        return false;
    }

    /* The public loader accepts a writable buffer and may modify its bytes. */
    return wamr_test_runtime_load_with_heap(runtime, module_bytes, source_size,
                                            wasm_heap_size, error, error_size);
}

bool
wamr_test_runtime_start_copy(struct wamr_test_runtime *runtime, uint8_t *pool,
                             uint32_t pool_size, const uint8_t *source,
                             uint32_t source_size, char *error,
                             uint32_t error_size)
{
    return wamr_test_runtime_start_copy_with_heap(
        runtime, pool, pool_size, source, source_size, WAMR_TEST_WASM_HEAP_SIZE,
        error, error_size);
}

bool
wamr_test_runtime_call_add(struct wamr_test_runtime *runtime, uint32_t *result)
{
    wasm_function_inst_t add =
        wasm_runtime_lookup_function(runtime->instance, "add");
    uint32_t argv[2] = { 20U, 22U };

    if (add == NULL
        || !wasm_runtime_call_wasm(runtime->exec_env, add, 2U, argv)) {
        return false;
    }

    *result = argv[0];
    return true;
}

bool
wamr_test_run_add_copy(struct wamr_test_runtime *runtime, uint8_t *pool,
                       uint32_t pool_size, const uint8_t *source,
                       uint32_t source_size, uint32_t *result, char *error,
                       uint32_t error_size)
{
    bool started = wamr_test_runtime_start_copy(
        runtime, pool, pool_size, source, source_size, error, error_size);
    bool called = started && wamr_test_runtime_call_add(runtime, result);

    if (started) {
        wamr_test_runtime_stop(runtime);
    }
    return called;
}

void
wamr_test_runtime_stop(struct wamr_test_runtime *runtime)
{
    if (runtime->exec_env != NULL) {
        wasm_runtime_destroy_exec_env(runtime->exec_env);
        runtime->exec_env = NULL;
    }
    if (runtime->instance != NULL) {
        wasm_runtime_deinstantiate(runtime->instance);
        runtime->instance = NULL;
    }
    if (runtime->module != NULL) {
        wasm_runtime_unload(runtime->module);
        runtime->module = NULL;
    }
    if (runtime->initialized) {
        wasm_runtime_destroy();
        runtime->initialized = false;
    }
}

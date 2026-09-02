/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "runtime_fixture.h"
#include "wasm_fixtures.h"

#define ERROR_BUFFER_SIZE 128U
#define WASM_STACK_SIZE 4096U
#define WASM_HEAP_SIZE 4096U
#define SMALL_POOL_SIZE 1024U

static struct loaded_runtime runtime_fixture;
static uint8_t small_pool[SMALL_POOL_SIZE] __aligned(8);

bool
runtime_start(struct loaded_runtime *rt, const uint8_t *bytes, uint32_t size,
              char *error, uint32_t error_size)
{
    RuntimeInitArgs args = { 0 };
    uint32_t image_size = (size + 7U) & ~7U;
    uint32_t heap_size;
    uint8_t *module_bytes;

    rt->module = NULL;
    rt->instance = NULL;
    rt->exec_env = NULL;

    if (size == 0U || image_size < size || image_size >= sizeof(rt->pool)) {
        return false;
    }

    heap_size = sizeof(rt->pool) - image_size;
    module_bytes = &rt->pool[heap_size];
    memcpy(module_bytes, bytes, size);

    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = rt->pool;
    args.mem_alloc_option.pool.heap_size = heap_size;
    args.running_mode = Mode_Interp;
    if (!wasm_runtime_full_init(&args)) {
        return false;
    }

    /* The public loader accepts a writable buffer and may modify its bytes. */
    rt->module = wasm_runtime_load(module_bytes, size, error, error_size);
    if (rt->module == NULL) {
        runtime_stop(rt);
        return false;
    }

    rt->instance = wasm_runtime_instantiate(rt->module, WASM_STACK_SIZE,
                                            WASM_HEAP_SIZE, error, error_size);
    if (rt->instance == NULL) {
        runtime_stop(rt);
        return false;
    }

    rt->exec_env = wasm_runtime_create_exec_env(rt->instance, WASM_STACK_SIZE);
    if (rt->exec_env == NULL) {
        runtime_stop(rt);
        return false;
    }

    return true;
}

void
runtime_stop(struct loaded_runtime *rt)
{
    if (rt->exec_env != NULL) {
        wasm_runtime_destroy_exec_env(rt->exec_env);
        rt->exec_env = NULL;
    }
    if (rt->instance != NULL) {
        wasm_runtime_deinstantiate(rt->instance);
        rt->instance = NULL;
    }
    if (rt->module != NULL) {
        wasm_runtime_unload(rt->module);
        rt->module = NULL;
    }
    wasm_runtime_destroy();
}

static bool
call_add(struct loaded_runtime *rt, uint32_t *result)
{
    wasm_function_inst_t add =
        wasm_runtime_lookup_function(rt->instance, "add");
    uint32_t argv[2] = { 20U, 22U };

    if (add == NULL || !wasm_runtime_call_wasm(rt->exec_env, add, 2U, argv)) {
        return false;
    }

    *result = argv[0];
    return true;
}

static bool
run_add_lifecycle(uint32_t *result)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    bool started = runtime_start(&runtime_fixture, wasm_add, sizeof(wasm_add),
                                 error, sizeof(error));
    bool called = started && call_add(&runtime_fixture, result);

    if (started) {
        runtime_stop(&runtime_fixture);
    }
    return called;
}

ZTEST_SUITE(runtime_interpreter_pool, NULL, NULL, NULL, NULL, NULL);

ZTEST(runtime_interpreter_pool, test_initialize_and_destroy)
{
    RuntimeInitArgs args = { 0 };
    bool initialized;

    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = runtime_fixture.pool;
    args.mem_alloc_option.pool.heap_size = sizeof(runtime_fixture.pool);
    args.running_mode = Mode_Interp;
    initialized = wasm_runtime_full_init(&args);
    if (initialized) {
        wasm_runtime_destroy();
    }

    zassert_true(initialized, "interpreter pool initialization failed");
}

ZTEST(runtime_interpreter_pool, test_load_and_instantiate_valid_module)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    bool started = runtime_start(&runtime_fixture, wasm_add, sizeof(wasm_add),
                                 error, sizeof(error));
    bool loaded = started && runtime_fixture.module != NULL;
    bool instantiated = started && runtime_fixture.instance != NULL;
    bool exec_env_created = started && runtime_fixture.exec_env != NULL;

    if (started) {
        runtime_stop(&runtime_fixture);
    }

    zassert_true(started, "valid module lifecycle failed: %s", error);
    zassert_true(loaded, "valid module was not loaded");
    zassert_true(instantiated, "valid module was not instantiated");
    zassert_true(exec_env_created, "execution environment was not created");
    zassert_is_null(runtime_fixture.module, "module handle was not cleared");
    zassert_is_null(runtime_fixture.instance,
                    "instance handle was not cleared");
    zassert_is_null(runtime_fixture.exec_env,
                    "execution environment handle was not cleared");
}

ZTEST(runtime_interpreter_pool, test_add_export_returns_expected_result)
{
    uint32_t result = 0U;
    bool called = run_add_lifecycle(&result);

    zassert_true(called, "add export call failed");
    zassert_equal(result, 42U, "add export returned %u", result);
}

ZTEST(runtime_interpreter_pool, test_trap_sets_exception)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    uint32_t unused_argv[1] = { 0U };
    bool started = runtime_start(&runtime_fixture, wasm_trap, sizeof(wasm_trap),
                                 error, sizeof(error));
    wasm_function_inst_t trap = NULL;
    bool call_succeeded = false;
    bool exception_set = false;

    if (started) {
        trap = wasm_runtime_lookup_function(runtime_fixture.instance, "trap");
        if (trap != NULL) {
            call_succeeded = wasm_runtime_call_wasm(runtime_fixture.exec_env,
                                                    trap, 0U, unused_argv);
            exception_set =
                wasm_runtime_get_exception(runtime_fixture.instance) != NULL;
        }
        runtime_stop(&runtime_fixture);
    }

    zassert_true(started, "trapping module lifecycle failed: %s", error);
    zassert_not_null(trap, "trap export was not found");
    zassert_false(call_succeeded, "unreachable instruction did not trap");
    zassert_true(exception_set, "trap did not set an exception");
}

ZTEST(runtime_interpreter_pool, test_complete_lifecycle_runs_twice)
{
    uint32_t first_result = 0U;
    uint32_t second_result = 0U;
    bool first = run_add_lifecycle(&first_result);
    bool second = run_add_lifecycle(&second_result);

    zassert_true(first, "first lifecycle failed");
    zassert_equal(first_result, 42U, "first lifecycle returned %u",
                  first_result);
    zassert_true(second, "second lifecycle failed");
    zassert_equal(second_result, 42U, "second lifecycle returned %u",
                  second_result);
}

ZTEST(runtime_interpreter_pool, test_malformed_module_has_diagnostic)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    bool started = runtime_start(&runtime_fixture, malformed_wasm,
                                 sizeof(malformed_wasm), error, sizeof(error));

    if (started) {
        runtime_stop(&runtime_fixture);
    }

    zassert_false(started, "malformed module was accepted");
    zassert_not_equal(error[0], '\0', "malformed module had no diagnostic");
}

ZTEST(runtime_interpreter_pool, test_missing_export_is_clean_failure)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    bool started = runtime_start(&runtime_fixture, wasm_add, sizeof(wasm_add),
                                 error, sizeof(error));
    wasm_function_inst_t missing = NULL;
    bool exception_set = false;

    if (started) {
        missing =
            wasm_runtime_lookup_function(runtime_fixture.instance, "missing");
        exception_set =
            wasm_runtime_get_exception(runtime_fixture.instance) != NULL;
        runtime_stop(&runtime_fixture);
    }

    zassert_true(started, "valid module lifecycle failed: %s", error);
    zassert_is_null(missing, "missing export was unexpectedly found");
    zassert_false(exception_set, "missing lookup set an unrelated exception");
}

ZTEST(runtime_interpreter_pool, test_small_pool_failure_does_not_poison_retry)
{
    RuntimeInitArgs args = { 0 };
    char error[ERROR_BUFFER_SIZE] = { 0 };
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    uint32_t image_size = (sizeof(wasm_add) + 7U) & ~7U;
    uint32_t heap_size = sizeof(small_pool) - image_size;
    uint8_t *module_bytes = &small_pool[heap_size];
    bool small_initialized;
    bool small_failed;
    uint32_t retry_result = 0U;
    bool retry_succeeded;

    memcpy(module_bytes, wasm_add, sizeof(wasm_add));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = small_pool;
    args.mem_alloc_option.pool.heap_size = heap_size;
    args.running_mode = Mode_Interp;
    small_initialized = wasm_runtime_full_init(&args);
    if (small_initialized) {
        module = wasm_runtime_load(module_bytes, sizeof(wasm_add), error,
                                   sizeof(error));
        if (module != NULL) {
            instance = wasm_runtime_instantiate(
                module, WASM_STACK_SIZE, WASM_HEAP_SIZE, error, sizeof(error));
        }
        small_failed = module == NULL || instance == NULL;
        if (instance != NULL) {
            wasm_runtime_deinstantiate(instance);
        }
        if (module != NULL) {
            wasm_runtime_unload(module);
        }
        wasm_runtime_destroy();
    }
    else {
        small_failed = false;
    }

    retry_succeeded = run_add_lifecycle(&retry_result);

    zassert_true(small_initialized, "small pool initialization failed early");
    zassert_true(small_failed, "small pool unexpectedly instantiated module");
    zassert_true(retry_succeeded, "normal pool retry failed");
    zassert_equal(retry_result, 42U, "normal pool retry returned %u",
                  retry_result);
}

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/ztest.h>

#include "runtime_fixture.h"
#include "wasm_fixtures.h"

#define SMALL_POOL_SIZE 1024U

static struct loaded_runtime runtime_fixture;
static uint8_t small_pool[SMALL_POOL_SIZE] __aligned(8);

static bool
run_add_lifecycle(uint32_t *result)
{
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };

    return wamr_test_run_add_copy(
        &runtime_fixture.runtime, runtime_fixture.pool,
        sizeof(runtime_fixture.pool), wasm_add, sizeof(wasm_add), result, error,
        sizeof(error));
}

ZTEST_SUITE(runtime_interpreter_pool, NULL, NULL, NULL, NULL, NULL);

ZTEST(runtime_interpreter_pool, test_initialize_and_destroy)
{
    struct wamr_test_runtime empty_runtime = { 0 };
    struct wamr_test_runtime failed_runtime = { 0 };
    uint8_t failed_pool[64] __aligned(8) = { 0 };
    bool failed_initialized;
    bool initialized;

    wamr_test_runtime_stop(&empty_runtime);
    zassert_false(empty_runtime.initialized,
                  "empty runtime acquired initialization state");

    failed_initialized = wamr_test_runtime_init(&failed_runtime, failed_pool,
                                                sizeof(failed_pool));
    wamr_test_runtime_stop(&failed_runtime);

    zassert_false(failed_initialized, "undersized pool initialized runtime");
    zassert_false(failed_runtime.initialized,
                  "failed init retained initialization state");

    initialized =
        wamr_test_runtime_init(&runtime_fixture.runtime, runtime_fixture.pool,
                               sizeof(runtime_fixture.pool));
    zassert_true(runtime_fixture.runtime.initialized,
                 "successful init did not retain initialization state");
    if (initialized) {
        wamr_test_runtime_stop(&runtime_fixture.runtime);
    }

    zassert_true(initialized, "interpreter pool initialization failed");
    zassert_false(runtime_fixture.runtime.initialized,
                  "runtime initialization state was not cleared");
}

ZTEST(runtime_interpreter_pool, test_load_and_instantiate_valid_module)
{
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    bool started = wamr_test_runtime_start_copy(
        &runtime_fixture.runtime, runtime_fixture.pool,
        sizeof(runtime_fixture.pool), wasm_add, sizeof(wasm_add), error,
        sizeof(error));
    bool loaded = started && runtime_fixture.runtime.module != NULL;
    bool instantiated = started && runtime_fixture.runtime.instance != NULL;
    bool exec_env_created = started && runtime_fixture.runtime.exec_env != NULL;

    if (started) {
        wamr_test_runtime_stop(&runtime_fixture.runtime);
    }

    zassert_true(started, "valid module lifecycle failed: %s", error);
    zassert_true(loaded, "valid module was not loaded");
    zassert_true(instantiated, "valid module was not instantiated");
    zassert_true(exec_env_created, "execution environment was not created");
    zassert_is_null(runtime_fixture.runtime.module,
                    "module handle was not cleared");
    zassert_is_null(runtime_fixture.runtime.instance,
                    "instance handle was not cleared");
    zassert_is_null(runtime_fixture.runtime.exec_env,
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
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    uint32_t unused_argv[1] = { 0U };
    bool started = wamr_test_runtime_start_copy(
        &runtime_fixture.runtime, runtime_fixture.pool,
        sizeof(runtime_fixture.pool), wasm_trap, sizeof(wasm_trap), error,
        sizeof(error));
    wasm_function_inst_t trap = NULL;
    bool call_succeeded = false;
    bool exception_set = false;

    if (started) {
        trap = wasm_runtime_lookup_function(runtime_fixture.runtime.instance,
                                            "trap");
        if (trap != NULL) {
            call_succeeded = wasm_runtime_call_wasm(
                runtime_fixture.runtime.exec_env, trap, 0U, unused_argv);
            exception_set =
                wasm_runtime_get_exception(runtime_fixture.runtime.instance)
                != NULL;
        }
        wamr_test_runtime_stop(&runtime_fixture.runtime);
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
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    bool started = wamr_test_runtime_start_copy(
        &runtime_fixture.runtime, runtime_fixture.pool,
        sizeof(runtime_fixture.pool), malformed_wasm, sizeof(malformed_wasm),
        error, sizeof(error));

    if (started) {
        wamr_test_runtime_stop(&runtime_fixture.runtime);
    }

    zassert_false(started, "malformed module was accepted");
    zassert_not_equal(error[0], '\0', "malformed module had no diagnostic");
}

ZTEST(runtime_interpreter_pool, test_missing_export_is_clean_failure)
{
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    bool started = wamr_test_runtime_start_copy(
        &runtime_fixture.runtime, runtime_fixture.pool,
        sizeof(runtime_fixture.pool), wasm_add, sizeof(wasm_add), error,
        sizeof(error));
    wasm_function_inst_t missing = NULL;
    bool exception_set = false;

    if (started) {
        missing = wasm_runtime_lookup_function(runtime_fixture.runtime.instance,
                                               "missing");
        exception_set =
            wasm_runtime_get_exception(runtime_fixture.runtime.instance)
            != NULL;
        wamr_test_runtime_stop(&runtime_fixture.runtime);
    }

    zassert_true(started, "valid module lifecycle failed: %s", error);
    zassert_is_null(missing, "missing export was unexpectedly found");
    zassert_false(exception_set, "missing lookup set an unrelated exception");
}

ZTEST(runtime_interpreter_pool, test_small_pool_failure_does_not_poison_retry)
{
    struct wamr_test_runtime small_runtime = { 0 };
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    bool small_initialized;
    bool small_failed;
    uint32_t retry_result = 0U;
    bool retry_succeeded;

    small_failed = !wamr_test_runtime_start_copy(
        &small_runtime, small_pool, sizeof(small_pool), wasm_add,
        sizeof(wasm_add), error, sizeof(error));
    small_initialized = small_failed && error[0] != '\0';
    if (!small_failed) {
        wamr_test_runtime_stop(&small_runtime);
    }

    retry_succeeded = run_add_lifecycle(&retry_result);

    zassert_true(small_initialized, "small pool initialization failed early");
    zassert_true(small_failed, "small pool unexpectedly instantiated module");
    zassert_true(retry_succeeded, "normal pool retry failed");
    zassert_equal(retry_result, 42U, "normal pool retry returned %u",
                  retry_result);
}

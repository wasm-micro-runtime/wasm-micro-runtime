/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "expected_fault.h"
#include "fault_fixture.h"
#include "wasm_fixtures.h"

static void
wasm_oob_worker(struct wamr_fault_results *results)
{
    struct wamr_test_runtime runtime = { 0 };
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    wasm_function_inst_t oob = NULL;
    uint32_t unused_argv[1] = { 0U };
    bool call_succeeded = false;
    const char *exception = NULL;
    bool started = wamr_test_runtime_start_copy_with_heap(
        &runtime, wamr_fault_pool_buffer(), WAMR_TEST_RUNTIME_POOL_SIZE,
        wasm_oob, sizeof(wasm_oob), 0U, error, sizeof(error));

    if (started) {
        oob = wasm_runtime_lookup_function(runtime.instance, "oob");
        if (oob != NULL) {
            call_succeeded =
                wasm_runtime_call_wasm(runtime.exec_env, oob, 0U, unused_argv);
        }
        exception = wasm_runtime_get_exception(runtime.instance);
        if (exception != NULL) {
            strncpy(results->exception, exception,
                    sizeof(results->exception) - 1U);
        }
        results->runtime_trap_observed = !call_succeeded && exception != NULL;
        wamr_test_runtime_stop(&runtime);
    }

    results->workflow_completed = started;
}

ZTEST_F(wamr_usermode_faults, test_wasm_oob_is_runtime_trap_not_mpu_fault)
{
    struct expected_fault_snapshot snapshot;

    wamr_fault_run_normal(WAMR_FAULT_COMPLETE_DOMAIN, wasm_oob_worker);
    snapshot = expected_fault_snapshot();

    zassert_true(fixture->results->workflow_completed,
                 "linear-memory OOB workflow did not start");
    zassert_true(fixture->results->runtime_trap_observed,
                 "linear-memory OOB did not produce a WAMR runtime trap");
    zassert_not_equal(fixture->results->exception[0], '\0',
                      "linear-memory OOB runtime exception was empty");
    zassert_not_null(
        strstr(fixture->results->exception, "out of bounds memory access"),
        "linear-memory OOB returned an unexpected exception: %s",
        fixture->results->exception);
    zassert_false(snapshot.observed,
                  "linear-memory OOB was accepted as an MPU fault");
    wamr_fault_assert_recovery(fixture->results);
}

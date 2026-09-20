/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/ztest.h>

#include "fault_fixture.h"
#include "wasm_fixtures.h"

static bool
fault_runtime_init(struct wamr_fault_results *results)
{
    RuntimeInitArgs args = { 0 };

    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = wamr_fault_pool_buffer();
    args.mem_alloc_option.pool.heap_size = WAMR_TEST_RUNTIME_POOL_SIZE;
    args.running_mode = Mode_Interp;
    results->runtime_initialized = wasm_runtime_full_init(&args);
    return results->runtime_initialized;
}

static void
fault_runtime_cleanup(struct wamr_fault_results *results)
{
    if (results->runtime_initialized) {
        wasm_runtime_destroy();
        results->runtime_initialized = false;
    }
}

static void
full_init_worker(struct wamr_fault_results *results)
{
    if (fault_runtime_init(results)) {
        fault_runtime_cleanup(results);
    }
}

static void
load_module_worker(struct wamr_fault_results *results)
{
    wasm_module_t module;

    if (!fault_runtime_init(results)) {
        return;
    }

    module = wasm_runtime_load(wamr_fault_module_buffer(), sizeof(wasm_add),
                               results->exception, sizeof(results->exception));
    if (module != NULL) {
        wasm_runtime_unload(module);
    }
    fault_runtime_cleanup(results);
}

ZTEST_F(wamr_usermode_faults, test_wamr_pool_partition_is_required)
{
    bool initialized;

    wamr_fault_run_expected(WAMR_FAULT_COMPLETE_DOMAIN & ~WAMR_FAULT_PART_POOL,
                            full_init_worker);

    initialized = fixture->results->runtime_initialized;
    fault_runtime_cleanup(fixture->results);
    zassert_false(initialized,
                  "pool fault occurred after WAMR initialization completed");
    wamr_fault_assert_recovery(fixture->results);
}

ZTEST_F(wamr_usermode_faults, test_writable_module_partition_is_required)
{
    wamr_fault_run_expected(WAMR_FAULT_COMPLETE_DOMAIN
                                & ~WAMR_FAULT_PART_MODULE,
                            load_module_worker);

    zassert_true(fixture->results->runtime_initialized,
                 "loader fault occurred before WAMR initialization completed");
    fault_runtime_cleanup(fixture->results);
    wamr_fault_assert_recovery(fixture->results);
}

ZTEST_F(wamr_usermode_faults, test_wamr_globals_partition_is_required)
{
    bool initialized;

    wamr_fault_run_expected(WAMR_FAULT_COMPLETE_DOMAIN & ~WAMR_FAULT_PART_WAMR,
                            full_init_worker);

    initialized = fixture->results->runtime_initialized;
    fault_runtime_cleanup(fixture->results);
    zassert_false(
        initialized,
        "WAMR-global fault occurred after WAMR initialization completed");
    wamr_fault_assert_recovery(fixture->results);
}

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>

#include <zephyr/ztest.h>

#include "fault_fixture.h"

static volatile uint32_t supervisor_runtime_result;

static void
publish_wamr_result_worker(struct wamr_fault_results *results)
{
    uint32_t value = 0U;

    if (!wamr_fault_run_add(&value) || value != 42U) {
        return;
    }

    results->workflow_completed = true;
    supervisor_runtime_result = value;
}

ZTEST_SUITE(wamr_usermode_faults, NULL, wamr_fault_suite_setup,
            wamr_fault_before, wamr_fault_after, NULL);

ZTEST_F(wamr_usermode_faults, test_user_cannot_access_supervisor_runtime_state)
{
    supervisor_runtime_result = 0U;

    wamr_fault_run_expected(WAMR_FAULT_COMPLETE_DOMAIN,
                            publish_wamr_result_worker);

    zassert_true(fixture->results->workflow_completed,
                 "WAMR add workflow did not complete before the fault");
    zassert_equal(supervisor_runtime_result, 0U,
                  "user worker published into supervisor runtime state");
    wamr_fault_assert_recovery(fixture->results);
}

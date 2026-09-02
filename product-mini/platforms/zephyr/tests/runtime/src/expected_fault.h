/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_TEST_EXPECTED_FAULT_H
#define WAMR_ZEPHYR_TEST_EXPECTED_FAULT_H

#include <stdbool.h>

#include <zephyr/kernel.h>

struct expected_fault_state {
    bool armed;
    bool observed;
    k_tid_t expected_tid;
    unsigned int expected_reason;
    struct k_sem *done;
};

void
expected_fault_arm(k_tid_t tid, unsigned int reason, struct k_sem *done);
bool
expected_fault_observed(void);
void
expected_fault_disarm(void);

#endif

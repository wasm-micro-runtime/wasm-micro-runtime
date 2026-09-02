/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>

#include "expected_fault.h"

static struct expected_fault_state expected_fault;

void
expected_fault_arm(k_tid_t tid, unsigned int reason, struct k_sem *done)
{
    expected_fault.armed = true;
    expected_fault.observed = false;
    expected_fault.expected_tid = tid;
    expected_fault.expected_reason = reason;
    expected_fault.done = done;
}

bool
expected_fault_observed(void)
{
    return expected_fault.observed;
}

void
expected_fault_disarm(void)
{
    expected_fault.armed = false;
    expected_fault.observed = false;
    expected_fault.expected_tid = NULL;
    expected_fault.expected_reason = 0U;
    expected_fault.done = NULL;
}

void
k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(esf);

    if (reason == K_ERR_KERNEL_PANIC || !expected_fault.armed
        || k_current_get() != expected_fault.expected_tid
        || reason != expected_fault.expected_reason) {
        k_fatal_halt(reason);
    }

    expected_fault.observed = true;
    k_sem_give(expected_fault.done);
}

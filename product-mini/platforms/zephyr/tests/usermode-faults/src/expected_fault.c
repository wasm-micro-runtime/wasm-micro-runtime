/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "expected_fault.h"

struct expected_fault_state {
    atomic_t armed;
    atomic_t observed;
    k_tid_t expected_tid;
    unsigned int expected_reason;
    struct k_sem *done;
};

static struct expected_fault_state expected_fault;

void
expected_fault_arm(k_tid_t tid, unsigned int reason, struct k_sem *done)
{
    atomic_clear(&expected_fault.observed);
    expected_fault.expected_tid = tid;
    expected_fault.expected_reason = reason;
    expected_fault.done = done;
    atomic_set(&expected_fault.armed, 1);
}

struct expected_fault_snapshot
expected_fault_snapshot(void)
{
    return (struct expected_fault_snapshot){
        .armed = atomic_get(&expected_fault.armed) != 0,
        .observed = atomic_get(&expected_fault.observed) != 0,
        .expected_tid = expected_fault.expected_tid,
        .expected_reason = expected_fault.expected_reason,
    };
}

void
expected_fault_disarm(void)
{
    atomic_clear(&expected_fault.armed);
    expected_fault.expected_tid = NULL;
    expected_fault.expected_reason = 0U;
    expected_fault.done = NULL;
    atomic_clear(&expected_fault.observed);
}

void
k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    ARG_UNUSED(esf);

    if (reason == K_ERR_KERNEL_PANIC || !atomic_get(&expected_fault.armed)
        || atomic_get(&expected_fault.observed)
        || k_current_get() != expected_fault.expected_tid
        || reason != expected_fault.expected_reason
        || expected_fault.done == NULL
        || !atomic_cas(&expected_fault.observed, 0, 1)) {
        k_fatal_halt(reason);
    }

    k_sem_give(expected_fault.done);
}

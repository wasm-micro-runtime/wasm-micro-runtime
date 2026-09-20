/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_TEST_FAULT_FIXTURE_H
#define WAMR_ZEPHYR_TEST_FAULT_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "runtime_user_workflow.h"

enum wamr_fault_domain_parts {
    WAMR_FAULT_PART_WAMR = BIT(0),
    WAMR_FAULT_PART_POOL = BIT(1),
    WAMR_FAULT_PART_MODULE = BIT(2),
    WAMR_FAULT_PART_RESULTS = BIT(3),
    WAMR_FAULT_PART_LIBC = BIT(4),
};

#define WAMR_FAULT_COMPLETE_DOMAIN                                        \
    (WAMR_FAULT_PART_WAMR | WAMR_FAULT_PART_POOL | WAMR_FAULT_PART_MODULE \
     | WAMR_FAULT_PART_RESULTS | WAMR_FAULT_PART_LIBC)

struct wamr_fault_results {
    bool worker_was_user;
    bool worker_completed;
    bool workflow_completed;
    bool recovery_completed;
    bool runtime_trap_observed;
    bool runtime_initialized;
    uint32_t value;
    char exception[WAMR_TEST_ERROR_SIZE];
};

struct wamr_usermode_faults_fixture {
    struct wamr_fault_results *results;
};

typedef void (*wamr_fault_worker_t)(struct wamr_fault_results *results);

void *
wamr_fault_suite_setup(void);
void
wamr_fault_before(void *fixture);
void
wamr_fault_after(void *fixture);
void
wamr_fault_run_expected(enum wamr_fault_domain_parts parts,
                        wamr_fault_worker_t worker);
void
wamr_fault_run_normal(enum wamr_fault_domain_parts parts,
                      wamr_fault_worker_t worker);
bool
wamr_fault_run_add(uint32_t *result);
uint8_t *
wamr_fault_pool_buffer(void);
uint8_t *
wamr_fault_module_buffer(void);
void
wamr_fault_assert_recovery(struct wamr_fault_results *results);

#endif /* WAMR_ZEPHYR_TEST_FAULT_FIXTURE_H */

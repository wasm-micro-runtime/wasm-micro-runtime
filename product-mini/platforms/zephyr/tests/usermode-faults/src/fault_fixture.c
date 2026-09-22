/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <string.h>

#include <zephyr/app_memory/app_memdomain.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "expected_fault.h"
#include "fault_fixture.h"
#include "wasm_fixtures.h"

#define FAULT_WORKER_STACK_SIZE 8192U
#define FAULT_WORKER_PRIORITY 5
#define FAULT_WORKER_TIMEOUT K_SECONDS(1)

struct fault_worker_context {
    struct wamr_fault_results *results;
    wamr_fault_worker_t worker;
};

extern struct k_mem_partition z_libc_partition;

K_APPMEM_PARTITION_DEFINE(wamr_partition);
K_APPMEM_PARTITION_DEFINE(wamr_fault_pool_partition);
K_APPMEM_PARTITION_DEFINE(wamr_fault_module_partition);
K_APPMEM_PARTITION_DEFINE(wamr_fault_results_partition);

static struct k_mem_domain wamr_fault_complete_domain;
static struct k_mem_domain wamr_fault_complete_minus_pool_domain;
static struct k_mem_domain wamr_fault_complete_minus_module_domain;
static struct k_mem_domain wamr_fault_complete_minus_wamr_globals_domain;
static struct k_thread fault_worker_thread;
K_THREAD_STACK_DEFINE(fault_worker_stack, FAULT_WORKER_STACK_SIZE);
static struct k_sem fault_worker_done;

static struct k_mem_partition *wamr_fault_complete_partitions[] = {
    &wamr_partition,
    &wamr_fault_pool_partition,
    &wamr_fault_module_partition,
    &z_libc_partition,
    &wamr_fault_results_partition,
};
static struct k_mem_partition *wamr_fault_complete_minus_pool_partitions[] = {
    &wamr_partition,
    &wamr_fault_module_partition,
    &z_libc_partition,
    &wamr_fault_results_partition,
};
static struct k_mem_partition *wamr_fault_complete_minus_module_partitions[] = {
    &wamr_partition,
    &wamr_fault_pool_partition,
    &z_libc_partition,
    &wamr_fault_results_partition,
};
static struct k_mem_partition
    *wamr_fault_complete_minus_wamr_globals_partitions[] = {
        &wamr_fault_pool_partition,
        &wamr_fault_module_partition,
        &z_libc_partition,
        &wamr_fault_results_partition,
    };

K_APP_BMEM(wamr_fault_pool_partition)
static uint8_t fault_pool[WAMR_TEST_RUNTIME_POOL_SIZE] __aligned(8);
K_APP_BMEM(wamr_partition)
static struct wamr_test_runtime workflow_runtime;
K_APP_BMEM(wamr_fault_module_partition)
static uint8_t mutable_module[sizeof(wasm_add)] __aligned(8);
K_APP_BMEM(wamr_fault_results_partition)
static struct wamr_fault_results fault_results;
ZTEST_DMEM static struct wamr_usermode_faults_fixture fault_fixture;
K_APP_BMEM(wamr_fault_results_partition)
static struct fault_worker_context fault_worker_context;

static void
fault_worker_trampoline(void *arg1, void *arg2, void *arg3)
{
    struct fault_worker_context *context = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    context->results->worker_was_user = k_is_user_context();
    context->worker(context->results);
    context->results->worker_completed = true;
    k_sem_give(&fault_worker_done);
}

static struct k_mem_domain *
fault_domain_for_parts(enum wamr_fault_domain_parts parts)
{
    switch (parts) {
        case WAMR_FAULT_COMPLETE_DOMAIN:
            return &wamr_fault_complete_domain;
        case WAMR_FAULT_COMPLETE_DOMAIN & ~WAMR_FAULT_PART_POOL:
            return &wamr_fault_complete_minus_pool_domain;
        case WAMR_FAULT_COMPLETE_DOMAIN & ~WAMR_FAULT_PART_MODULE:
            return &wamr_fault_complete_minus_module_domain;
        case WAMR_FAULT_COMPLETE_DOMAIN & ~WAMR_FAULT_PART_WAMR:
            return &wamr_fault_complete_minus_wamr_globals_domain;
        default:
            zassert_true(false, "unsupported WAMR fault domain mask 0x%x",
                         parts);
            return NULL;
    }
}

static k_tid_t
fault_worker_create(enum wamr_fault_domain_parts parts,
                    wamr_fault_worker_t worker)
{
    k_tid_t tid;
    struct k_mem_domain *domain = fault_domain_for_parts(parts);
    int domain_result;

    if (domain == NULL) {
        return NULL;
    }

    fault_results.worker_completed = false;
    fault_worker_context.results = &fault_results;
    fault_worker_context.worker = worker;
    k_sem_reset(&fault_worker_done);
    tid = k_thread_create(&fault_worker_thread, fault_worker_stack,
                          K_THREAD_STACK_SIZEOF(fault_worker_stack),
                          fault_worker_trampoline, &fault_worker_context, NULL,
                          NULL, FAULT_WORKER_PRIORITY, K_USER, K_FOREVER);
    zassert_not_null(tid, "user fault worker creation failed");

    domain_result = k_mem_domain_add_thread(domain, tid);
    if (domain_result != 0) {
        k_thread_abort(tid);
        zassert_equal(domain_result, 0,
                      "adding fault worker to memory domain failed");
        return NULL;
    }

    k_object_access_grant(&fault_worker_done, tid);
    return tid;
}

static void
fault_worker_wait(k_tid_t tid, const char *completion_message)
{
    int completion_result;
    int join_result;

    k_thread_start(tid);
    completion_result = k_sem_take(&fault_worker_done, FAULT_WORKER_TIMEOUT);
    join_result = k_thread_join(tid, FAULT_WORKER_TIMEOUT);
    if (join_result != 0) {
        k_thread_abort(tid);
    }

    zassert_equal(completion_result, 0, "%s", completion_message);
    zassert_equal(join_result, 0,
                  "user fault worker did not join within the bound");
    zassert_true(fault_results.worker_was_user,
                 "fault worker did not execute in user mode");
}

static void
recovery_worker(struct wamr_fault_results *results)
{
    results->value = 0U;
    results->recovery_completed = wamr_fault_run_add(&results->value);
}

void *
wamr_fault_suite_setup(void)
{
    int domain_result;

    fault_fixture.results = &fault_results;
    k_sem_init(&fault_worker_done, 0, 1);

    domain_result = k_mem_domain_init(
        &wamr_fault_complete_domain, ARRAY_SIZE(wamr_fault_complete_partitions),
        wamr_fault_complete_partitions);
    zassert_equal(domain_result, 0,
                  "complete WAMR fault memory domain initialization failed");

    domain_result =
        k_mem_domain_init(&wamr_fault_complete_minus_pool_domain,
                          ARRAY_SIZE(wamr_fault_complete_minus_pool_partitions),
                          wamr_fault_complete_minus_pool_partitions);
    zassert_equal(domain_result, 0,
                  "complete-minus-pool memory domain initialization failed");

    domain_result = k_mem_domain_init(
        &wamr_fault_complete_minus_module_domain,
        ARRAY_SIZE(wamr_fault_complete_minus_module_partitions),
        wamr_fault_complete_minus_module_partitions);
    zassert_equal(domain_result, 0,
                  "complete-minus-module memory domain initialization failed");

    domain_result = k_mem_domain_init(
        &wamr_fault_complete_minus_wamr_globals_domain,
        ARRAY_SIZE(wamr_fault_complete_minus_wamr_globals_partitions),
        wamr_fault_complete_minus_wamr_globals_partitions);
    zassert_equal(
        domain_result, 0,
        "complete-minus-WAMR-globals memory domain initialization failed");

    return &fault_fixture;
}

void
wamr_fault_before(void *fixture)
{
    struct wamr_usermode_faults_fixture *suite_fixture = fixture;

    expected_fault_disarm();
    memset(suite_fixture->results, 0, sizeof(*suite_fixture->results));
    memcpy(mutable_module, wasm_add, sizeof(mutable_module));
    k_sem_reset(&fault_worker_done);
}

void
wamr_fault_after(void *fixture)
{
    ARG_UNUSED(fixture);
    expected_fault_disarm();
}

void
wamr_fault_run_expected(enum wamr_fault_domain_parts parts,
                        wamr_fault_worker_t worker)
{
    k_tid_t tid = fault_worker_create(parts, worker);
    struct expected_fault_snapshot snapshot;

    if (tid == NULL) {
        return;
    }

    expected_fault_arm(tid, K_ERR_CPU_EXCEPTION, &fault_worker_done);
    fault_worker_wait(tid, "fault handler did not signal completion");
    snapshot = expected_fault_snapshot();
    expected_fault_disarm();

    zassert_true(snapshot.observed,
                 "worker returned without the expected fault");
    zassert_false(fault_results.worker_completed,
                  "faulting worker returned normally");
}

void
wamr_fault_run_normal(enum wamr_fault_domain_parts parts,
                      wamr_fault_worker_t worker)
{
    k_tid_t tid = fault_worker_create(parts, worker);

    if (tid == NULL) {
        return;
    }

    fault_worker_wait(tid, "user worker did not signal completion");
    zassert_true(fault_results.worker_completed,
                 "user worker did not complete normally");
}

bool
wamr_fault_run_add(uint32_t *result)
{
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };

    return wamr_test_run_add_copy(
        &workflow_runtime, fault_pool, sizeof(fault_pool), wasm_add,
        sizeof(wasm_add), result, error, sizeof(error));
}

uint8_t *
wamr_fault_pool_buffer(void)
{
    return fault_pool;
}

uint8_t *
wamr_fault_module_buffer(void)
{
    return mutable_module;
}

void
wamr_fault_assert_recovery(struct wamr_fault_results *results)
{
    results->recovery_completed = false;
    results->value = 0U;
    wamr_fault_run_normal(WAMR_FAULT_COMPLETE_DOMAIN, recovery_worker);
    zassert_true(results->recovery_completed,
                 "valid WAMR workflow failed after the accepted fault");
    zassert_equal(results->value, 42U, "recovery WAMR workflow returned %u",
                  results->value);
}

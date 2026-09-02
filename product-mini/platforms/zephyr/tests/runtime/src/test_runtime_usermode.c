/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/app_memory/app_memdomain.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "expected_fault.h"
#include "runtime_fixture.h"
#include "wasm_fixtures.h"

#define ERROR_BUFFER_SIZE 128U
#define USER_WORKER_STACK_SIZE 8192U
#define USER_WORKER_PRIORITY 5
#define USER_WORKER_TIMEOUT K_SECONDS(1)
#define WASM_STACK_SIZE 4096U
#define WASM_HEAP_SIZE 4096U
#define SMALL_POOL_SIZE 1024U
#define FIXTURE_ACCESS_TOKEN 0x57414d52U
#define PROTECTED_ACCESS_TOKEN 0x50524f54U

struct runtime_user_mode_fixture {
    bool worker_was_user;
    bool workflow_succeeded;
    bool negative_observed;
    bool diagnostic_present;
    bool small_pool_initialized;
    bool recovery_succeeded;
    uint32_t result;
    uint32_t run_count;
    uint32_t access_token;
    char diagnostic[ERROR_BUFFER_SIZE];
};

struct runtime_supervisor_fixture {
    uint32_t protected_value;
};

extern struct k_mem_partition z_libc_partition;
extern struct k_mem_partition ztest_mem_partition;

K_APPMEM_PARTITION_DEFINE(wamr_partition);

static struct k_mem_domain wamr_domain;
static struct k_thread runtime_worker;
K_THREAD_STACK_DEFINE(runtime_worker_stack, USER_WORKER_STACK_SIZE);
static struct k_sem worker_done;
static volatile struct runtime_supervisor_fixture supervisor_fixture;

K_APP_BMEM(wamr_partition) static struct loaded_runtime user_runtime;
K_APP_BMEM(wamr_partition)
static uint8_t small_pool[SMALL_POOL_SIZE] __aligned(8);
ZTEST_DMEM static struct runtime_user_mode_fixture user_results = { 0 };

static void
stop_runtime(struct loaded_runtime *runtime)
{
    if (runtime->exec_env != NULL) {
        wasm_runtime_destroy_exec_env(runtime->exec_env);
        runtime->exec_env = NULL;
    }
    if (runtime->instance != NULL) {
        wasm_runtime_deinstantiate(runtime->instance);
        runtime->instance = NULL;
    }
    if (runtime->module != NULL) {
        wasm_runtime_unload(runtime->module);
        runtime->module = NULL;
    }
    wasm_runtime_destroy();
}

static bool
start_runtime(struct loaded_runtime *runtime, const uint8_t *bytes,
              uint32_t size, char *error)
{
    RuntimeInitArgs args = { 0 };
    uint32_t image_size = (size + 7U) & ~7U;
    uint32_t heap_size;
    uint8_t *module_bytes;

    runtime->module = NULL;
    runtime->instance = NULL;
    runtime->exec_env = NULL;
    if (size == 0U || image_size < size
        || image_size >= sizeof(runtime->pool)) {
        return false;
    }

    heap_size = sizeof(runtime->pool) - image_size;
    module_bytes = &runtime->pool[heap_size];
    memcpy(module_bytes, bytes, size);

    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = runtime->pool;
    args.mem_alloc_option.pool.heap_size = heap_size;
    args.running_mode = Mode_Interp;
    if (!wasm_runtime_full_init(&args)) {
        return false;
    }

    /* The public loader may mutate its input, so it receives the pool copy. */
    runtime->module =
        wasm_runtime_load(module_bytes, size, error, ERROR_BUFFER_SIZE);
    if (runtime->module == NULL) {
        stop_runtime(runtime);
        return false;
    }

    runtime->instance =
        wasm_runtime_instantiate(runtime->module, WASM_STACK_SIZE,
                                 WASM_HEAP_SIZE, error, ERROR_BUFFER_SIZE);
    if (runtime->instance == NULL) {
        stop_runtime(runtime);
        return false;
    }

    runtime->exec_env =
        wasm_runtime_create_exec_env(runtime->instance, WASM_STACK_SIZE);
    if (runtime->exec_env == NULL) {
        stop_runtime(runtime);
        return false;
    }

    return true;
}

static bool
run_add_lifecycle(uint32_t *result)
{
    char error[ERROR_BUFFER_SIZE] = { 0 };
    uint32_t argv[2] = { 20U, 22U };
    wasm_function_inst_t add;
    bool started =
        start_runtime(&user_runtime, wasm_add, sizeof(wasm_add), error);
    bool called = false;

    if (started) {
        add = wasm_runtime_lookup_function(user_runtime.instance, "add");
        called =
            add != NULL
            && wasm_runtime_call_wasm(user_runtime.exec_env, add, 2U, argv);
        if (called) {
            *result = argv[0];
        }
        stop_runtime(&user_runtime);
    }

    return called;
}

static void
complete_worker(struct runtime_user_mode_fixture *results)
{
    results->worker_was_user = k_is_user_context();
    k_sem_give(&worker_done);
}

static void
valid_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    results->workflow_succeeded = run_add_lifecycle(&results->result);
    complete_worker(results);
}

static void
valid_recovery_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;
    uint32_t result = 0U;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    results->recovery_succeeded = run_add_lifecycle(&result) && result == 42U;
    complete_worker(results);
}

static void
twice_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;
    uint32_t result = 0U;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    if (run_add_lifecycle(&result) && result == 42U) {
        results->run_count++;
    }
    result = 0U;
    if (run_add_lifecycle(&result) && result == 42U) {
        results->run_count++;
    }
    results->workflow_succeeded = results->run_count == 2U;
    complete_worker(results);
}

static void
fixture_access_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    results->access_token = FIXTURE_ACCESS_TOKEN;
    complete_worker(results);
}

static void
protection_fault_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    results->worker_was_user = k_is_user_context();
    supervisor_fixture.protected_value = PROTECTED_ACCESS_TOKEN;
    k_sem_give(&worker_done);
}

static void
malformed_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;
    char error[ERROR_BUFFER_SIZE] = { 0 };
    bool started = start_runtime(&user_runtime, malformed_wasm,
                                 sizeof(malformed_wasm), error);

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    if (started) {
        stop_runtime(&user_runtime);
    }
    results->negative_observed = !started;
    results->diagnostic_present = error[0] != '\0';
    memcpy(results->diagnostic, error, sizeof(results->diagnostic));
    complete_worker(results);
}

static void
small_pool_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;
    RuntimeInitArgs args = { 0 };
    char error[ERROR_BUFFER_SIZE] = { 0 };
    wasm_module_t module = NULL;
    wasm_module_inst_t instance = NULL;
    uint32_t image_size = (sizeof(wasm_add) + 7U) & ~7U;
    uint32_t heap_size = sizeof(small_pool) - image_size;
    uint8_t *module_bytes = &small_pool[heap_size];

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    memcpy(module_bytes, wasm_add, sizeof(wasm_add));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = small_pool;
    args.mem_alloc_option.pool.heap_size = heap_size;
    args.running_mode = Mode_Interp;
    results->small_pool_initialized = wasm_runtime_full_init(&args);
    if (results->small_pool_initialized) {
        module = wasm_runtime_load(module_bytes, sizeof(wasm_add), error,
                                   sizeof(error));
        if (module != NULL) {
            instance = wasm_runtime_instantiate(
                module, WASM_STACK_SIZE, WASM_HEAP_SIZE, error, sizeof(error));
        }
        results->negative_observed = module == NULL || instance == NULL;
        if (instance != NULL) {
            wasm_runtime_deinstantiate(instance);
        }
        if (module != NULL) {
            wasm_runtime_unload(module);
        }
        wasm_runtime_destroy();
    }
    memcpy(results->diagnostic, error, sizeof(results->diagnostic));
    results->diagnostic_present = error[0] != '\0';
    complete_worker(results);
}

static void
missing_export_worker(void *arg1, void *arg2, void *arg3)
{
    struct runtime_user_mode_fixture *results = arg1;
    char error[ERROR_BUFFER_SIZE] = { 0 };
    wasm_function_inst_t missing = NULL;
    bool exception_set = false;
    bool started =
        start_runtime(&user_runtime, wasm_add, sizeof(wasm_add), error);

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    if (started) {
        missing =
            wasm_runtime_lookup_function(user_runtime.instance, "missing");
        exception_set =
            wasm_runtime_get_exception(user_runtime.instance) != NULL;
        stop_runtime(&user_runtime);
    }
    results->negative_observed = started && missing == NULL && !exception_set;
    memcpy(results->diagnostic, error, sizeof(results->diagnostic));
    results->diagnostic_present = error[0] != '\0';
    complete_worker(results);
}

static void
run_user_worker(struct runtime_user_mode_fixture *results,
                k_thread_entry_t entry)
{
    k_tid_t tid = k_thread_create(&runtime_worker, runtime_worker_stack,
                                  K_THREAD_STACK_SIZEOF(runtime_worker_stack),
                                  entry, results, NULL, NULL,
                                  USER_WORKER_PRIORITY, K_USER, K_FOREVER);
    int domain_result;
    int completion_result;
    int join_result;

    zassert_not_null(tid, "user worker creation failed");
    domain_result = k_mem_domain_add_thread(&wamr_domain, tid);
    if (domain_result != 0) {
        k_thread_abort(tid);
        zassert_equal(domain_result, 0, "adding user worker to domain failed");
        return;
    }

    k_object_access_grant(&worker_done, tid);
    k_thread_start(tid);
    completion_result = k_sem_take(&worker_done, USER_WORKER_TIMEOUT);
    join_result = k_thread_join(tid, USER_WORKER_TIMEOUT);
    if (join_result != 0) {
        k_thread_abort(tid);
    }

    zassert_equal(completion_result, 0,
                  "user worker did not signal completion");
    zassert_equal(join_result, 0, "user worker did not join within the bound");
    zassert_true(results->worker_was_user,
                 "runtime worker did not execute in user mode");
}

static void *
runtime_user_mode_setup(void)
{
    struct k_mem_partition *partitions[] = {
        &wamr_partition,
        &z_libc_partition,
        &ztest_mem_partition,
    };

    zassert_equal(
        k_mem_domain_init(&wamr_domain, ARRAY_SIZE(partitions), partitions), 0,
        "WAMR memory domain initialization failed");
    return &user_results;
}

static void
runtime_user_mode_before(void *fixture)
{
    expected_fault_disarm();
    memset(fixture, 0, sizeof(struct runtime_user_mode_fixture));
    supervisor_fixture.protected_value = 0U;
    k_sem_init(&worker_done, 0, 1);
}

static void
runtime_user_mode_after(void *fixture)
{
    ARG_UNUSED(fixture);
    expected_fault_disarm();
}

ZTEST_SUITE(runtime_user_mode, NULL, runtime_user_mode_setup,
            runtime_user_mode_before, runtime_user_mode_after, NULL);

ZTEST_F(runtime_user_mode, test_valid_module_workflow)
{
    run_user_worker(fixture, valid_worker);
    zassert_true(fixture->workflow_succeeded,
                 "valid user-mode workflow failed");
    zassert_equal(fixture->result, 42U, "valid user-mode workflow returned %u",
                  fixture->result);
}

ZTEST_F(runtime_user_mode, test_workflow_runs_twice)
{
    run_user_worker(fixture, twice_worker);
    zassert_true(fixture->workflow_succeeded,
                 "repeated user-mode workflow failed");
    zassert_equal(fixture->run_count, 2U,
                  "user-mode workflow completed %u times", fixture->run_count);
}

ZTEST_F(runtime_user_mode, test_fixture_memory_is_accessible)
{
    run_user_worker(fixture, fixture_access_worker);
    zassert_equal(fixture->access_token, FIXTURE_ACCESS_TOKEN,
                  "user worker could not update shared fixture memory");
}

ZTEST_F(runtime_user_mode, test_user_cannot_access_supervisor_fixture)
{
    k_tid_t tid = k_thread_create(&runtime_worker, runtime_worker_stack,
                                  K_THREAD_STACK_SIZEOF(runtime_worker_stack),
                                  protection_fault_worker, fixture, NULL, NULL,
                                  USER_WORKER_PRIORITY, K_USER, K_FOREVER);
    int domain_result;
    int completion_result;
    int join_result;

    zassert_not_null(tid, "user worker creation failed");
    domain_result = k_mem_domain_add_thread(&wamr_domain, tid);
    if (domain_result != 0) {
        k_thread_abort(tid);
        zassert_equal(domain_result, 0, "adding user worker to domain failed");
        return;
    }

    k_object_access_grant(&worker_done, tid);
    expected_fault_arm(tid, K_ERR_CPU_EXCEPTION, &worker_done);
    k_thread_start(tid);
    completion_result = k_sem_take(&worker_done, USER_WORKER_TIMEOUT);
    join_result = k_thread_join(tid, USER_WORKER_TIMEOUT);
    if (join_result != 0) {
        k_thread_abort(tid);
    }

    zassert_equal(completion_result, 0,
                  "fault handler did not signal completion");
    zassert_equal(join_result, 0,
                  "faulting user worker did not join within the bound");
    zassert_true(fixture->worker_was_user,
                 "protection worker did not execute in user mode");
    zassert_true(expected_fault_observed(),
                 "user worker returned without the expected fault");
    zassert_equal(supervisor_fixture.protected_value, 0U,
                  "user worker modified supervisor-only fixture memory");
}

ZTEST_F(runtime_user_mode, test_malformed_module_is_rejected)
{
    run_user_worker(fixture, malformed_worker);
    run_user_worker(fixture, valid_recovery_worker);
    zassert_true(fixture->negative_observed,
                 "malformed module was accepted in user mode");
    zassert_true(fixture->diagnostic_present,
                 "malformed module had no diagnostic");
    zassert_true(fixture->recovery_succeeded,
                 "valid workflow failed after malformed module");
}

ZTEST_F(runtime_user_mode, test_small_pool_fails_cleanly)
{
    run_user_worker(fixture, small_pool_worker);
    run_user_worker(fixture, valid_recovery_worker);
    zassert_true(fixture->small_pool_initialized,
                 "small pool initialization failed early");
    zassert_true(fixture->negative_observed,
                 "small pool unexpectedly instantiated module");
    zassert_true(fixture->recovery_succeeded,
                 "valid workflow failed after small pool rejection");
}

ZTEST_F(runtime_user_mode, test_missing_export_then_valid_workflow)
{
    run_user_worker(fixture, missing_export_worker);
    run_user_worker(fixture, valid_recovery_worker);
    zassert_true(fixture->negative_observed,
                 "missing export lookup did not fail cleanly");
    zassert_true(fixture->recovery_succeeded,
                 "valid workflow failed after missing export lookup");
}

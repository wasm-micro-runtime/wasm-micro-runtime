/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#if defined(CONFIG_WAMR_TEST_USER_MODE)
#include <zephyr/app_memory/app_memdomain.h>
#include "zephyr_sync_pool.h"

K_APPMEM_PARTITION_DEFINE(wamr_partition);
#define CONTRACT_RUNTIME_DATA K_APP_BMEM(wamr_partition)
#else
#define CONTRACT_RUNTIME_DATA
#endif

#include "platform_api_extension.h"
#include "runtime_user_workflow.h"
#include "wasm_fixtures.h"
#include "zephyr_thread_pool.h"

#define CONTRACT_THREAD_COUNT 2U
#define CONTRACT_STACK_SIZE 2048U
#define CONTRACT_ATTEMPTS (CONTRACT_THREAD_COUNT + 1U)

WAMR_ZEPHYR_THREAD_POOL_DEFINE(contract_threads, CONTRACT_THREAD_COUNT,
                               CONTRACT_STACK_SIZE);
WAMR_ZEPHYR_THREAD_POOL_DEFINE(replacement_threads, 1, CONTRACT_STACK_SIZE);
K_THREAD_STACK_DEFINE(alternative_owner_stack, 1024);
static struct k_thread alternative_owner;
CONTRACT_RUNTIME_DATA
static uint8_t runtime_pool[WAMR_TEST_RUNTIME_POOL_SIZE] __aligned(8);
CONTRACT_RUNTIME_DATA static struct wamr_test_runtime runtime;
static struct k_sem worker_ready;
static struct k_sem worker_release;

struct worker_context {
    k_tid_t native;
    bool user;
};

struct worker_batch {
    struct worker_context contexts[CONTRACT_ATTEMPTS];
    korp_tid handles[CONTRACT_ATTEMPTS];
    int create[CONTRACT_ATTEMPTS];
    int ready[CONTRACT_ATTEMPTS];
    int join[CONTRACT_ATTEMPTS];
    int identical_prepare;
    int replacement_prepare;
    int different_owner_prepare;
    int owner_join;
    bool owner_created;
};

ZTEST_BMEM static struct worker_batch batch;
ZTEST_BMEM static void *pressure_allocations[512];

static void
unused_root(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
}

static void
check_live_binding(void)
{
    k_tid_t owner =
        k_thread_create(&alternative_owner, alternative_owner_stack,
                        K_THREAD_STACK_SIZEOF(alternative_owner_stack),
                        unused_root, NULL, NULL, NULL, 5, 0, K_FOREVER);

    batch.identical_prepare =
        wamr_zephyr_thread_pool_prepare(&contract_threads, k_current_get());
    batch.replacement_prepare =
        wamr_zephyr_thread_pool_prepare(&replacement_threads, k_current_get());
    batch.owner_created = owner != NULL;
    batch.different_owner_prepare =
        wamr_zephyr_thread_pool_prepare(&contract_threads, owner);
    if (owner != NULL) {
        k_thread_abort(owner);
        batch.owner_join = k_thread_join(owner, K_SECONDS(1));
    }
}

static void *
hold_slot(void *arg)
{
    struct worker_context *context = arg;

    context->native = k_current_get();
    context->user = k_is_user_context();
    k_sem_give(&worker_ready);
    k_sem_take(&worker_release, K_FOREVER);
    return NULL;
}

/* Complete every successful creation before any assertion, including an
 * unexpected success from the allocation implementation being tested.
 */
static void
run_workers(size_t attempts, unsigned int stack_size, bool check_binding)
{
    memset(&batch, 0, sizeof(batch));

    for (size_t i = 0; i < attempts; i++) {
        if (check_binding && i == CONTRACT_THREAD_COUNT) {
            /* Both slots are still occupied when re-preparation is tried.
             * The next create must not see their storage as free.
             */
            check_live_binding();
        }
        batch.create[i] = os_thread_create(&batch.handles[i], hold_slot,
                                           &batch.contexts[i], stack_size);
        if (batch.create[i] == BHT_OK) {
            batch.ready[i] = k_sem_take(&worker_ready, K_SECONDS(1));
        }
    }
    for (size_t i = 0; i < attempts; i++) {
        if (batch.create[i] == BHT_OK) {
            k_sem_give(&worker_release);
        }
    }
    for (size_t i = 0; i < attempts; i++) {
        if (batch.create[i] == BHT_OK) {
            batch.join[i] = os_thread_join(batch.handles[i], NULL);
        }
    }
}

static bool
worker_succeeded(size_t index, bool expected_user)
{
    return batch.create[index] == BHT_OK && batch.ready[index] == 0
           && batch.join[index] == BHT_OK
           && (batch.contexts[index].native == &contract_threads.threads[0]
               || batch.contexts[index].native == &contract_threads.threads[1])
           && batch.contexts[index].user == expected_user;
}

static bool
metadata_allocation_failure_preserves_slots(bool expected_user)
{
    static const uint32_t requests[] = { 4096U, 1024U, 256U, 64U, 16U, 1U };
    size_t count = 0;
    int create_result = BHT_OK;

    /* Fill the real pool, not a fake allocator. The pointer table is outside
     * that heap; its fixed bound must never masquerade as allocator failure.
     */
    for (size_t size = 0; size < ARRAY_SIZE(requests); size++) {
        while (count < ARRAY_SIZE(pressure_allocations)) {
            void *ptr = wasm_runtime_malloc(requests[size]);
            if (ptr == NULL) {
                break;
            }
            pressure_allocations[count++] = ptr;
        }
    }
    void *probe = wasm_runtime_malloc(1U);
    bool exhausted = probe == NULL;
    if (probe != NULL) {
        wasm_runtime_free(probe);
    }
    if (exhausted) {
        run_workers(1U, CONTRACT_STACK_SIZE, false);
        create_result = batch.create[0];
    }
    while (count != 0U) {
        wasm_runtime_free(pressure_allocations[--count]);
        pressure_allocations[count] = NULL;
    }
    run_workers(CONTRACT_THREAD_COUNT, CONTRACT_STACK_SIZE, false);
    return exhausted && create_result == BHT_ERROR
           && worker_succeeded(0U, expected_user)
           && worker_succeeded(1U, expected_user)
           && batch.contexts[0].native != batch.contexts[1].native;
}

ZTEST_SUITE(thread_pool_contract, NULL, NULL, NULL, NULL, NULL);

#if !defined(CONFIG_WAMR_TEST_USER_MODE)
struct root_join_probe {
    korp_tid root;
    int result;
    struct k_sem done;
};

static void
try_join_root(void *arg1, void *arg2, void *arg3)
{
    struct root_join_probe *probe = arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    probe->result = os_thread_join(probe->root, NULL);
    k_sem_give(&probe->done);
}

static bool
root_join_is_rejected(void)
{
    struct root_join_probe probe = { .root = os_self_thread() };

    k_sem_init(&probe.done, 0, 1);
    k_tid_t tid =
        k_thread_create(&alternative_owner, alternative_owner_stack,
                        K_THREAD_STACK_SIZEOF(alternative_owner_stack),
                        try_join_root, &probe, NULL, NULL, 5, 0, K_NO_WAIT);
    if (tid == NULL) {
        return false;
    }
    int completed = k_sem_take(&probe.done, K_SECONDS(1));
    if (completed != 0) {
        k_thread_abort(tid);
    }
    int joined = k_thread_join(tid, K_SECONDS(1));

    return completed == 0 && joined == 0 && probe.result == BHT_ERROR;
}

static void
assert_worker(size_t index)
{
    zassert_true(worker_succeeded(index, false),
                 "worker %zu: create=%d ready=%d join=%d native=%p user=%d",
                 index, batch.create[index], batch.ready[index],
                 batch.join[index], batch.contexts[index].native,
                 batch.contexts[index].user);
}

/* One ordered fresh-image contract: a prepared binding is not reset between
 * independent cases just to manufacture unprepared state.
 */
ZTEST(thread_pool_contract, test_kernel_contract)
{
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    uint32_t result = 0;

    k_sem_init(&worker_ready, 0, CONTRACT_ATTEMPTS);
    k_sem_init(&worker_release, 0, CONTRACT_ATTEMPTS);
    zassert_true(wamr_test_runtime_start_copy(
                     &runtime, runtime_pool, sizeof(runtime_pool), wasm_add,
                     sizeof(wasm_add), error, sizeof(error)),
                 "no-worker runtime failed: %s", error);
    zassert_not_null(os_self_thread(), "root identity requires a worker pool");
    /* Application roots own no worker slot. A different native caller must
     * not block joining the root or transfer its static metadata to cleanup.
     */
    bool root_join_rejected = root_join_is_rejected();
    bool called = wamr_test_runtime_call_add(&runtime, &result);
    wamr_test_runtime_stop(&runtime);
    zassert_true(root_join_rejected,
                 "application root was accepted by worker join");
    zassert_true(called, "no-worker Wasm call failed");
    zassert_equal(result, 42U, "incorrect Wasm result");

    zassert_true(
        wamr_test_runtime_init(&runtime, runtime_pool, sizeof(runtime_pool)),
        "unprepared runtime initialization failed");
    run_workers(1U, CONTRACT_STACK_SIZE, false);
    int create_result = batch.create[0];
    int root_detach_result = os_thread_detach(os_self_thread());
    wamr_test_runtime_stop(&runtime);
    zassert_equal(root_detach_result, BHT_ERROR,
                  "application root was accepted by worker detach");
    zassert_is_null(os_self_thread(),
                    "root rejection broke no-worker teardown");
    zassert_equal(create_result, BHT_ERROR,
                  "worker creation succeeded without preparation");

    zassert_equal(
        wamr_zephyr_thread_pool_prepare(&contract_threads, k_current_get()),
        BHT_OK, "kernel pool preparation failed");
    zassert_true(
        wamr_test_runtime_init(&runtime, runtime_pool, sizeof(runtime_pool)),
        "prepared runtime initialization failed");
    run_workers(CONTRACT_ATTEMPTS, CONTRACT_STACK_SIZE, true);
    assert_worker(0U);
    assert_worker(1U);
    zassert_not_equal(batch.contexts[0].native, batch.contexts[1].native,
                      "simultaneous workers shared a slot");
    zassert_equal(batch.create[CONTRACT_THREAD_COUNT], BHT_ERROR,
                  "supplied capacity was ignored");
    zassert_equal(batch.identical_prepare, BHT_OK,
                  "identical live binding was rejected");
    zassert_equal(batch.replacement_prepare, BHT_ERROR,
                  "replacement descriptor was accepted");
    zassert_true(batch.owner_created, "alternative owner setup failed");
    zassert_equal(batch.owner_join, 0, "alternative owner did not terminate");
    zassert_equal(batch.different_owner_prepare, BHT_ERROR,
                  "binding owner was replaced");

    zassert_true(metadata_allocation_failure_preserves_slots(false),
                 "heap exhaustion setup or full-capacity recovery failed");
    korp_tid previous_worker = batch.handles[0];
    korp_tid previous_root = os_self_thread();
    wamr_test_runtime_stop(&runtime);

    for (size_t cycle = 0; cycle < 3U; cycle++) {
        zassert_true(wamr_test_runtime_init(&runtime, runtime_pool,
                                            sizeof(runtime_pool)),
                     "restart initialization failed");
        korp_tid current_root = os_self_thread();
        zassert_not_null(current_root, "restarted root is unmapped");
        zassert_not_equal(current_root, previous_root,
                          "restart revived an old root identity");
        zassert_equal(os_thread_join(previous_worker, NULL), BHT_ERROR,
                      "restart revived an old worker identity");
        zassert_equal(os_thread_join(previous_root, NULL), BHT_ERROR,
                      "old root remained joinable after restart");
        zassert_equal(os_thread_detach(previous_worker), BHT_ERROR,
                      "stale worker became detachable");
        zassert_equal(os_thread_detach(previous_root), BHT_ERROR,
                      "stale root became detachable");
        run_workers(CONTRACT_THREAD_COUNT, CONTRACT_STACK_SIZE, false);
        zassert_equal(batch.create[0], BHT_OK,
                      "restart %zu lost prepared worker resources", cycle);
        assert_worker(0U);
        assert_worker(1U);
        zassert_not_equal(batch.handles[0], previous_worker,
                          "worker generation counter reset");
        previous_worker = batch.handles[0];
        previous_root = current_root;
        wamr_test_runtime_stop(&runtime);
        zassert_is_null(os_self_thread(), "destroy retained root mapping");
    }
}
#else

extern struct k_mem_partition z_libc_partition;
extern struct k_mem_partition ztest_mem_partition;

WAMR_ZEPHYR_SYNC_POOL_DEFINE(contract_sync, 16, 8);
K_THREAD_STACK_DEFINE(user_root_stack, 8192);
static struct k_thread user_root;
static struct k_mem_domain user_domain;
static struct k_sem root_stage;
static struct k_sem root_continue;
static struct k_sem root_done;

struct user_report {
    bool user;
    bool no_pool_runtime;
    bool no_pool_identity;
    int no_pool_create;
    bool capacity;
    bool recovery;
    bool metadata_recovery;
    bool restarts;
    bool supervisor_init;
    bool supervisor_context;
    bool identity_preserved;
    bool transitioned_add;
    korp_tid supervisor_identity;
};

ZTEST_BMEM static struct user_report user_results;

static void
finish_user_report(struct user_report *report)
{
    report->user = k_is_user_context();
    k_sem_give(&root_done);
}

static void
user_pool_contract(void *arg1, void *arg2, void *arg3)
{
    struct user_report *report = arg1;
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };
    uint32_t result = 0;
    bool started;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    started = wamr_test_runtime_start_copy(
        &runtime, runtime_pool, sizeof(runtime_pool), wasm_add,
        sizeof(wasm_add), error, sizeof(error));
    if (started) {
        report->no_pool_identity = os_self_thread() != NULL;
        report->no_pool_runtime =
            wamr_test_runtime_call_add(&runtime, &result) && result == 42U;
        run_workers(1U, CONTRACT_STACK_SIZE, false);
        report->no_pool_create = batch.create[0];
        wamr_test_runtime_stop(&runtime);
    }
    k_sem_give(&root_stage);
    k_sem_take(&root_continue, K_FOREVER);

    report->restarts = started;
    korp_tid previous_worker = NULL;
    korp_tid previous_root = NULL;
    for (size_t cycle = 0; cycle < 3U; cycle++) {
        if (!wamr_test_runtime_init(&runtime, runtime_pool,
                                    sizeof(runtime_pool))) {
            report->restarts = false;
            break;
        }
        korp_tid current_root = os_self_thread();
        if (current_root == NULL || current_root == previous_root) {
            report->restarts = false;
        }
        if (cycle != 0U
            && (os_thread_join(previous_worker, NULL) != BHT_ERROR
                || os_thread_detach(previous_worker) != BHT_ERROR
                || os_thread_join(previous_root, NULL) != BHT_ERROR
                || os_thread_detach(previous_root) != BHT_ERROR)) {
            report->restarts = false;
        }
        run_workers(CONTRACT_ATTEMPTS, CONTRACT_STACK_SIZE, false);
        bool capacity = worker_succeeded(0U, true) && worker_succeeded(1U, true)
                        && batch.contexts[0].native != batch.contexts[1].native
                        && batch.create[2] == BHT_ERROR;
        report->capacity =
            cycle == 0U ? capacity : report->capacity && capacity;
        if (batch.handles[0] == previous_worker) {
            report->restarts = false;
        }
        previous_worker = batch.handles[0];
        previous_root = current_root;
        if (cycle == 0U) {
            report->metadata_recovery =
                metadata_allocation_failure_preserves_slots(true);
        }
        wamr_test_runtime_stop(&runtime);
        if (os_self_thread() != NULL) {
            report->restarts = false;
        }
    }
    finish_user_report(report);
}

static void
transition_continuation(void *arg1, void *arg2, void *arg3)
{
    struct user_report *report = arg1;
    uint32_t result = 0;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    report->identity_preserved =
        os_self_thread() == report->supervisor_identity;
    report->transitioned_add =
        wamr_test_runtime_call_add(&runtime, &result) && result == 42U;
    run_workers(1U, CONTRACT_STACK_SIZE, false);
    report->capacity = worker_succeeded(0U, true);
    korp_tid previous_worker = batch.handles[0];
    report->metadata_recovery =
        metadata_allocation_failure_preserves_slots(true);
    wamr_test_runtime_stop(&runtime);
    report->restarts =
        wamr_test_runtime_init(&runtime, runtime_pool, sizeof(runtime_pool));
    if (report->restarts) {
        report->restarts =
            os_self_thread() != report->supervisor_identity
            && os_thread_join(previous_worker, NULL) == BHT_ERROR;
        run_workers(1U, CONTRACT_STACK_SIZE, false);
        report->recovery =
            worker_succeeded(0U, true) && batch.handles[0] != previous_worker;
        wamr_test_runtime_stop(&runtime);
    }
    finish_user_report(report);
}

static void
supervisor_initialization(void *arg1, void *arg2, void *arg3)
{
    struct user_report *report = arg1;
    char error[WAMR_TEST_ERROR_SIZE] = { 0 };

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    report->supervisor_context = !k_is_user_context();
    report->supervisor_init = wamr_test_runtime_start_copy(
        &runtime, runtime_pool, sizeof(runtime_pool), wasm_add,
        sizeof(wasm_add), error, sizeof(error));
    report->supervisor_identity = os_self_thread();
    if (!report->supervisor_init || report->supervisor_identity == NULL) {
        finish_user_report(report);
        return;
    }
    /* No WAMR workers exist yet. All continuation state is static: Zephyr
     * discards this supervisor stack when entering user mode.
     */
    k_thread_user_mode_enter(transition_continuation, report, NULL, NULL);
}

static k_tid_t
prepare_user_root(bool transition)
{
    struct k_mem_partition *partitions[] = {
        &wamr_partition,
        &z_libc_partition,
        &ztest_mem_partition,
    };

    memset(&user_results, 0, sizeof(user_results));
    k_sem_init(&worker_ready, 0, CONTRACT_ATTEMPTS);
    k_sem_init(&worker_release, 0, CONTRACT_ATTEMPTS);
    k_sem_init(&root_stage, 0, 1);
    k_sem_init(&root_continue, 0, 1);
    k_sem_init(&root_done, 0, 1);
    zassert_equal(
        k_mem_domain_init(&user_domain, ARRAY_SIZE(partitions), partitions), 0,
        "memory domain setup failed");
    k_tid_t tid = k_thread_create(
        &user_root, user_root_stack, K_THREAD_STACK_SIZEOF(user_root_stack),
        transition ? supervisor_initialization : user_pool_contract,
        &user_results, NULL, NULL, 5, transition ? 0 : K_USER, K_FOREVER);
    zassert_not_null(tid, "root creation failed");
    zassert_equal(k_mem_domain_add_thread(&user_domain, tid), 0,
                  "root memory domain setup failed");
    zassert_equal(wamr_zephyr_sync_pool_prepare(&contract_sync, tid), BHT_OK,
                  "existing synchronization pool setup failed");
    k_object_access_grant(&worker_ready, tid);
    k_object_access_grant(&worker_release, tid);
    k_object_access_grant(&root_stage, tid);
    k_object_access_grant(&root_continue, tid);
    k_object_access_grant(&root_done, tid);
    return tid;
}

static void
join_user_root(k_tid_t tid)
{
    int completed = k_sem_take(&root_done, K_SECONDS(2));
    int joined = k_thread_join(tid, K_SECONDS(1));

    if (joined != 0) {
        k_thread_abort(tid);
    }
    zassert_equal(completed, 0, "root completion timed out");
    zassert_equal(joined, 0, "root join timed out");
    zassert_true(user_results.user, "root did not execute user continuation");
}

#if defined(CONFIG_WAMR_TEST_THREAD_POOL_TRANSITION)
ZTEST(thread_pool_contract, test_supervisor_initialization_then_user_workers)
{
    k_tid_t tid = prepare_user_root(true);

    zassert_equal(wamr_zephyr_thread_pool_prepare(&contract_threads, tid),
                  BHT_OK, "transition pool setup failed");
    k_thread_start(tid);
    join_user_root(tid);
    zassert_true(user_results.supervisor_context,
                 "root did not start as supervisor");
    zassert_true(user_results.supervisor_init,
                 "supervisor runtime init failed");
    zassert_true(user_results.identity_preserved,
                 "privilege transition changed mapped root identity");
    zassert_true(user_results.transitioned_add, "user Wasm call failed");
    zassert_true(user_results.capacity, "post-transition worker failed");
    zassert_true(user_results.metadata_recovery,
                 "user metadata failure/recovery failed");
    zassert_true(user_results.restarts && user_results.recovery,
                 "user restart required new preparation");
}
#else
ZTEST(thread_pool_contract, test_user_contract)
{
    k_tid_t tid = prepare_user_root(false);

    k_thread_start(tid);
    int staged = k_sem_take(&root_stage, K_SECONDS(2));
    if (staged != 0) {
        k_thread_abort(tid);
    }
    zassert_equal(staged, 0, "unprepared user runtime timed out");
    zassert_equal(wamr_zephyr_thread_pool_prepare(&contract_threads, tid),
                  BHT_OK, "live user root pool setup failed");
    k_sem_give(&root_continue);
    join_user_root(tid);
    zassert_true(user_results.no_pool_runtime && user_results.no_pool_identity,
                 "user runtime incorrectly required worker resources");
    zassert_equal(user_results.no_pool_create, BHT_ERROR,
                  "unprepared user worker was created");
    zassert_true(user_results.capacity, "user native storage/capacity failed");
    zassert_true(user_results.metadata_recovery,
                 "user metadata failure/recovery failed");
    zassert_true(user_results.restarts,
                 "user restart/generation safety failed");
}
#endif
#endif

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "test_common.h"

#include "platform_api_extension.h"
#include "platform_api_vmcore.h"
#include "zephyr_thread_pool.h"

#define WAMR_TEST_STACK_SIZE 2048U
#define THREAD_READY_TIMEOUT_MS 500
#define MAX_BLOCKED_THREADS (BH_ZEPHYR_MPU_STACK_COUNT + 1U)
#define NESTED_GRANDCHILD_RESULT ((void *)0x2468U)
#define NESTED_CHILD_RESULT ((void *)0x1357U)
#define EXPLICIT_EXIT_RESULT ((void *)0x369CU)

BUILD_ASSERT(!__builtin_types_compatible_p(korp_tid, k_tid_t),
             "WAMR thread handles must be opaque to Zephyr");

enum {
    WAMR_JOIN_TEST_BEFORE_CLAIM,
    WAMR_JOIN_TEST_UNPROTECTED_LOOKUP,
    WAMR_JOIN_TEST_PROTECTED_LOOKUP,
    WAMR_JOIN_TEST_CLAIMED,
    WAMR_JOIN_TEST_BEFORE_REMOVE,
    WAMR_JOIN_TEST_ENTERED,
};

enum {
    WAMR_DETACH_TEST_EXITED_CLAIMED,
};

void
wamr_thread_test_before(void *fixture);

int
wamr_zephyr_thread_test_wait(korp_tid handle, k_timeout_t timeout);
struct thread_result {
    int input;
    int output;
    unsigned int writes;
};

struct identity_state {
    korp_tid handles[2];
};

struct identity_arg {
    struct identity_state *state;
    unsigned int slot;
};

struct blocked_thread_state {
    atomic_t ready;
    atomic_t release;
    atomic_t exited;
};

struct nested_thread_state {
    int create_result;
    int join_result;
    void *grandchild_result;
};

struct concurrent_join_state {
    atomic_t hook_active;
    atomic_t before_claim_arrivals;
    atomic_t unprotected_lookup_arrivals;
    atomic_t protected_lookup_arrivals;
    atomic_t cleanup_started;
    atomic_t winner_done;
    atomic_t replacement_pending;
    korp_tid target;
    korp_tid replacement;
    int join_results[2];
    int replacement_create_result;
};

struct concurrent_join_arg {
    struct concurrent_join_state *state;
    unsigned int slot;
};

struct join_detach_race_state {
    atomic_t hook_active;
    atomic_t join_claimed;
    atomic_t release_join;
    korp_tid target;
    int join_result;
};

struct detached_join_state {
    atomic_t completed;
    korp_tid target;
    int result;
};

struct detach_after_exit_race_state {
    atomic_t hook_active;
    atomic_t target_claimed;
    atomic_t release_detacher;
    atomic_t detacher_done;
    korp_tid target;
    int detach_result;
};

struct detached_reuse_race_state {
    atomic_t hook_active;
    atomic_t start_join;
    atomic_t join_paused;
    atomic_t release_join;
    atomic_t join_done;
    atomic_t start_creator;
    atomic_t creator_started;
    atomic_t creator_done;
    struct blocked_thread_state replacement_blocked;
    korp_tid target;
    korp_tid replacement;
    int join_result;
    int replacement_create_result;
};

struct explicit_exit_state {
    atomic_t exit_started;
    atomic_t exit_returned;
    int detach_result;
};

struct platform_thread_fixture {
    struct thread_result result;
    struct thread_result results[4];
    struct identity_state identities;
    struct identity_arg identity_args[2];
    struct blocked_thread_state blocked;
    struct nested_thread_state nested;
    struct concurrent_join_state concurrent_join;
    struct concurrent_join_arg concurrent_join_args[2];
    struct join_detach_race_state join_detach_race;
    struct detached_join_state detached_join;
    struct detach_after_exit_race_state detach_after_exit_race;
    struct detached_reuse_race_state detached_reuse_race;
    struct explicit_exit_state explicit_exit;
    atomic_t child_exited;
};

ZTEST_DMEM static struct platform_thread_fixture thread_fixture;

static void *
write_result(void *arg)
{
    struct thread_result *result = arg;

    result->output = result->input * 2;
    result->writes++;
    return NULL;
}

static void *
record_identity(void *arg)
{
    struct identity_arg *identity_arg = arg;
    struct identity_state *state = identity_arg->state;

    state->handles[identity_arg->slot] = os_self_thread();
    return NULL;
}

static void *
block_for_stack_recovery(void *arg)
{
    struct blocked_thread_state *state = arg;

    atomic_inc(&state->ready);
    while (!atomic_get(&state->release)) {
        k_sleep(K_MSEC(1));
    }
    atomic_inc(&state->exited);
    return NULL;
}

static void *
return_nested_grandchild_result(void *arg)
{
    ARG_UNUSED(arg);
    return NESTED_GRANDCHILD_RESULT;
}

static void *
create_and_join_grandchild(void *arg)
{
    struct nested_thread_state *state = arg;
    korp_tid grandchild;

    state->create_result =
        os_thread_create(&grandchild, return_nested_grandchild_result, NULL,
                         WAMR_TEST_STACK_SIZE);
    if (state->create_result != BHT_OK) {
        return NULL;
    }

    state->join_result = os_thread_join(grandchild, &state->grandchild_result);
    return state->join_result == BHT_OK
                   && state->grandchild_result == NESTED_GRANDCHILD_RESULT
               ? NESTED_CHILD_RESULT
               : NULL;
}

static void *
publish_exit(void *arg)
{
    atomic_set(arg, 1);
    return NULL;
}

static void *
return_argument(void *arg)
{
    return arg;
}

void
wamr_zephyr_thread_join_test_hook(int phase)
{
    struct join_detach_race_state *join_detach_state =
        &thread_fixture.join_detach_race;
    struct detached_reuse_race_state *reuse_state =
        &thread_fixture.detached_reuse_race;
    struct concurrent_join_state *state = &thread_fixture.concurrent_join;

    if (atomic_get(&reuse_state->hook_active)
        && phase == WAMR_JOIN_TEST_ENTERED) {
        atomic_set(&reuse_state->join_paused, 1);
        while (!atomic_get(&reuse_state->release_join)) {
            k_sleep(K_MSEC(1));
        }
        return;
    }

    if (atomic_get(&join_detach_state->hook_active)
        && phase == WAMR_JOIN_TEST_CLAIMED) {
        atomic_set(&join_detach_state->join_claimed, 1);
        while (!atomic_get(&join_detach_state->release_join)) {
            k_sleep(K_MSEC(1));
        }
        return;
    }

    if (!atomic_get(&state->hook_active)) {
        return;
    }

    if (phase == WAMR_JOIN_TEST_BEFORE_CLAIM) {
        atomic_inc(&state->before_claim_arrivals);
        while (atomic_get(&state->before_claim_arrivals) < 2) {
            k_yield();
        }
    }
    else if (phase == WAMR_JOIN_TEST_UNPROTECTED_LOOKUP) {
        if (atomic_inc(&state->unprotected_lookup_arrivals) == 0) {
            while (!atomic_get(&state->winner_done)) {
                k_yield();
            }
        }
    }
    else if (phase == WAMR_JOIN_TEST_PROTECTED_LOOKUP) {
        if (atomic_inc(&state->protected_lookup_arrivals) != 0) {
            while (!atomic_get(&state->cleanup_started)) {
                k_yield();
            }
        }
    }
    else if (phase == WAMR_JOIN_TEST_CLAIMED) {
        while (atomic_get(&state->protected_lookup_arrivals) < 2) {
            k_yield();
        }
    }
    else if (phase == WAMR_JOIN_TEST_BEFORE_REMOVE) {
        atomic_set(&state->cleanup_started, 1);
    }
}

void
wamr_zephyr_thread_detach_test_hook(int phase)
{
    struct detach_after_exit_race_state *state =
        &thread_fixture.detach_after_exit_race;

    if (!atomic_get(&state->hook_active)
        || phase != WAMR_DETACH_TEST_EXITED_CLAIMED) {
        return;
    }

    atomic_set(&state->target_claimed, 1);
    while (!atomic_get(&state->release_detacher)) {
        k_sleep(K_MSEC(1));
    }
}

static void *
join_target_concurrently(void *arg)
{
    struct concurrent_join_arg *join_arg = arg;
    struct concurrent_join_state *state = join_arg->state;
    int result = os_thread_join(state->target, NULL);

    state->join_results[join_arg->slot] = result;
    if (result == BHT_OK && atomic_cas(&state->replacement_pending, 1, 0)) {
        state->replacement_create_result = os_thread_create(
            &state->replacement, return_argument, NULL, WAMR_TEST_STACK_SIZE);
        if (state->replacement_create_result == BHT_OK) {
            (void)wamr_zephyr_thread_test_wait(state->replacement, K_FOREVER);
        }
        atomic_set(&state->winner_done, 1);
    }

    return NULL;
}

static void *
join_detached_target(void *arg)
{
    struct detached_join_state *state = arg;

    state->result = os_thread_join(state->target, NULL);
    atomic_set(&state->completed, 1);
    return NULL;
}

static void *
join_before_competing_detach(void *arg)
{
    struct join_detach_race_state *state = arg;

    state->join_result = os_thread_join(state->target, NULL);
    return NULL;
}

static void *
detach_exited_target(void *arg)
{
    struct detach_after_exit_race_state *state = arg;

    state->detach_result = os_thread_detach(state->target);
    atomic_set(&state->detacher_done, 1);
    return NULL;
}

static void *
join_detached_target_during_reuse(void *arg)
{
    struct detached_reuse_race_state *state = arg;

    while (!atomic_get(&state->start_join)) {
        k_sleep(K_MSEC(1));
    }
    state->join_result = os_thread_join(state->target, NULL);
    atomic_set(&state->join_done, 1);
    return NULL;
}

static void *
create_replacement_during_join(void *arg)
{
    struct detached_reuse_race_state *state = arg;

    while (!atomic_get(&state->start_creator)) {
        k_sleep(K_MSEC(1));
    }
    atomic_set(&state->creator_started, 1);
    state->replacement_create_result =
        os_thread_create(&state->replacement, block_for_stack_recovery,
                         &state->replacement_blocked, WAMR_TEST_STACK_SIZE);
    atomic_set(&state->creator_done, 1);
    return NULL;
}

static void *
detach_and_exit_explicitly(void *arg)
{
    struct explicit_exit_state *state = arg;

    state->detach_result = os_thread_detach(os_self_thread());
    atomic_set(&state->exit_started, 1);
    os_thread_exit(EXPLICIT_EXIT_RESULT);
    atomic_set(&state->exit_returned, 1);
    return NULL;
}

static void
reset_result(struct thread_result *result, int input)
{
    memset(result, 0, sizeof(*result));
    result->input = input;
}

static bool
wait_for_atomic_count(atomic_t *counter, size_t expected)
{
    int64_t deadline = k_uptime_get() + THREAD_READY_TIMEOUT_MS;

    while ((size_t)atomic_get(counter) < expected
           && k_uptime_get() < deadline) {
        k_sleep(K_MSEC(1));
    }

    return (size_t)atomic_get(counter) >= expected;
}

ZTEST_SUITE(platform_thread, NULL, NULL, wamr_thread_test_before, pool_after,
            NULL);

WAMR_CONTEXT_TEST(platform_thread,
                  test_platform_lifecycle_exposes_current_thread)
{
    zassert_not_null(os_self_thread(), "current thread is unavailable");
}

WAMR_CONTEXT_TEST(platform_thread, test_create_and_join)
{
    korp_tid thread;

    reset_result(&thread_fixture.result, 1);
    zassert_equal(os_thread_create(&thread, write_result,
                                   &thread_fixture.result,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    zassert_equal(os_thread_join(thread, NULL), BHT_OK, "thread join failed");
    zassert_equal(thread_fixture.result.writes, 1U,
                  "thread did not run exactly once");
}

WAMR_CONTEXT_TEST(platform_thread, test_argument_reaches_thread)
{
    korp_tid thread;

    reset_result(&thread_fixture.result, 21);
    zassert_equal(os_thread_create(&thread, write_result,
                                   &thread_fixture.result,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    zassert_equal(os_thread_join(thread, NULL), BHT_OK, "thread join failed");
    zassert_equal(thread_fixture.result.output, 42,
                  "thread did not receive its argument");
}

WAMR_CONTEXT_TEST(platform_thread, test_join_after_child_exit)
{
    korp_tid thread;

    atomic_clear(&thread_fixture.child_exited);
    zassert_equal(os_thread_create(&thread, publish_exit,
                                   &thread_fixture.child_exited,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    zassert_equal(wamr_zephyr_thread_test_wait(thread, K_FOREVER), 0,
                  "child did not exit before WAMR join");
    zassert_equal(atomic_get(&thread_fixture.child_exited), 1,
                  "child did not publish its exit");
    zassert_equal(os_thread_join(thread, NULL), BHT_OK,
                  "join after child exit failed");
}

WAMR_CONTEXT_TEST(platform_thread, test_join_propagates_return_value)
{
    void *expected = (void *)0x1234U;
    void *actual = NULL;
    korp_tid thread;

    zassert_equal(os_thread_create(&thread, return_argument, expected,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    zassert_equal(os_thread_join(thread, &actual), BHT_OK,
                  "thread join failed");
    zassert_equal_ptr(actual, expected, "thread return value was lost");
}

WAMR_CONTEXT_TEST(platform_thread, test_second_join_is_rejected)
{
    korp_tid thread;

    zassert_equal(
        os_thread_create(&thread, return_argument, NULL, WAMR_TEST_STACK_SIZE),
        BHT_OK, "thread creation failed");
    zassert_equal(os_thread_join(thread, NULL), BHT_OK, "thread join failed");
    zassert_equal(os_thread_join(thread, NULL), BHT_ERROR,
                  "second join unexpectedly claimed released metadata");
}

WAMR_CONTEXT_TEST(platform_thread, test_concurrent_join_has_one_owner)
{
    struct concurrent_join_state *state = &thread_fixture.concurrent_join;
    korp_tid joiners[2];
    unsigned int successful_joins = 0U;

    memset(state, 0, sizeof(*state));
    state->join_results[0] = BHT_ERROR;
    state->join_results[1] = BHT_ERROR;
    state->replacement_create_result = BHT_ERROR;
    atomic_set(&state->replacement_pending, 1);
    zassert_equal(os_thread_create(&state->target, return_argument, NULL,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "target creation failed");
    zassert_equal(wamr_zephyr_thread_test_wait(state->target, K_FOREVER), 0,
                  "target did not exit before concurrent joins");

    atomic_set(&state->hook_active, 1);
    for (size_t i = 0; i < ARRAY_SIZE(joiners); ++i) {
        thread_fixture.concurrent_join_args[i].state = state;
        thread_fixture.concurrent_join_args[i].slot = i;
        zassert_equal(os_thread_create(&joiners[i], join_target_concurrently,
                                       &thread_fixture.concurrent_join_args[i],
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "joiner creation failed at index %zu", i);
    }

    zassert_equal(wamr_zephyr_thread_test_wait(joiners[0], K_FOREVER), 0,
                  "first joiner did not exit");
    zassert_equal(wamr_zephyr_thread_test_wait(joiners[1], K_FOREVER), 0,
                  "second joiner did not exit");
    atomic_clear(&state->hook_active);

    zassert_equal(os_thread_join(joiners[0], NULL), BHT_OK,
                  "first joiner cleanup failed");
    zassert_equal(os_thread_join(joiners[1], NULL), BHT_OK,
                  "second joiner cleanup failed");
    if (state->replacement_create_result == BHT_OK) {
        (void)os_thread_join(state->replacement, NULL);
    }
    for (size_t i = 0; i < ARRAY_SIZE(state->join_results); ++i) {
        successful_joins += state->join_results[i] == BHT_OK;
    }

    zassert_equal(state->replacement_create_result, BHT_OK,
                  "winning joiner could not create a replacement generation");
    zassert_equal(successful_joins, 1U,
                  "concurrent joiners claimed two metadata generations");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_join_claim_serializes_against_competing_detach)
{
    struct join_detach_race_state *state = &thread_fixture.join_detach_race;
    korp_tid target;
    korp_tid joiner;
    int detach_result;

    memset(state, 0, sizeof(*state));
    atomic_clear(&thread_fixture.blocked.ready);
    atomic_clear(&thread_fixture.blocked.release);
    atomic_clear(&thread_fixture.blocked.exited);
    state->join_result = BHT_ERROR;

    zassert_equal(os_thread_create(&target, block_for_stack_recovery,
                                   &thread_fixture.blocked,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "target creation failed");
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.ready, 1U),
                 "target did not block");

    state->target = target;
    atomic_set(&state->hook_active, 1);
    zassert_equal(os_thread_create(&joiner, join_before_competing_detach, state,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "joiner creation failed");
    zassert_true(wait_for_atomic_count(&state->join_claimed, 1U),
                 "joiner did not claim lifecycle ownership");

    detach_result = os_thread_detach(target);
    atomic_set(&thread_fixture.blocked.release, 1);
    atomic_set(&state->release_join, 1);
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.exited, 1U),
                 "target did not publish exit");
    zassert_equal(wamr_zephyr_thread_test_wait(joiner, K_FOREVER), 0,
                  "joiner did not exit");
    atomic_clear(&state->hook_active);
    zassert_equal(os_thread_join(joiner, NULL), BHT_OK,
                  "joiner cleanup failed");

    zassert_equal(detach_result, BHT_ERROR,
                  "detach stole cleanup ownership from a claimed join");
    zassert_equal(state->join_result, BHT_OK,
                  "claimed join did not retain cleanup ownership");
}

WAMR_CONTEXT_TEST(platform_thread, test_thread_identities_are_distinct)
{
    /* Mutation caught: always choosing pool slot zero. */
    korp_tid threads[2];
    korp_tid parent = os_self_thread();

    memset(&thread_fixture.identities, 0, sizeof(thread_fixture.identities));
    thread_fixture.identity_args[0].state = &thread_fixture.identities;
    thread_fixture.identity_args[0].slot = 0U;
    thread_fixture.identity_args[1].state = &thread_fixture.identities;
    thread_fixture.identity_args[1].slot = 1U;
    zassert_equal(os_thread_create(&threads[0], record_identity,
                                   &thread_fixture.identity_args[0],
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "first thread creation failed");
    zassert_equal(os_thread_create(&threads[1], record_identity,
                                   &thread_fixture.identity_args[1],
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "second thread creation failed");
    zassert_equal(os_thread_join(threads[0], NULL), BHT_OK,
                  "first thread join failed");
    zassert_equal(os_thread_join(threads[1], NULL), BHT_OK,
                  "second thread join failed");
    zassert_not_null(thread_fixture.identities.handles[0],
                     "first thread identity is null");
    zassert_not_null(thread_fixture.identities.handles[1],
                     "second thread identity is null");
    zassert_not_equal(thread_fixture.identities.handles[0],
                      thread_fixture.identities.handles[1],
                      "thread identities are shared");
    zassert_not_equal(thread_fixture.identities.handles[0], parent,
                      "first thread has the parent identity");
    zassert_not_equal(thread_fixture.identities.handles[1], parent,
                      "second thread has the parent identity");
}

WAMR_CONTEXT_TEST(platform_thread, test_multiple_threads_leave_no_stale_state)
{
    /* Mutation caught: leaving metadata from a stale slot generation. */

    memset(thread_fixture.results, 0, sizeof(thread_fixture.results));
    for (size_t i = 0; i < ARRAY_SIZE(thread_fixture.results); ++i) {
        korp_tid thread;

        thread_fixture.results[i].input = i + 1;
        zassert_equal(os_thread_create(&thread, write_result,
                                       &thread_fixture.results[i],
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
        zassert_equal(os_thread_join(thread, NULL), BHT_OK,
                      "thread join failed at index %zu", i);
        zassert_equal(thread_fixture.results[i].writes, 1U,
                      "thread wrote its result an unexpected number of times");
        zassert_equal(thread_fixture.results[i].output,
                      thread_fixture.results[i].input * 2,
                      "thread left stale result state");
    }
}

WAMR_CONTEXT_TEST(platform_thread, test_nested_thread_creation_inherits_access)
{
    struct nested_thread_state *state = &thread_fixture.nested;
    korp_tid child;
    void *child_result = NULL;

    /* Mutations caught: removing K_INHERIT_PERMS or WAMR-domain inheritance. */
    memset(state, 0, sizeof(*state));
    state->create_result = BHT_ERROR;
    state->join_result = BHT_ERROR;
    zassert_equal(os_thread_create(&child, create_and_join_grandchild, state,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "child creation failed");
    zassert_equal(os_thread_join(child, &child_result), BHT_OK,
                  "child join failed");
    zassert_equal(state->create_result, BHT_OK,
                  "nested grandchild creation failed");
    zassert_equal(state->join_result, BHT_OK, "nested grandchild join failed");
    zassert_equal_ptr(state->grandchild_result, NESTED_GRANDCHILD_RESULT,
                      "grandchild literal result was lost");
    zassert_equal_ptr(child_result, NESTED_CHILD_RESULT,
                      "child did not report the nested literal result");
}

WAMR_CONTEXT_TEST(platform_thread, test_create_rejects_null_tid)
{
    zassert_equal(
        os_thread_create(NULL, write_result, NULL, WAMR_TEST_STACK_SIZE),
        BHT_ERROR, NULL);
}

WAMR_CONTEXT_TEST(platform_thread, test_create_rejects_null_start)
{
    korp_tid thread;

    zassert_equal(os_thread_create(&thread, NULL, NULL, WAMR_TEST_STACK_SIZE),
                  BHT_ERROR, NULL);
}

WAMR_CONTEXT_TEST(platform_thread, test_create_rejects_zero_stack)
{
    korp_tid thread;

    zassert_equal(os_thread_create(&thread, write_result, NULL, 0), BHT_ERROR,
                  NULL);
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_oversized_stack_is_rejected_without_consuming_slot)
{
    korp_tid threads[BH_ZEPHYR_MPU_STACK_COUNT];
    int oversized_result;
    int cleanup_result = BHT_OK;

    /* Mutations caught: ignoring configured stack size or failing rollback. */
    oversized_result = os_thread_create(&threads[0], return_argument, NULL,
                                        BH_ZEPHYR_MPU_STACK_SIZE + 1U);
    if (oversized_result == BHT_OK) {
        cleanup_result = os_thread_join(threads[0], NULL);
    }
    zassert_equal(oversized_result, BHT_ERROR,
                  "oversized stack request was accepted");
    zassert_equal(cleanup_result, BHT_OK,
                  "unexpected oversized thread cleanup failed");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_create(&threads[i], return_argument, NULL,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "rejected oversized request consumed slot %zu",
                      i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_join(threads[i], NULL), BHT_OK,
                      "thread join failed at index %zu", i);
    }
}

#if defined(CONFIG_WAMR_TEST_USER_MODE)
WAMR_CONTEXT_TEST(platform_thread, test_create_rejects_out_of_range_priority)
{
    korp_tid thread;

    zassert_equal(
        os_thread_create_with_prio(&thread, write_result,
                                   &thread_fixture.result, WAMR_TEST_STACK_SIZE,
                                   K_LOWEST_APPLICATION_THREAD_PRIO + 1),
        BHT_ERROR, "out-of-range priority was accepted");
}

WAMR_CONTEXT_TEST(platform_thread, test_create_rejects_disallowed_priority)
{
    korp_tid thread;
    int disallowed_priority = k_thread_priority_get(k_current_get()) - 1;

    zassert_equal(os_thread_create_with_prio(
                      &thread, write_result, &thread_fixture.result,
                      WAMR_TEST_STACK_SIZE, disallowed_priority),
                  BHT_ERROR, "disallowed priority was accepted");
}
#endif

WAMR_CONTEXT_TEST(platform_thread, test_stack_pool_recovers_after_exhaustion)
{
    korp_tid threads[MAX_BLOCKED_THREADS];
    korp_tid recovery_thread;
    int join_results[BH_ZEPHYR_MPU_STACK_COUNT];
    int unexpected_join_result = BHT_OK;
    int exhaustion_result;

    /* Mutations caught: reusing slot zero or failing exhaustion rollback. */
    atomic_clear(&thread_fixture.blocked.ready);
    atomic_clear(&thread_fixture.blocked.release);
    atomic_clear(&thread_fixture.blocked.exited);
    for (size_t i = 0; i < BH_ZEPHYR_MPU_STACK_COUNT; ++i) {
        zassert_equal(os_thread_create(&threads[i], block_for_stack_recovery,
                                       &thread_fixture.blocked,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
        zassert_true(
            wait_for_atomic_count(&thread_fixture.blocked.ready, i + 1U),
            "thread did not block at index %zu", i);
        for (size_t j = 0; j < i; ++j) {
            zassert_not_equal(threads[i], threads[j],
                              "threads %zu and %zu share a pool slot", i, j);
        }
    }
    exhaustion_result = os_thread_create(
        &threads[BH_ZEPHYR_MPU_STACK_COUNT], block_for_stack_recovery,
        &thread_fixture.blocked, WAMR_TEST_STACK_SIZE);
    atomic_set(&thread_fixture.blocked.release, 1);
    for (size_t i = 0; i < BH_ZEPHYR_MPU_STACK_COUNT; ++i) {
        join_results[i] = os_thread_join(threads[i], NULL);
    }
    if (exhaustion_result == BHT_OK) {
        unexpected_join_result =
            os_thread_join(threads[BH_ZEPHYR_MPU_STACK_COUNT], NULL);
    }
    zassert_equal(exhaustion_result, BHT_ERROR,
                  "fifth reservation did not report exhaustion");
    zassert_equal(unexpected_join_result, BHT_OK,
                  "unexpected fifth thread cleanup failed");
    for (size_t i = 0; i < BH_ZEPHYR_MPU_STACK_COUNT; ++i) {
        zassert_equal(join_results[i], BHT_OK,
                      "thread join failed at index %zu", i);
    }
    reset_result(&thread_fixture.result, 1);
    zassert_equal(os_thread_create(&recovery_thread, write_result,
                                   &thread_fixture.result,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "stack pool did not recover");
    zassert_equal(os_thread_join(recovery_thread, NULL), BHT_OK,
                  "recovery thread join failed");
    zassert_equal(thread_fixture.result.writes, 1U,
                  "recovery thread did not run");
}

WAMR_CONTEXT_TEST(platform_thread, test_exited_threads_keep_slots_until_join)
{
    korp_tid threads[BH_ZEPHYR_MPU_STACK_COUNT];
    korp_tid replacement;

    /* Mutation caught: releasing a joinable slot at exit rather than join. */
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_create(&threads[i], return_argument, NULL,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(wamr_zephyr_thread_test_wait(threads[i], K_FOREVER), 0,
                      "thread did not exit at index %zu", i);
    }
    zassert_equal(os_thread_create(&replacement, return_argument, NULL,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_ERROR, "exited slot was released before WAMR join");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_join(threads[i], NULL), BHT_OK,
                      "thread join failed at index %zu", i);
    }
    zassert_equal(os_thread_create(&replacement, return_argument, NULL,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "joined slot was not recycled");
    zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                  "replacement join failed");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_detached_running_thread_releases_slot_on_exit)
{
    korp_tid threads[BH_ZEPHYR_MPU_STACK_COUNT];
    korp_tid replacement;
    int replacement_create_result;

    /* Mutation caught: leaving a detached running generation retained. */
    atomic_clear(&thread_fixture.blocked.ready);
    atomic_clear(&thread_fixture.blocked.release);
    atomic_clear(&thread_fixture.blocked.exited);
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_create(&threads[i], block_for_stack_recovery,
                                       &thread_fixture.blocked,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
        zassert_true(
            wait_for_atomic_count(&thread_fixture.blocked.ready, i + 1U),
            "thread did not block at index %zu", i);
    }
    zassert_equal(os_thread_detach(threads[0]), BHT_OK,
                  "running-thread detach failed");

    atomic_set(&thread_fixture.blocked.release, 1);
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.exited,
                                       ARRAY_SIZE(threads)),
                 "blocked threads did not publish exit");
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(wamr_zephyr_thread_test_wait(
                          threads[i], K_MSEC(THREAD_READY_TIMEOUT_MS)),
                      0, "thread did not exit at index %zu", i);
    }

    replacement_create_result = os_thread_create(&replacement, return_argument,
                                                 NULL, WAMR_TEST_STACK_SIZE);
    if (replacement_create_result == BHT_OK) {
        zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                      "replacement join failed");
    }
    for (size_t i = 1; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_join(threads[i], NULL), BHT_OK,
                      "joinable thread cleanup failed at index %zu", i);
    }
    if (replacement_create_result != BHT_OK) {
        zassert_equal(os_thread_join(threads[0], NULL), BHT_OK,
                      "broken detach path cleanup failed");
    }

    zassert_equal(replacement_create_result, BHT_OK,
                  "detached running thread did not release its slot on exit");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_detach_after_exit_releases_slot_immediately)
{
    korp_tid threads[BH_ZEPHYR_MPU_STACK_COUNT];
    korp_tid replacement;
    int replacement_create_result;

    /* Mutation caught: retaining an exited generation after detach. */
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_create(&threads[i], return_argument, NULL,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
    }
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(wamr_zephyr_thread_test_wait(
                          threads[i], K_MSEC(THREAD_READY_TIMEOUT_MS)),
                      0, "thread did not exit at index %zu", i);
    }
    zassert_equal(os_thread_detach(threads[0]), BHT_OK,
                  "exited-thread detach failed");

    replacement_create_result = os_thread_create(&replacement, return_argument,
                                                 NULL, WAMR_TEST_STACK_SIZE);
    if (replacement_create_result == BHT_OK) {
        zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                      "replacement join failed");
    }
    for (size_t i = 1; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_join(threads[i], NULL), BHT_OK,
                      "joinable thread cleanup failed at index %zu", i);
    }
    if (replacement_create_result != BHT_OK) {
        zassert_equal(os_thread_join(threads[0], NULL), BHT_OK,
                      "broken detach path cleanup failed");
    }

    zassert_equal(replacement_create_result, BHT_OK,
                  "detach did not release an exited thread slot");
}

WAMR_CONTEXT_TEST(platform_thread, test_join_rejects_detached_running_thread)
{
    struct detached_join_state *state = &thread_fixture.detached_join;
    korp_tid thread;
    korp_tid joiner;
    korp_tid replacement;
    bool join_completed_while_target_running;

    /* Mutation caught: allowing join ownership after detach owns cleanup. */
    atomic_clear(&thread_fixture.blocked.ready);
    atomic_clear(&thread_fixture.blocked.release);
    atomic_clear(&thread_fixture.blocked.exited);
    atomic_clear(&state->completed);
    state->result = BHT_ERROR;
    zassert_equal(os_thread_create(&thread, block_for_stack_recovery,
                                   &thread_fixture.blocked,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "thread creation failed");
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.ready, 1U),
                 "thread did not block");
    zassert_equal(os_thread_detach(thread), BHT_OK,
                  "running-thread detach failed");
    state->target = thread;
    zassert_equal(os_thread_create(&joiner, join_detached_target, state,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "join-attempt thread creation failed");
    join_completed_while_target_running =
        wait_for_atomic_count(&state->completed, 1U);

    atomic_set(&thread_fixture.blocked.release, 1);
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.exited, 1U),
                 "thread did not publish exit");
    zassert_equal(
        wamr_zephyr_thread_test_wait(thread, K_MSEC(THREAD_READY_TIMEOUT_MS)),
        0, "thread did not exit");
    zassert_true(wait_for_atomic_count(&state->completed, 1U),
                 "join-attempt thread did not finish after target exit");
    zassert_equal(os_thread_join(joiner, NULL), BHT_OK,
                  "join-attempt thread cleanup failed");

    zassert_equal(os_thread_create(&replacement, return_argument, NULL,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "join rejection corrupted the released slot");
    zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                  "replacement join failed");
    zassert_true(join_completed_while_target_running,
                 "join blocked on a detached running thread");
    zassert_equal(state->result, BHT_ERROR,
                  "join unexpectedly claimed a detached thread");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_detach_after_exit_keeps_exclusive_cleanup_ownership)
{
    struct detach_after_exit_race_state *state =
        &thread_fixture.detach_after_exit_race;
    korp_tid threads[BH_ZEPHYR_MPU_STACK_COUNT];
    korp_tid detacher;
    korp_tid replacement;
    int first_create_result;
    int retry_create_result;

    /* Mutation caught: publishing exited detach to the generic reaper. */
    for (size_t i = 0; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_create(&threads[i], return_argument, NULL,
                                       WAMR_TEST_STACK_SIZE),
                      BHT_OK, "thread creation failed at index %zu", i);
        zassert_equal(wamr_zephyr_thread_test_wait(
                          threads[i], K_MSEC(THREAD_READY_TIMEOUT_MS)),
                      0, "thread did not exit at index %zu", i);
    }
    zassert_equal(os_thread_join(threads[1], NULL), BHT_OK,
                  "detacher slot preparation failed");

    memset(state, 0, sizeof(*state));
    state->target = threads[0];
    state->detach_result = BHT_ERROR;
    atomic_set(&state->hook_active, 1);
    zassert_equal(os_thread_create(&detacher, detach_exited_target, state,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "detacher creation failed");
    zassert_true(wait_for_atomic_count(&state->target_claimed, 1U),
                 "detacher did not claim the exited target");

    first_create_result = os_thread_create(&replacement, return_argument, NULL,
                                           WAMR_TEST_STACK_SIZE);
    if (first_create_result == BHT_OK) {
        zassert_equal(wamr_zephyr_thread_test_wait(
                          replacement, K_MSEC(THREAD_READY_TIMEOUT_MS)),
                      0, "unexpected replacement did not exit");
    }
    atomic_set(&state->release_detacher, 1);
    zassert_true(wait_for_atomic_count(&state->detacher_done, 1U),
                 "detacher did not finish");
    atomic_clear(&state->hook_active);

    zassert_equal(os_thread_join(detacher, NULL), BHT_OK,
                  "detacher cleanup failed");
    if (first_create_result == BHT_OK) {
        zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                      "unexpected replacement cleanup failed");
    }
    retry_create_result = os_thread_create(&replacement, return_argument, NULL,
                                           WAMR_TEST_STACK_SIZE);
    if (retry_create_result == BHT_OK) {
        zassert_equal(os_thread_join(replacement, NULL), BHT_OK,
                      "post-detach replacement cleanup failed");
    }
    for (size_t i = 2; i < ARRAY_SIZE(threads); ++i) {
        zassert_equal(os_thread_join(threads[i], NULL), BHT_OK,
                      "retained thread cleanup failed at index %zu", i);
    }

    zassert_equal(state->detach_result, BHT_OK, "exited-thread detach failed");
    zassert_equal(first_create_result, BHT_ERROR,
                  "creator reused the target during detach cleanup");
    zassert_equal(retry_create_result, BHT_OK,
                  "target slot was not released after detach cleanup");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_detached_handle_join_cannot_bind_replacement)
{
    struct detached_reuse_race_state *state =
        &thread_fixture.detached_reuse_race;
    korp_tid target;
    korp_tid creator;
    korp_tid joiner;
    bool creator_completed_before_join_release;
    bool join_completed_before_replacement_release = false;

    /* Mutation caught: stale join lookup binding a reused static k_tid. */
    atomic_clear(&thread_fixture.blocked.ready);
    atomic_clear(&thread_fixture.blocked.release);
    atomic_clear(&thread_fixture.blocked.exited);
    zassert_equal(os_thread_create(&target, block_for_stack_recovery,
                                   &thread_fixture.blocked,
                                   WAMR_TEST_STACK_SIZE),
                  BHT_OK, "target creation failed");
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.ready, 1U),
                 "target did not block");
    zassert_equal(os_thread_detach(target), BHT_OK, "target detach failed");

    memset(state, 0, sizeof(*state));
    state->target = target;
    state->join_result = BHT_OK;
    state->replacement_create_result = BHT_ERROR;
    atomic_set(&state->hook_active, 1);
    zassert_equal(os_thread_create(&creator, create_replacement_during_join,
                                   state, WAMR_TEST_STACK_SIZE),
                  BHT_OK, "creator-driver creation failed");
    zassert_equal(os_thread_create(&joiner, join_detached_target_during_reuse,
                                   state, WAMR_TEST_STACK_SIZE),
                  BHT_OK, "stale-join driver creation failed");

    atomic_set(&thread_fixture.blocked.release, 1);
    zassert_true(wait_for_atomic_count(&thread_fixture.blocked.exited, 1U),
                 "target did not publish exit");
    zassert_equal(
        wamr_zephyr_thread_test_wait(target, K_MSEC(THREAD_READY_TIMEOUT_MS)),
        0, "target did not exit");
    atomic_set(&state->start_join, 1);
    zassert_true(wait_for_atomic_count(&state->join_paused, 1U),
                 "stale join did not reach the lookup boundary");

    atomic_set(&state->start_creator, 1);
    zassert_true(wait_for_atomic_count(&state->creator_started, 1U),
                 "creator driver did not start");
    creator_completed_before_join_release =
        wait_for_atomic_count(&state->creator_done, 1U);
    atomic_set(&state->release_join, 1);
    zassert_true(wait_for_atomic_count(&state->creator_done, 1U),
                 "creator driver did not finish");

    if (state->replacement_create_result == BHT_OK) {
        zassert_true(
            wait_for_atomic_count(&state->replacement_blocked.ready, 1U),
            "replacement did not block");
        join_completed_before_replacement_release =
            wait_for_atomic_count(&state->join_done, 1U);
        atomic_set(&state->replacement_blocked.release, 1);
        zassert_true(
            wait_for_atomic_count(&state->replacement_blocked.exited, 1U),
            "replacement did not publish exit");
    }
    zassert_true(wait_for_atomic_count(&state->join_done, 1U),
                 "stale-join driver did not finish");
    atomic_clear(&state->hook_active);

    zassert_equal(
        wamr_zephyr_thread_test_wait(creator, K_MSEC(THREAD_READY_TIMEOUT_MS)),
        0, "creator driver did not exit");
    zassert_equal(
        wamr_zephyr_thread_test_wait(joiner, K_MSEC(THREAD_READY_TIMEOUT_MS)),
        0, "stale-join driver did not exit");
    zassert_equal(os_thread_join(creator, NULL), BHT_OK,
                  "creator-driver cleanup failed");
    zassert_equal(os_thread_join(joiner, NULL), BHT_OK,
                  "stale-join driver cleanup failed");
    if (state->replacement_create_result == BHT_OK
        && state->join_result == BHT_ERROR) {
        zassert_equal(os_thread_join(state->replacement, NULL), BHT_OK,
                      "replacement cleanup failed");
    }

    zassert_true(creator_completed_before_join_release,
                 "creator did not reuse the slot before stale join lookup");
    zassert_equal(state->replacement_create_result, BHT_OK,
                  "replacement generation was not created");
    zassert_true(join_completed_before_replacement_release,
                 "stale join blocked on the replacement generation");
    zassert_equal(state->join_result, BHT_ERROR,
                  "stale detached handle joined the replacement generation");
}

WAMR_CONTEXT_TEST(platform_thread,
                  test_detached_explicit_thread_exit_releases_slot)
{
    struct explicit_exit_state *state = &thread_fixture.explicit_exit;
    korp_tid retained[BH_ZEPHYR_MPU_STACK_COUNT - 1U] = { NULL };
    korp_tid thread = NULL;
    korp_tid replacement = NULL;
    size_t created = 0U;
    int retained_create_result = BHT_OK;
    int target_create_result = BHT_ERROR;
    int target_wait_result = BHT_ERROR;
    int replacement_create_result = BHT_ERROR;
    int replacement_join_result = BHT_ERROR;
    bool cleanup_succeeded = true;
    bool exit_started = false;

    /* Mutation caught: explicit exit bypassing detached completion cleanup. */
    memset(state, 0, sizeof(*state));
    state->detach_result = BHT_ERROR;
    /* Keep the other joinable generations allocated until replacement.
     * A spare stack must not conceal failure to reclaim the detached slot.
     */
    for (; created < ARRAY_SIZE(retained); created++) {
        retained_create_result = os_thread_create(
            &retained[created], return_argument, NULL, WAMR_TEST_STACK_SIZE);
        if (retained_create_result != BHT_OK) {
            break;
        }
    }
    if (retained_create_result == BHT_OK) {
        target_create_result = os_thread_create(
            &thread, detach_and_exit_explicitly, state, WAMR_TEST_STACK_SIZE);
    }
    if (target_create_result == BHT_OK) {
        exit_started = wait_for_atomic_count(&state->exit_started, 1U);
        target_wait_result = wamr_zephyr_thread_test_wait(
            thread, K_MSEC(THREAD_READY_TIMEOUT_MS));
        if (exit_started && target_wait_result == 0) {
            replacement_create_result = os_thread_create(
                &replacement, return_argument, NULL, WAMR_TEST_STACK_SIZE);
        }
    }
    if (replacement_create_result == BHT_OK) {
        replacement_join_result = os_thread_join(replacement, NULL);
    }
    while (created > 0U) {
        if (os_thread_join(retained[--created], NULL) != BHT_OK) {
            cleanup_succeeded = false;
        }
    }

    zassert_equal(retained_create_result, BHT_OK, "retained create failed");
    zassert_equal(target_create_result, BHT_OK,
                  "explicit-exit thread creation failed");
    zassert_true(exit_started, "explicit-exit thread did not detach");
    zassert_equal(target_wait_result, 0,
                  "explicit-exit thread did not terminate");
    zassert_equal(replacement_create_result, BHT_OK,
                  "explicit exit did not release the detached slot");
    zassert_equal(replacement_join_result, BHT_OK,
                  "explicit-exit replacement cleanup failed");
    zassert_true(cleanup_succeeded, "retained generations were not joined");
    zassert_equal(state->detach_result, BHT_OK, "self-detach failed");
    zassert_false(atomic_get(&state->exit_returned),
                  "os_thread_exit unexpectedly returned");
}

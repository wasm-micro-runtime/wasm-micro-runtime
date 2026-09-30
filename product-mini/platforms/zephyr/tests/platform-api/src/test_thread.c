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
#define NESTED_GRANDCHILD_RESULT ((void *)0x2468U)
#define NESTED_CHILD_RESULT ((void *)0x1357U)

BUILD_ASSERT(!__builtin_types_compatible_p(korp_tid, k_tid_t),
             "WAMR thread handles must be opaque to Zephyr");

void
wamr_thread_test_before(void *fixture);

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

struct nested_thread_state {
    int create_result;
    int join_result;
    void *grandchild_result;
};

struct platform_thread_fixture {
    struct thread_result result;
    struct thread_result results[4];
    struct identity_state identities;
    struct identity_arg identity_args[2];
    struct nested_thread_state nested;
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
publish_exit(void *arg)
{
    atomic_set(arg, 1);
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
return_argument(void *arg)
{
    return arg;
}

static void
reset_result(struct thread_result *result, int input)
{
    memset(result, 0, sizeof(*result));
    result->input = input;
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
    while (!atomic_get(&thread_fixture.child_exited)) {
        k_sleep(K_MSEC(1));
    }
    k_sleep(K_MSEC(1));
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

WAMR_CONTEXT_TEST(platform_thread, test_thread_identities_are_distinct)
{
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

    zassert_equal(os_thread_create(&threads[0], return_argument, NULL,
                                   BH_ZEPHYR_MPU_STACK_SIZE + 1U),
                  BHT_ERROR, "oversized stack request was accepted");
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

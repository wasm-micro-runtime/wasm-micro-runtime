/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * Native caller identity through the public runtime and C-API surfaces.
 *
 * WAMR identifies a thread it did not create by the thread's own k_tid_t. Two
 * application-created threads must therefore keep separate execution
 * environments and separate C-API store bookkeeping, and keep the same
 * identity across runtime teardown.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "bh_platform.h"
#include "runtime_peers.h"
#include "runtime_user_workflow.h"
#include "wasm_c_api.h"
#include "wasm_c_api_internal.h"
#include "wasm_fixtures.h"

#if defined(CONFIG_WAMR_TEST_USER_MODE)
#include <zephyr/app_memory/app_memdomain.h>
extern struct k_mem_partition wamr_partition;
#define PEER_DATA K_APP_BMEM(wamr_partition)
#define PEER_OPTIONS K_USER
#else
#define PEER_DATA
#define PEER_OPTIONS 0
#endif

#define PEER_STACK_SIZE 8192U
#define PEER_PRIORITY 5
#define PEER_TIMEOUT K_SECONDS(5)
#define PEER_WASM_SIZE 4096U

/* Userspace C-API objects consume slots from the application's fixed mutex
 * pool (16 here). A module and instance per peer exceed it, so the userspace
 * build checks store bookkeeping with bare stores; per-thread Wasm execution
 * is covered by the direct runtime scenario in both builds.
 */
#define C_API_WITH_MODULE (!IS_ENABLED(CONFIG_WAMR_TEST_USER_MODE))

typedef void (*peer_step_t)(unsigned int peer);

struct peer_control {
    peer_step_t step;
    bool stop;
    bool was_user;
};

/* Mirrors the private thread_local_stores row in wasm_c_api.c. The size is
 * checked against the vector's element size before any row is copied.
 */
struct store_row {
    korp_tid tid;
    unsigned stores_num;
};

struct wasm_identity_state {
    struct wamr_test_runtime runtime[RUNTIME_PEER_COUNT];
    korp_tid identity[RUNTIME_PEER_COUNT];
    korp_tid identity_after[RUNTIME_PEER_COUNT];
    korp_tid owner_after_destroy;
    uint32_t result[RUNTIME_PEER_COUNT];
    bool ready[RUNTIME_PEER_COUNT];
    bool called[RUNTIME_PEER_COUNT];
    char error[WAMR_TEST_ERROR_SIZE];
};

struct c_api_peer {
    wasm_store_t *store;
    wasm_store_t *extra_store;
    wasm_module_t *module;
    wasm_instance_t *instance;
    wasm_extern_vec_t exports;
    korp_tid identity;
    uint32_t calls;
    /* Last setup stage reached, reported when setup fails. */
    int stage;
    bool ready;
};

struct c_api_state {
    wasm_engine_t *engine;
    struct c_api_peer peer[RUNTIME_PEER_COUNT];
    bool recreated;
};

static struct k_thread peer_threads[RUNTIME_PEER_COUNT];
K_THREAD_STACK_ARRAY_DEFINE(peer_stacks, RUNTIME_PEER_COUNT, PEER_STACK_SIZE);
static struct k_sem peer_command[RUNTIME_PEER_COUNT];
static struct k_sem peer_done[RUNTIME_PEER_COUNT];
static k_tid_t peer_tids[RUNTIME_PEER_COUNT];

PEER_DATA static struct peer_control peer_control[RUNTIME_PEER_COUNT];
PEER_DATA static uint8_t peer_pool[WAMR_TEST_RUNTIME_POOL_SIZE] __aligned(8);
PEER_DATA static uint8_t peer_wasm[PEER_WASM_SIZE] __aligned(8);
PEER_DATA static struct wasm_identity_state wasm_state;
PEER_DATA static struct c_api_state c_api_state;

static void
peer_entry(void *arg1, void *arg2, void *arg3)
{
    unsigned int peer = (unsigned int)(uintptr_t)arg1;

    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    while (true) {
        k_sem_take(&peer_command[peer], K_FOREVER);
        if (peer_control[peer].stop) {
            return;
        }
        peer_control[peer].was_user = k_is_user_context();
        peer_control[peer].step(peer);
        k_sem_give(&peer_done[peer]);
    }
}

void
runtime_peers_start(runtime_peer_grant_t grant)
{
    for (unsigned int i = 0U; i < RUNTIME_PEER_COUNT; i++) {
        memset(&peer_control[i], 0, sizeof(peer_control[i]));
        k_sem_init(&peer_command[i], 0, 1);
        k_sem_init(&peer_done[i], 0, 1);
        peer_tids[i] =
            k_thread_create(&peer_threads[i], peer_stacks[i],
                            K_THREAD_STACK_SIZEOF(peer_stacks[i]), peer_entry,
                            (void *)(uintptr_t)i, NULL, NULL, PEER_PRIORITY,
                            PEER_OPTIONS, K_FOREVER);
        zassert_not_null(peer_tids[i], "peer %u creation failed", i);
        k_object_access_grant(&peer_command[i], peer_tids[i]);
        k_object_access_grant(&peer_done[i], peer_tids[i]);
        if (grant != NULL) {
            grant(peer_tids[i]);
        }
        k_thread_start(peer_tids[i]);
    }
}

void
runtime_peers_stop(void)
{
    for (unsigned int i = 0U; i < RUNTIME_PEER_COUNT; i++) {
        int joined;

        peer_control[i].stop = true;
        k_sem_give(&peer_command[i]);
        joined = k_thread_join(peer_tids[i], PEER_TIMEOUT);
        if (joined != 0) {
            k_thread_abort(peer_tids[i]);
        }
        zassert_equal(joined, 0, "peer %u did not stop", i);
    }
}

static void
run_on_peer(unsigned int peer, peer_step_t step)
{
    peer_control[peer].step = step;
    k_sem_give(&peer_command[peer]);
    zassert_equal(k_sem_take(&peer_done[peer], PEER_TIMEOUT), 0,
                  "peer %u did not finish its step", peer);
    zassert_equal(peer_control[peer].was_user,
                  IS_ENABLED(CONFIG_WAMR_TEST_USER_MODE),
                  "peer %u ran in the wrong privilege mode", peer);
}

static void
assert_native_identity(korp_tid identity, unsigned int peer)
{
    zassert_equal_ptr(identity, (korp_tid)peer_tids[peer],
                      "peer %u is not identified by its k_tid_t", peer);
}

/* ---- Direct runtime API ---------------------------------------------- */

static void
wasm_owner_start(unsigned int peer)
{
    struct wamr_test_runtime *runtime = &wasm_state.runtime[peer];

    wasm_state.ready[peer] = wamr_test_runtime_start_copy(
        runtime, peer_pool, sizeof(peer_pool), wasm_add, sizeof(wasm_add),
        wasm_state.error, sizeof(wasm_state.error));
    wasm_state.identity[peer] = os_self_thread();
}

static void
wasm_participant_start(unsigned int peer)
{
    struct wamr_test_runtime *runtime = &wasm_state.runtime[peer];
    wasm_module_t module = wasm_state.runtime[0].module;

    memset(runtime, 0, sizeof(*runtime));
    wasm_state.identity[peer] = os_self_thread();
    if (module == NULL || !wasm_runtime_init_thread_env()) {
        return;
    }
    /* Another native thread shares the owner's module but not its instance,
     * execution environment or runtime reference.
     */
    runtime->instance = wasm_runtime_instantiate(
        module, WAMR_TEST_WASM_STACK_SIZE, WAMR_TEST_WASM_HEAP_SIZE,
        wasm_state.error, sizeof(wasm_state.error));
    if (runtime->instance != NULL) {
        runtime->exec_env = wasm_runtime_create_exec_env(
            runtime->instance, WAMR_TEST_WASM_STACK_SIZE);
    }
    wasm_state.ready[peer] = runtime->exec_env != NULL;
}

static void
wasm_call(unsigned int peer)
{
    wasm_state.called[peer] = wamr_test_runtime_call_add(
        &wasm_state.runtime[peer], &wasm_state.result[peer]);
    wasm_state.identity_after[peer] = os_self_thread();
}

static void
wasm_participant_stop(unsigned int peer)
{
    /* Runtime is not initialized here, so stop leaves it to the owner. */
    wamr_test_runtime_stop(&wasm_state.runtime[peer]);
    wasm_runtime_destroy_thread_env();
}

static void
wasm_owner_stop(unsigned int peer)
{
    wamr_test_runtime_stop(&wasm_state.runtime[peer]);
    wasm_state.owner_after_destroy = os_self_thread();
}

void
runtime_peers_check_wasm_identities(void)
{
    korp_tid controller = (korp_tid)k_current_get();

    memset(&wasm_state, 0, sizeof(wasm_state));
    run_on_peer(0U, wasm_owner_start);
    zassert_true(wasm_state.ready[0], "owner runtime failed: %s",
                 wasm_state.error);
    run_on_peer(1U, wasm_participant_start);
    zassert_equal_ptr(os_self_thread(), controller,
                      "controller identity changed while runtime is live");

    /* Interleave calls: each execution records its own caller identity. */
    run_on_peer(1U, wasm_call);
    run_on_peer(0U, wasm_call);
    run_on_peer(1U, wasm_participant_stop);
    run_on_peer(0U, wasm_call);
    run_on_peer(0U, wasm_owner_stop);

    zassert_true(wasm_state.ready[1], "participant setup failed: %s",
                 wasm_state.error);
    for (unsigned int i = 0U; i < RUNTIME_PEER_COUNT; i++) {
        assert_native_identity(wasm_state.identity[i], i);
        zassert_equal_ptr(wasm_state.identity_after[i], wasm_state.identity[i],
                          "peer %u identity changed across calls", i);
        zassert_true(wasm_state.called[i], "peer %u Wasm call failed", i);
        zassert_equal(wasm_state.result[i], 42U, "peer %u add returned %u", i,
                      wasm_state.result[i]);
        zassert_not_equal(wasm_state.identity[i], controller,
                          "peer %u shares the controller identity", i);
    }
    zassert_not_equal(wasm_state.identity[0], wasm_state.identity[1],
                      "two native threads share one identity");
    zassert_equal_ptr(wasm_state.owner_after_destroy, wasm_state.identity[0],
                      "runtime destroy changed the owner identity");
}

/* ---- C-API store bookkeeping ----------------------------------------- */

static bool
store_row_lookup(korp_tid tid, unsigned *stores_num, uint32_t *rows)
{
    Vector *vector = &c_api_state.engine->stores_by_tid;
    bool found = false;

    zassert_equal(vector->size_elem, sizeof(struct store_row),
                  "C-API store row layout changed");
    *rows = (uint32_t)vector->num_elems;
    *stores_num = 0U;
    for (uint32_t i = 0U; i < vector->num_elems; i++) {
        struct store_row row;

        zassert_true(bh_vector_get(vector, i, &row), "row %u unreadable", i);
        if (row.tid == tid) {
            zassert_false(found, "identity has two store rows");
            found = true;
            *stores_num = row.stores_num;
        }
    }
    return found;
}

static void
assert_store_rows(uint32_t expected_rows, unsigned expected_first,
                  unsigned expected_second)
{
    unsigned expected[RUNTIME_PEER_COUNT] = { expected_first, expected_second };
    uint32_t rows = 0U;

    for (unsigned int i = 0U; i < RUNTIME_PEER_COUNT; i++) {
        unsigned stores_num;
        bool found =
            store_row_lookup((korp_tid)peer_tids[i], &stores_num, &rows);

        if (expected[i] != 0U) {
            zassert_true(found, "peer %u has no store row", i);
        }
        zassert_equal(stores_num, expected[i], "peer %u owns %u stores", i,
                      stores_num);
    }
    zassert_equal(rows, expected_rows, "engine has %u store rows", rows);
}

static void
c_api_engine_new(unsigned int peer)
{
    wasm_config_t config = { 0 };
    MemAllocOption option = { 0 };

    ARG_UNUSED(peer);
    option.pool.heap_buf = peer_pool;
    option.pool.heap_size = sizeof(peer_pool);
    wasm_config_set_mem_alloc_opt(&config, Alloc_With_Pool, &option);
    c_api_state.engine = wasm_engine_new_with_config(&config);
}

static bool
c_api_call_add(struct c_api_peer *state)
{
    wasm_val_t args_val[2] = { WASM_I32_VAL(20), WASM_I32_VAL(22) };
    wasm_val_t results_val[1] = { WASM_INIT_VAL };
    wasm_val_vec_t args = WASM_ARRAY_VEC(args_val);
    wasm_val_vec_t results = WASM_ARRAY_VEC(results_val);
    wasm_func_t *add;
    wasm_trap_t *trap;

    if (state->exports.size != 1U
        || (add = wasm_extern_as_func(state->exports.data[0])) == NULL) {
        return false;
    }
    trap = wasm_func_call(add, &args, &results);
    if (trap != NULL) {
        wasm_trap_delete(trap);
        return false;
    }
    return results_val[0].kind == WASM_I32 && results_val[0].of.i32 == 42;
}

static void
c_api_store_new(unsigned int peer)
{
    struct c_api_peer *state = &c_api_state.peer[peer];
    wasm_extern_vec_t imports = WASM_EMPTY_VEC;
    wasm_byte_vec_t binary;

    memset(state, 0, sizeof(*state));
    state->identity = os_self_thread();
    state->stage = 1;
    state->store = wasm_store_new(c_api_state.engine);
    if (state->store == NULL) {
        return;
    }
    if (!C_API_WITH_MODULE) {
        state->ready = true;
        return;
    }
    memcpy(peer_wasm, wasm_add, sizeof(wasm_add));
    wasm_byte_vec_new(&binary, sizeof(wasm_add),
                      (const wasm_byte_t *)peer_wasm);
    state->module = wasm_module_new(state->store, &binary);
    wasm_byte_vec_delete(&binary);
    state->stage = 2;
    if (state->module == NULL) {
        return;
    }
    state->instance =
        wasm_instance_new_with_args(state->store, state->module, &imports, NULL,
                                    WAMR_TEST_WASM_STACK_SIZE, 0U);
    state->stage = 3;
    if (state->instance == NULL) {
        return;
    }
    state->stage = 4;
    wasm_instance_exports(state->instance, &state->exports);
    state->ready = c_api_call_add(state);
    state->calls = state->ready ? 1U : 0U;
}

static void
c_api_call(unsigned int peer)
{
    struct c_api_peer *state = &c_api_state.peer[peer];

    if (os_self_thread() == state->identity
        && (!C_API_WITH_MODULE || c_api_call_add(state))) {
        state->calls++;
    }
}

static void
c_api_extra_store_new(unsigned int peer)
{
    c_api_state.peer[peer].extra_store = wasm_store_new(c_api_state.engine);
}

static void
c_api_extra_store_delete(unsigned int peer)
{
    wasm_store_delete(c_api_state.peer[peer].extra_store);
    c_api_state.peer[peer].extra_store = NULL;
}

static void
c_api_store_delete(unsigned int peer)
{
    struct c_api_peer *state = &c_api_state.peer[peer];

    /* wasm_instance_delete() does not accept NULL. */
    wasm_extern_vec_delete(&state->exports);
    if (state->instance != NULL) {
        wasm_instance_delete(state->instance);
    }
    if (state->module != NULL) {
        wasm_module_delete(state->module);
    }
    wasm_store_delete(state->store);
    memset(&state->exports, 0, sizeof(state->exports));
    state->instance = NULL;
    state->module = NULL;
    state->store = NULL;
}

static void
c_api_store_recreate(unsigned int peer)
{
    wasm_store_t *store = wasm_store_new(c_api_state.engine);

    c_api_state.recreated = store != NULL;
    wasm_store_delete(store);
    ARG_UNUSED(peer);
}

static void
c_api_engine_delete(unsigned int peer)
{
    ARG_UNUSED(peer);
    wasm_engine_delete(c_api_state.engine);
    c_api_state.engine = NULL;
}

void
runtime_peers_check_c_api_stores(void)
{
    memset(&c_api_state, 0, sizeof(c_api_state));

    /* Peer 0 creates the engine; the creator owns no store row by itself. */
    run_on_peer(0U, c_api_engine_new);
    zassert_not_null(c_api_state.engine, "C-API engine creation failed");
    assert_store_rows(0U, 0U, 0U);

    run_on_peer(0U, c_api_store_new);
    run_on_peer(1U, c_api_store_new);
    zassert_true(c_api_state.peer[0].ready,
                 "peer 0 store setup failed after stage %d",
                 c_api_state.peer[0].stage);
    zassert_true(c_api_state.peer[1].ready,
                 "peer 1 store setup failed after stage %d",
                 c_api_state.peer[1].stage);
    for (unsigned int i = 0U; i < RUNTIME_PEER_COUNT; i++) {
        assert_native_identity(c_api_state.peer[i].identity, i);
    }
    assert_store_rows(2U, 1U, 1U);

    /* A second store on the same thread counts in that thread's row only. */
    run_on_peer(0U, c_api_extra_store_new);
    zassert_not_null(c_api_state.peer[0].extra_store,
                     "second peer 0 store failed");
    assert_store_rows(2U, 2U, 1U);

    /* Deleting peer 1's store must not disturb peer 0's live store. */
    run_on_peer(1U, c_api_store_delete);
    assert_store_rows(2U, 2U, 0U);
    run_on_peer(0U, c_api_call);
    zassert_equal(c_api_state.peer[0].calls, C_API_WITH_MODULE ? 2U : 1U,
                  "peer 0 store broke after peer 1 deleted its store");

    run_on_peer(0U, c_api_extra_store_delete);
    assert_store_rows(2U, 1U, 0U);

    /* Recreation reuses the thread's existing row rather than adding one. */
    run_on_peer(1U, c_api_store_recreate);
    zassert_true(c_api_state.recreated, "peer 1 store recreation failed");
    assert_store_rows(2U, 1U, 0U);

    run_on_peer(0U, c_api_store_delete);
    assert_store_rows(2U, 0U, 0U);
    run_on_peer(0U, c_api_engine_delete);
}

/* ---- Cleanup after a failed assertion ----------------------------------- */

static void
cleanup_peer_objects(unsigned int peer)
{
    struct wamr_test_runtime *runtime = &wasm_state.runtime[peer];

    if (c_api_state.engine != NULL) {
        c_api_extra_store_delete(peer);
        c_api_store_delete(peer);
    }
    if (peer != 0U
        && (runtime->exec_env != NULL || runtime->instance != NULL)) {
        wasm_participant_stop(peer);
    }
}

static void
cleanup_owner_runtime(unsigned int peer)
{
    if (c_api_state.engine != NULL) {
        c_api_engine_delete(peer);
    }
    wamr_test_runtime_stop(&wasm_state.runtime[peer]);
}

void
runtime_peers_cleanup(void)
{
    /* Each object is released by the thread that created it; owners last.
     * Successful scenarios already released everything, so this is a no-op.
     */
    run_on_peer(1U, cleanup_peer_objects);
    run_on_peer(0U, cleanup_peer_objects);
    run_on_peer(0U, cleanup_owner_runtime);
}

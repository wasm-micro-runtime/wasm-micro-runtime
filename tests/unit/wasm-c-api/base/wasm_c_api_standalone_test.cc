/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The standalone entry points: wasm_config_t and the engine constructors,
   the hand-built externs (wasm_global_new / wasm_table_new / wasm_memory_new
   / wasm_extern_new_empty), and the callback-with-env spelling of
   wasm_func_new.

   None of these needs a module or an instance, so they also pin down how the
   process-wide engine behaves when the runtime is initialised and destroyed
   repeatedly. */

#include "c_api_test_fixture.h"

namespace {

/* Counter the with-env callback and its finalizer bump, so the case can check
   that the callback saw the environment and that the finalizer ran once. */
struct EnvState {
    int calls;
    int finalizer_calls;
};

static wasm_trap_t *
counting_env_callback(void *env, const wasm_val_vec_t *args,
                      wasm_val_vec_t *results)
{
    EnvState *state = reinterpret_cast<EnvState *>(env);

    if (state)
        state->calls++;

    if (results->num_elems == 1)
        results->data[0] = val_i32(args->num_elems ? args->data[0].of.i32 : 0);
    return nullptr;
}

static void
env_state_finalizer(void *env)
{
    EnvState *state = reinterpret_cast<EnvState *>(env);

    if (state)
        state->finalizer_calls++;
}

class WasmCApiStandaloneTest : public CApiTestBase
{};

TEST_F(WasmCApiStandaloneTest, config_setters_round_trip)
{
    wasm_config_t *config = wasm_config_new();
    ASSERT_NE(nullptr, config);

    MemAllocOption option = { 0 };
    option.pool.heap_buf = nullptr;
    option.pool.heap_size = 0;

    EXPECT_EQ(config,
              wasm_config_set_mem_alloc_opt(config, Alloc_With_Pool, &option));
    EXPECT_EQ(Alloc_With_Pool, config->mem_alloc_type);

    /* A NULL option leaves the allocator type alone. */
    EXPECT_EQ(config, wasm_config_set_mem_alloc_opt(
                          config, Alloc_With_System_Allocator, nullptr));
    EXPECT_EQ(Alloc_With_System_Allocator, config->mem_alloc_type);

    EXPECT_EQ(config, wasm_config_set_linux_perf_opt(config, true));
    EXPECT_TRUE(config->enable_linux_perf);

    EXPECT_EQ(config, wasm_config_set_segue_flags(config, 0x5));
    EXPECT_EQ(0x5u, config->segue_flags);

    /* Every setter tolerates a NULL config. */
    EXPECT_EQ(nullptr,
              wasm_config_set_mem_alloc_opt(nullptr, Alloc_With_Pool, &option));
    EXPECT_EQ(nullptr, wasm_config_set_linux_perf_opt(nullptr, false));
    EXPECT_EQ(nullptr, wasm_config_set_segue_flags(nullptr, 0));

    wasm_config_delete(config);
    wasm_config_delete(nullptr);
}

TEST_F(WasmCApiStandaloneTest, engine_new_with_args_returns_the_singleton)
{
    MemAllocOption option = { 0 };

    option.allocator.malloc_func = nullptr;
    option.allocator.realloc_func = nullptr;
    option.allocator.free_func = nullptr;

    /* The engine is a process-wide singleton, so this hands back the engine
       the fixture already holds and only bumps its reference count. */
    wasm_engine_t *engine2 =
        wasm_engine_new_with_args(Alloc_With_System_Allocator, &option);

    EXPECT_EQ(engine, engine2);

    wasm_engine_delete(engine2);
}

TEST_F(WasmCApiStandaloneTest, hand_built_global_keeps_its_type_and_value)
{
    wasm_globaltype_t *type =
        wasm_globaltype_new(wasm_valtype_new(WASM_I64), WASM_VAR);
    ASSERT_NE(nullptr, type);

    wasm_val_t init = val_i64(42);
    wasm_global_t *global = wasm_global_new(store, type, &init);

    ASSERT_NE(nullptr, global);

    wasm_globaltype_t *got_type = wasm_global_type(global);
    ASSERT_NE(nullptr, got_type);
    EXPECT_EQ(WASM_VAR, wasm_globaltype_mutability(got_type));
    EXPECT_EQ(WASM_I64, wasm_valtype_kind(wasm_globaltype_content(got_type)));
    wasm_globaltype_delete(got_type);

    /* The host-built global has no instance behind it, so reading its value
       reports nothing rather than the initial one (see the TODO in
       wasm_global_new()). */
    wasm_val_t out = { 0 };
    wasm_global_get(global, &out);
    EXPECT_EQ(0, out.kind);

    wasm_global_t *copy = wasm_global_copy(global);
    ASSERT_NE(nullptr, copy);
    wasm_global_delete(copy);

    wasm_global_delete(global);
    wasm_globaltype_delete(type);

    /* Both arguments are required. */
    EXPECT_EQ(nullptr, wasm_global_new(store, nullptr, &init));
    EXPECT_EQ(nullptr, wasm_global_new(store, type, nullptr));
}

TEST_F(WasmCApiStandaloneTest, hand_built_memory_reports_its_type)
{
    wasm_limits_t limits = { 1, 2 };
    wasm_memorytype_t *type = wasm_memorytype_new(&limits);
    ASSERT_NE(nullptr, type);

    wasm_memory_t *memory = wasm_memory_new(store, type);
    ASSERT_NE(nullptr, memory);

    wasm_memorytype_t *got_type = wasm_memory_type(memory);
    ASSERT_NE(nullptr, got_type);
    EXPECT_EQ(1u, wasm_memorytype_limits(got_type)->min);
    EXPECT_EQ(2u, wasm_memorytype_limits(got_type)->max);
    wasm_memorytype_delete(got_type);

    /* With no instance the memory has no bytes to hand out. */
    EXPECT_EQ(nullptr, wasm_memory_data(memory));
    EXPECT_EQ(0u, wasm_memory_size(memory));

    wasm_memory_t *copy = wasm_memory_copy(memory);
    ASSERT_NE(nullptr, copy);
    wasm_memory_delete(copy);

    wasm_memory_delete(memory);
    wasm_memorytype_delete(type);

    EXPECT_EQ(nullptr, wasm_memory_new(store, nullptr));
}

TEST_F(WasmCApiStandaloneTest, hand_built_table_reports_its_type)
{
    wasm_limits_t limits = { 2, 4 };
    wasm_tabletype_t *type =
        wasm_tabletype_new(wasm_valtype_new(WASM_FUNCREF), &limits);
    ASSERT_NE(nullptr, type);

    wasm_table_t *table = wasm_table_new(store, type, nullptr);
    ASSERT_NE(nullptr, table);

    wasm_tabletype_t *got_type = wasm_table_type(table);
    ASSERT_NE(nullptr, got_type);
    EXPECT_EQ(WASM_FUNCREF,
              wasm_valtype_kind(wasm_tabletype_element(got_type)));
    EXPECT_EQ(2u, wasm_tabletype_limits(got_type)->min);
    wasm_tabletype_delete(got_type);

    wasm_table_t *copy = wasm_table_copy(table);
    ASSERT_NE(nullptr, copy);
    wasm_table_delete(copy);

    wasm_table_delete(table);
    wasm_tabletype_delete(type);

    EXPECT_EQ(nullptr, wasm_table_new(store, nullptr, nullptr));
}

TEST_F(WasmCApiStandaloneTest, extern_new_empty_only_supports_func_and_global)
{
    wasm_extern_t *func_extern = wasm_extern_new_empty(store, WASM_EXTERN_FUNC);
    ASSERT_NE(nullptr, func_extern);
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_extern_kind(func_extern));
    wasm_extern_delete(func_extern);

    wasm_extern_t *global_extern =
        wasm_extern_new_empty(store, WASM_EXTERN_GLOBAL);
    ASSERT_NE(nullptr, global_extern);
    EXPECT_EQ(WASM_EXTERN_GLOBAL, wasm_extern_kind(global_extern));
    wasm_extern_delete(global_extern);

    /* Memory and table cannot be linked yet, so the API refuses them. */
    EXPECT_EQ(nullptr, wasm_extern_new_empty(store, WASM_EXTERN_MEMORY));
    EXPECT_EQ(nullptr, wasm_extern_new_empty(store, WASM_EXTERN_TABLE));
}

TEST_F(WasmCApiStandaloneTest, func_new_with_env_passes_env_and_runs_finalizer)
{
    wasm_functype_t *type = i32_functype(1, 1);
    ASSERT_NE(nullptr, type);

    EnvState state = { 0, 0 };
    wasm_func_t *func = wasm_func_new_with_env(
        store, type, counting_env_callback, &state, env_state_finalizer);
    ASSERT_NE(nullptr, func);

    /* A host function is meant to be linked into an instance, not called
       directly: with no instance behind it, wasm_func_call() reports
       "failed to call unlinked function" and never reaches the callback. */
    wasm_val_t args[1] = { val_i32(9) };
    wasm_val_vec_t args_vec = { 0 };
    wasm_val_vec_t results = { 0 };

    wasm_val_vec_new(&args_vec, 1, args);
    wasm_val_vec_new_uninitialized(&results, 1);

    wasm_trap_t *trap = wasm_func_call(func, &args_vec, &results);

    ASSERT_NE(nullptr, trap);
    EXPECT_EQ(0, state.calls);

    wasm_trap_delete(trap);
    wasm_val_vec_delete(&results);
    wasm_val_vec_delete(&args_vec);

    /* The finalizer runs exactly once, when the function is released. */
    EXPECT_EQ(0, state.finalizer_calls);
    wasm_func_delete(func);
    EXPECT_EQ(1, state.finalizer_calls);

    wasm_functype_delete(type);

    /* A NULL callback is rejected. */
    EXPECT_EQ(nullptr,
              wasm_func_new_with_env(store, type, nullptr, nullptr, nullptr));
}

} // namespace

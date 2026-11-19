/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "wasm_exec_env.h"
#include "wasm_runtime_common.h"

class wasm_exec_env_test_suite : public testing::Test
{
  protected:
    // You should make the members protected s.t. they can be
    // accessed from sub-classes.

    // virtual void SetUp() will be called before each test is run.  You
    // should define it if you need to initialize the variables.
    // Otherwise, this can be skipped.
    virtual void SetUp() {}

    // virtual void TearDown() will be called after each test is run.
    // You should define it if there is cleanup work to do.  Otherwise,
    // you don't have to provide it.
    //
    virtual void TearDown() {}
};

TEST_F(wasm_exec_env_test_suite, wasm_exec_env_create)
{
    EXPECT_EQ(nullptr, wasm_exec_env_create(nullptr, 0));
}

TEST_F(wasm_exec_env_test_suite, wasm_exec_env_create_internal)
{
    EXPECT_EQ(nullptr, wasm_exec_env_create_internal(nullptr, UINT32_MAX));
}

TEST_F(wasm_exec_env_test_suite, wasm_exec_env_pop_jmpbuf)
{
    WASMExecEnv exec_env;

    exec_env.jmpbuf_stack_top = nullptr;
    EXPECT_EQ(nullptr, wasm_exec_env_pop_jmpbuf(&exec_env));
}

/*
 * Test: exec_env_tls is cleared on early return from native stack overflow
 *
 * This test verifies that when wasm_runtime_call_wasm fails early due to
 * native stack overflow check, exec_env_tls is properly cleared. Without
 * this fix, subsequent calls with a different exec_env would fail with
 * "invalid exec env" error.
 *
 * Bug scenario:
 *   1. Call WASM with exec_env_A, TLS set to exec_env_A
 *   2. Native stack overflow check fails, early return
 *   3. TLS still points to exec_env_A (BUG: not cleared)
 *   4. Destroy exec_env_A
 *   5. Create exec_env_B, call WASM
 *   6. Fails with "invalid exec env" because TLS != exec_env_B
 */
#ifdef OS_ENABLE_HW_BOUND_CHECK
/* Minimal WASM module: (module (func (export "test") (result i32) i32.const
 * 42)) */
static uint8_t test_wasm[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
                               0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03,
                               0x02, 0x01, 0x00, 0x07, 0x08, 0x01, 0x04, 0x74,
                               0x65, 0x73, 0x74, 0x00, 0x00, 0x0a, 0x06, 0x01,
                               0x04, 0x00, 0x41, 0x2a, 0x0b };

TEST_F(wasm_exec_env_test_suite, exec_env_tls_cleared_on_stack_overflow)
{
    WAMRRuntimeRAII<512 * 1024> runtime;

    /* Load and instantiate module */
    wasm_module_t module =
        wasm_runtime_load(test_wasm, sizeof(test_wasm), nullptr, 0);
    ASSERT_NE(module, nullptr);

    wasm_module_inst_t inst_a =
        wasm_runtime_instantiate(module, 8192, 8192, nullptr, 0);
    ASSERT_NE(inst_a, nullptr);

    wasm_exec_env_t exec_env_a =
        wasm_runtime_create_exec_env(inst_a, 8192);
    ASSERT_NE(exec_env_a, nullptr);

    /* Set native stack boundary high to trigger overflow check failure */
    uint8_t stack_var;
    uint8_t *high_boundary = &stack_var + 0x100000;
    wasm_runtime_set_native_stack_boundary(exec_env_a, high_boundary);

    /* Call should fail with native stack overflow */
    wasm_function_inst_t func =
        wasm_runtime_lookup_function(inst_a, "test");
    ASSERT_NE(func, nullptr);

    uint32_t argv[1] = { 0 };
    bool result = wasm_runtime_call_wasm(exec_env_a, func, 0, argv);
    EXPECT_FALSE(result);

    /* Verify TLS is cleared after failed call (this is the fix) */
    WASMExecEnv *tls = wasm_runtime_get_exec_env_tls();
    EXPECT_EQ(tls, nullptr);

    /* Clean up first instance */
    wasm_runtime_destroy_exec_env(exec_env_a);
    wasm_runtime_deinstantiate(inst_a);

    /* Create second instance and exec_env */
    wasm_module_inst_t inst_b =
        wasm_runtime_instantiate(module, 8192, 8192, nullptr, 0);
    ASSERT_NE(inst_b, nullptr);

    wasm_exec_env_t exec_env_b =
        wasm_runtime_create_exec_env(inst_b, 8192);
    ASSERT_NE(exec_env_b, nullptr);

    /* This call should succeed (would fail without the fix) */
    func = wasm_runtime_lookup_function(inst_b, "test");
    ASSERT_NE(func, nullptr);

    argv[0] = 0;
    result = wasm_runtime_call_wasm(exec_env_b, func, 0, argv);
    EXPECT_TRUE(result);
    EXPECT_EQ(argv[0], 42u);

    /* Clean up */
    wasm_runtime_destroy_exec_env(exec_env_b);
    wasm_runtime_deinstantiate(inst_b);
    wasm_runtime_unload(module);
}
#endif /* OS_ENABLE_HW_BOUND_CHECK */

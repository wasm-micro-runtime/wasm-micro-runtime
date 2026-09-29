/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <limits.h>
#include <vector>

#include "gtest/gtest.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

// To use a test fixture, derive a class from testing::Test.
class InterpreterTest : public testing::Test
{
  protected:
    // You should make the members protected s.t. they can be
    // accessed from sub-classes.

    // virtual void SetUp() will be called before each test is run.  You
    // should define it if you need to initialize the variables.
    // Otherwise, this can be skipped.
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));

        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);
    }

    // virtual void TearDown() will be called after each test is run.
    // You should define it if there is cleanup work to do.  Otherwise,
    // you don't have to provide it.
    //
    virtual void TearDown() { wasm_runtime_destroy(); }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
};

TEST_F(InterpreterTest, wasm_runtime_is_built_in_module)
{
    bool ret = wasm_runtime_is_built_in_module("env");
    ASSERT_TRUE(ret);

    ret = wasm_runtime_is_built_in_module("env1");
    ASSERT_FALSE(ret);
}

TEST_F(InterpreterTest, ExecutesControlFlowWithOddCellStackLayout)
{
    static const unsigned char module_bytes[] = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x04,
        0x01, 0x60, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07,
        0x01, 0x03, 0x72, 0x75, 0x6E, 0x00, 0x00, 0x0A, 0x09, 0x01,
        0x07, 0x01, 0x01, 0x7F, 0x02, 0x40, 0x0B, 0x0B,
    };
    std::vector<unsigned char> mutable_module(
        module_bytes, module_bytes + sizeof(module_bytes));
    char error_buf[128] = { 0 };
    wasm_module_t module = wasm_runtime_load(
        mutable_module.data(), static_cast<uint32_t>(mutable_module.size()),
        error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;

    wasm_module_inst_t instance = wasm_runtime_instantiate(
        module, 8 * 1024, 8 * 1024, error_buf, sizeof(error_buf));
    ASSERT_NE(instance, nullptr) << error_buf;
    EXPECT_TRUE(wasm_application_execute_func(instance, "run", 0, nullptr));

    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
}

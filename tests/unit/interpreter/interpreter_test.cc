/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <limits.h>
#include <cstring>
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

TEST_F(InterpreterTest, LoadsWasmFromUnalignedInputBuffer)
{
    static const unsigned char empty_wasm[] = { 0x00, 0x61, 0x73, 0x6D,
                                                0x01, 0x00, 0x00, 0x00 };
    std::vector<unsigned char> storage(sizeof(empty_wasm) + 1);
    std::memcpy(storage.data() + 1, empty_wasm, sizeof(empty_wasm));

    char error_buf[128] = { 0 };
    wasm_module_t module = wasm_runtime_load(
        storage.data() + 1, sizeof(empty_wasm), error_buf, sizeof(error_buf));
    ASSERT_NE(module, nullptr) << error_buf;
    wasm_runtime_unload(module);
}

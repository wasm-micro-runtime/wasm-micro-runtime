/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"
#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

class WasmGCTest : public testing::Test
{
  protected:
    void SetUp()
    {
        CWD = get_test_binary_dir();

        memset(&init_args, 0, sizeof(RuntimeInitArgs));

        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);

        cleanup = true;
    }

    void TearDown()
    {
        if (cleanup) {
            wasm_runtime_destroy();
        }
    }

  public:
    bool load_wasm_file(const char *wasm_file)
    {
        char *file;
        unsigned char *wasm_file_buf;
        uint32 wasm_file_size;

        file = strdup((CWD + "/" + wasm_file).c_str());

        wasm_file_buf =
            (unsigned char *)bh_read_file_to_buffer(file, &wasm_file_size);
        free(file);

        if (!wasm_file_buf)
            return false;

        module = wasm_runtime_load(wasm_file_buf, wasm_file_size, error_buf,
                                   sizeof(error_buf));
        if (!module)
            return false;

        return true;
    }

  public:
    std::string CWD;
    RuntimeInitArgs init_args;
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    wasm_function_inst_t func_inst = NULL;
    wasm_exec_env_t exec_env = NULL;
    char error_buf[128];
    char global_heap_buf[512 * 1024];
    bool cleanup = true;
};

TEST_F(WasmGCTest, Test_app1)
{
    ASSERT_TRUE(load_wasm_file("test1.wasm"));
    ASSERT_TRUE(load_wasm_file("test2.wasm"));
    ASSERT_TRUE(load_wasm_file("test3.wasm"));
    ASSERT_TRUE(load_wasm_file("test4.wasm"));
    ASSERT_TRUE(load_wasm_file("test5.wasm"));
    ASSERT_TRUE(load_wasm_file("test6.wasm"));

    ASSERT_TRUE(load_wasm_file("struct1.wasm"));
    ASSERT_TRUE(load_wasm_file("struct2.wasm"));
    ASSERT_TRUE(load_wasm_file("struct3.wasm"));

    ASSERT_TRUE(load_wasm_file("func1.wasm"));
    ASSERT_TRUE(load_wasm_file("func2.wasm"));
}

TEST_F(WasmGCTest, Test_nested_struct)
{
    // FIXME: Revert the change when anyref support is added
    ASSERT_FALSE(load_wasm_file("nested_struct_field_any.wasm"));
    ASSERT_FALSE(load_wasm_file("nested_array_elem_any.wasm"));
}

TEST_F(WasmGCTest, Test_array_new_init_expr)
{
    ASSERT_TRUE(load_wasm_file("array_new_valid.wasm"));
    /* Ensure loading array.new with negative length does not trigger heap
     * corruption or crash */
    load_wasm_file("array_new_negative_len.wasm");
}
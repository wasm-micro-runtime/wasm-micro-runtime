/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"
#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"

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
    //FIXME: Revert the change when anyref support is added
    ASSERT_FALSE(load_wasm_file("nested_struct_field_any.wasm"));
    ASSERT_FALSE(load_wasm_file("nested_array_elem_any.wasm"));
}

TEST_F(WasmGCTest, Test_get_table_func_inst_funcref_table)
{
    ASSERT_TRUE(load_wasm_file("table_funcref.wasm"));
    module_inst = wasm_runtime_instantiate(module, 8192, 8192, error_buf,
                                           sizeof(error_buf));
    ASSERT_NE(module_inst, nullptr) << error_buf;

    EXPECT_NE(wasm_runtime_get_table_func_inst(module_inst, 1), nullptr);
    /* null element and out of range */
    EXPECT_EQ(wasm_runtime_get_table_func_inst(module_inst, 0), nullptr);
    EXPECT_EQ(wasm_runtime_get_table_func_inst(module_inst, 2), nullptr);

    wasm_runtime_deinstantiate(module_inst);
}

TEST_F(WasmGCTest, Test_get_table_func_inst_rejects_non_func_ref)
{
    ASSERT_TRUE(load_wasm_file("table_anyref_i31.wasm"));
    module_inst = wasm_runtime_instantiate(module, 8192, 8192, error_buf,
                                           sizeof(error_buf));
    ASSERT_NE(module_inst, nullptr) << error_buf;

    exec_env = wasm_runtime_create_exec_env(module_inst, 8192);
    ASSERT_NE(exec_env, nullptr);
    func_inst = wasm_runtime_lookup_function(module_inst, "init");
    ASSERT_NE(func_inst, nullptr);
    ASSERT_TRUE(wasm_runtime_call_wasm(exec_env, func_inst, 0, NULL));

    /* element 1 is a non-null i31ref, it must not be read as a function
       object */
    EXPECT_EQ(wasm_runtime_get_table_func_inst(module_inst, 1), nullptr);
    EXPECT_EQ(wasm_runtime_get_table_func_inst(module_inst, 0), nullptr);

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(module_inst);
}

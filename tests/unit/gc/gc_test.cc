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
    //FIXME: Revert the change when anyref support is added
    ASSERT_FALSE(load_wasm_file("nested_struct_field_any.wasm"));
    ASSERT_FALSE(load_wasm_file("nested_array_elem_any.wasm"));
}

/*
 * Regression test for issue #5194: array.new_data must accept a zero-length
 * access at the very end of a data segment, and reject anything past it.
 *
 * (module
 *   (type $a (array (mut i32)))
 *   (data $empty "")
 *   (data $four "a\00\00\00")
 *   (func (export "empty") (result i32)
 *     (array.len (array.new_data $a $empty (i32.const 0) (i32.const 0))))
 *   (func (export "nonempty") (result i32)
 *     (array.get $a (array.new_data $a $four (i32.const 0) (i32.const 1))
 *       (i32.const 0)))
 *   (func (export "at_end") (result i32)
 *     (array.len (array.new_data $a $four (i32.const 4) (i32.const 0))))
 *   (func (export "past_end") (result i32)
 *     (array.len (array.new_data $a $four (i32.const 5) (i32.const 0))))
 *   (func (export "too_long") (result i32)
 *     (array.len (array.new_data $a $four (i32.const 4) (i32.const 1)))))
 */
TEST_F(WasmGCTest, Test_array_new_data_bounds)
{
    static uint8 array_new_data_wasm[] = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x5e,
        0x7f, 0x01, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x06, 0x05, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x07, 0x33, 0x05, 0x05, 0x65, 0x6d, 0x70, 0x74, 0x79, 0x00,
        0x00, 0x08, 0x6e, 0x6f, 0x6e, 0x65, 0x6d, 0x70, 0x74, 0x79, 0x00, 0x01,
        0x06, 0x61, 0x74, 0x5f, 0x65, 0x6e, 0x64, 0x00, 0x02, 0x08, 0x70, 0x61,
        0x73, 0x74, 0x5f, 0x65, 0x6e, 0x64, 0x00, 0x03, 0x08, 0x74, 0x6f, 0x6f,
        0x5f, 0x6c, 0x6f, 0x6e, 0x67, 0x00, 0x04, 0x0c, 0x01, 0x02, 0x0a, 0x45,
        0x05, 0x0c, 0x00, 0x41, 0x00, 0x41, 0x00, 0xfb, 0x09, 0x00, 0x00, 0xfb,
        0x0f, 0x0b, 0x0f, 0x00, 0x41, 0x00, 0x41, 0x01, 0xfb, 0x09, 0x00, 0x01,
        0x41, 0x00, 0xfb, 0x0b, 0x00, 0x0b, 0x0c, 0x00, 0x41, 0x04, 0x41, 0x00,
        0xfb, 0x09, 0x00, 0x01, 0xfb, 0x0f, 0x0b, 0x0c, 0x00, 0x41, 0x05, 0x41,
        0x00, 0xfb, 0x09, 0x00, 0x01, 0xfb, 0x0f, 0x0b, 0x0c, 0x00, 0x41, 0x04,
        0x41, 0x01, 0xfb, 0x09, 0x00, 0x01, 0xfb, 0x0f, 0x0b, 0x0b, 0x09, 0x02,
        0x01, 0x00, 0x01, 0x04, 0x61, 0x00, 0x00, 0x00
    };

    /* name, expected result, whether the call must trap */
    struct CallCase {
        const char *name;
        uint32 expected;
        bool traps;
    } cases[] = {
        { "empty", 0, false },   { "nonempty", 97, false },
        { "at_end", 0, false },  { "past_end", 0, true },
        { "too_long", 0, true },
    };

    uint32 argv[1];

    module = wasm_runtime_load(array_new_data_wasm, sizeof(array_new_data_wasm),
                               error_buf, sizeof(error_buf));
    ASSERT_TRUE(module != NULL) << error_buf;

    module_inst = wasm_runtime_instantiate(module, 8 * 1024, 8 * 1024,
                                           error_buf, sizeof(error_buf));
    ASSERT_TRUE(module_inst != NULL);

    exec_env = wasm_runtime_create_exec_env(module_inst, 8 * 1024);
    ASSERT_TRUE(exec_env != NULL);

    for (const CallCase &c : cases) {
        const char *exception;

        argv[0] = 0xdeadbeef;
        func_inst = wasm_runtime_lookup_function(module_inst, c.name);
        ASSERT_TRUE(func_inst != NULL) << c.name;

        ASSERT_EQ(!c.traps,
                  wasm_runtime_call_wasm(exec_env, func_inst, 0, argv))
            << c.name;

        if (c.traps) {
            exception = wasm_runtime_get_exception(module_inst);
            ASSERT_TRUE(exception != NULL) << c.name;
            ASSERT_TRUE(strstr(exception, "data segment out of bounds") != NULL)
                << c.name;
            wasm_runtime_clear_exception(module_inst);
        }
        else {
            ASSERT_EQ(c.expected, argv[0]) << c.name;
            ASSERT_TRUE(wasm_runtime_get_exception(module_inst) == NULL)
                << c.name;
        }
    }

    wasm_runtime_destroy_exec_env(exec_env);
    exec_env = NULL;

    wasm_runtime_deinstantiate(module_inst);
    module_inst = NULL;

    wasm_runtime_unload(module);
    module = NULL;
}

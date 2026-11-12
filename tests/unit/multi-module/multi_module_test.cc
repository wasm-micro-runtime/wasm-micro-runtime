/*
 * Copyright (C) 2026 WAMR Community.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_export.h"
#include "bh_platform.h"
#include "bh_read_file.h"

#include <string>

class MultiModuleTest : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
        ASSERT_TRUE(wasm_runtime_full_init(&init_args));

        /* The registered module is unloaded when the runtime is
           destroyed. */
        wasm_module_t callee = load("callee.wasm");
        ASSERT_NE(callee, nullptr) << error_buf;
        ASSERT_TRUE(wasm_runtime_register_module("callee", callee, error_buf,
                                                 sizeof(error_buf)))
            << error_buf;

        module = load("caller.wasm");
        ASSERT_NE(module, nullptr) << error_buf;
        module_inst = wasm_runtime_instantiate(module, 8192, 0, error_buf,
                                               sizeof(error_buf));
        ASSERT_NE(module_inst, nullptr) << error_buf;
        exec_env = wasm_runtime_create_exec_env(module_inst, 8192);
        ASSERT_NE(exec_env, nullptr);
    }

    virtual void TearDown()
    {
        if (exec_env)
            wasm_runtime_destroy_exec_env(exec_env);
        if (module_inst)
            wasm_runtime_deinstantiate(module_inst);
        if (module)
            wasm_runtime_unload(module);
        /* It also releases the buffers of the modules, which are
           allocated from the heap of the runtime. */
        wasm_runtime_destroy();
    }

    wasm_module_t load(const char *file)
    {
        uint32_t size = 0;
        uint8_t *buf = (uint8_t *)bh_read_file_to_buffer(file, &size);

        if (!buf) {
            snprintf(error_buf, sizeof(error_buf), "can't read %s", file);
            return nullptr;
        }
        return wasm_runtime_load(buf, size, error_buf, sizeof(error_buf));
    }

    /*
     * Call a function of the caller module which ends with a trap in the
     * callee module, and return the call stack which is then created.
     */
    std::string call_stack_of_trap(const char *name)
    {
        wasm_function_inst_t func =
            wasm_runtime_lookup_function(module_inst, name);
        uint32_t argv[1] = { 1 }, size;
        std::string call_stack;

        EXPECT_NE(func, nullptr) << name;
        EXPECT_FALSE(wasm_runtime_call_wasm(exec_env, func, 1, argv)) << name;
        EXPECT_NE(
            strstr(wasm_runtime_get_exception(module_inst), "unreachable"),
            nullptr)
            << name;

        size = wasm_runtime_get_call_stack_buf_size(exec_env);
        EXPECT_GT(size, 0u) << name;
        call_stack.resize(size);
        wasm_runtime_dump_call_stack_to_buf(exec_env, &call_stack[0], size);
        return call_stack;
    }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
    wasm_module_t module = nullptr;
    wasm_module_inst_t module_inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
};

/* The frames of the functions of the callee module are below the one of
   the function of the caller module which called them. */
TEST_F(MultiModuleTest, call_stack_of_trap_in_imported_function)
{
    std::string call_stack = call_stack_of_trap("call_trap");
    size_t trap = call_stack.find("- trap"),
           call_trap = call_stack.find("- call_trap");

    EXPECT_NE(trap, std::string::npos) << call_stack;
    EXPECT_NE(call_trap, std::string::npos) << call_stack;
    EXPECT_LT(trap, call_trap) << call_stack;
    /* #00 is the function which isn't exported and has no name */
    EXPECT_NE(call_stack.find("#02"), std::string::npos) << call_stack;
    EXPECT_EQ(call_stack.find("#03"), std::string::npos) << call_stack;
}

/* The function of the callee module is called by the host through the
   export of the caller module: there are only frames of the callee. */
TEST_F(MultiModuleTest, call_stack_of_trap_in_reexported_function)
{
    std::string call_stack = call_stack_of_trap("reexported_trap");

    EXPECT_NE(call_stack.find("- trap"), std::string::npos) << call_stack;
    EXPECT_NE(call_stack.find("#01"), std::string::npos) << call_stack;
    EXPECT_EQ(call_stack.find("#02"), std::string::npos) << call_stack;
}

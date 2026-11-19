/*
 * Copyright (C) 2026 WAMR Community.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_export.h"
#include "bh_platform.h"
#include "bh_read_file.h"

#include <string>

/* An instance of the callee module which no module imports from, and the
   call stack of the trap which the native function below gets in it. */
static wasm_module_inst_t unrelated_inst;
static std::string unrelated_call_stack;

static std::string
dump_call_stack(wasm_exec_env_t exec_env)
{
    uint32_t size = wasm_runtime_get_call_stack_buf_size(exec_env);
    std::string call_stack;

    call_stack.resize(size);
    wasm_runtime_dump_call_stack_to_buf(exec_env, &call_stack[0], size);
    return call_stack;
}

/* Call the function "trap" of the unrelated instance with the exec env of
   the calling instance, so that their frames are in the same wasm stack. */
static int32_t
call_trap_wrapper(wasm_exec_env_t exec_env, int32_t arg)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    wasm_function_inst_t func =
        wasm_runtime_lookup_function(unrelated_inst, "trap");
    uint32_t argv[1] = { (uint32_t)arg };

    wasm_runtime_set_module_inst(exec_env, unrelated_inst);
    if (!wasm_runtime_call_wasm(exec_env, func, 1, argv))
        unrelated_call_stack = dump_call_stack(exec_env);
    wasm_runtime_set_module_inst(exec_env, module_inst);

    wasm_runtime_set_exception(module_inst, "trap in the unrelated instance");
    return 0;
}

static NativeSymbol native_symbols[] = {
    { "call_trap", (void *)call_trap_wrapper, "(i)i", NULL },
};

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
        ASSERT_TRUE(wasm_runtime_register_natives(
            "env", native_symbols,
            sizeof(native_symbols) / sizeof(native_symbols[0])));

        /* The registered module is unloaded when the runtime is
           destroyed. */
        callee = load("callee.wasm");
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
        uint32_t argv[1] = { 1 };

        EXPECT_NE(func, nullptr) << name;
        EXPECT_FALSE(wasm_runtime_call_wasm(exec_env, func, 1, argv)) << name;
        EXPECT_NE(
            strstr(wasm_runtime_get_exception(module_inst), "unreachable"),
            nullptr)
            << name;

        return dump_call_stack(exec_env);
    }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
    wasm_module_t callee = nullptr;
    wasm_module_t module = nullptr;
    wasm_module_inst_t module_inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
};

/* The frames of the functions of the callee module are below the one of
   the function of the caller module which called them. */
TEST_F(MultiModuleTest, call_stack_of_trap_in_imported_function)
{
    std::string call_stack = call_stack_of_trap("call_trap");
    size_t trap = call_stack.find("- trap (callee)\n"),
           call_trap = call_stack.find("- call_trap\n");

    /* The frames of the callee are printed with the name of their module,
       so that they can't be taken for functions of the caller. */
    EXPECT_NE(trap, std::string::npos) << call_stack;
    EXPECT_NE(call_trap, std::string::npos) << call_stack;
    EXPECT_LT(trap, call_trap) << call_stack;
    /* #00 is the function which isn't exported and has no name */
    EXPECT_NE(call_stack.find("- $f0 (callee)\n"), std::string::npos)
        << call_stack;
    EXPECT_NE(call_stack.find("#02"), std::string::npos) << call_stack;
    EXPECT_EQ(call_stack.find("#03"), std::string::npos) << call_stack;
}

/* The function of the callee module is called by the host through the
   export of the caller module: there are only frames of the callee. */
TEST_F(MultiModuleTest, call_stack_of_trap_in_reexported_function)
{
    std::string call_stack = call_stack_of_trap("reexported_trap");

    EXPECT_NE(call_stack.find("- trap (callee)\n"), std::string::npos)
        << call_stack;
    EXPECT_NE(call_stack.find("#01"), std::string::npos) << call_stack;
    EXPECT_EQ(call_stack.find("#02"), std::string::npos) << call_stack;
}

/* A native function calls a function of an instance which the calling
   module doesn't import from: the functions of the calling instance aren't
   found from the one which traps, their frames are kept as unknown. */
TEST_F(MultiModuleTest, call_stack_of_trap_in_unrelated_instance)
{
    wasm_module_t native_caller = load("native_caller.wasm");
    wasm_module_inst_t native_caller_inst;
    wasm_exec_env_t native_caller_exec_env;
    wasm_function_inst_t func;
    uint32_t argv[1] = { 1 };

    ASSERT_NE(native_caller, nullptr) << error_buf;
    native_caller_inst = wasm_runtime_instantiate(native_caller, 8192, 0,
                                                  error_buf, sizeof(error_buf));
    ASSERT_NE(native_caller_inst, nullptr) << error_buf;
    unrelated_inst =
        wasm_runtime_instantiate(callee, 8192, 0, error_buf, sizeof(error_buf));
    ASSERT_NE(unrelated_inst, nullptr) << error_buf;
    native_caller_exec_env =
        wasm_runtime_create_exec_env(native_caller_inst, 8192);
    ASSERT_NE(native_caller_exec_env, nullptr);

    unrelated_call_stack.clear();
    func = wasm_runtime_lookup_function(native_caller_inst, "call_native");
    ASSERT_NE(func, nullptr);
    EXPECT_FALSE(wasm_runtime_call_wasm(native_caller_exec_env, func, 1, argv));

    /* The functions of the instance whose call stack is dumped don't have
       a module name. */
    size_t trap = unrelated_call_stack.find("- trap\n"),
           unknown = unrelated_call_stack.find("- <unknown function>\n");
    EXPECT_NE(trap, std::string::npos) << unrelated_call_stack;
    EXPECT_NE(unknown, std::string::npos) << unrelated_call_stack;
    EXPECT_LT(trap, unknown) << unrelated_call_stack;
    EXPECT_EQ(unrelated_call_stack.find("call_native"), std::string::npos)
        << unrelated_call_stack;

    wasm_runtime_destroy_exec_env(native_caller_exec_env);
    wasm_runtime_deinstantiate(unrelated_inst);
    unrelated_inst = nullptr;
    wasm_runtime_deinstantiate(native_caller_inst);
    wasm_runtime_unload(native_caller);
}

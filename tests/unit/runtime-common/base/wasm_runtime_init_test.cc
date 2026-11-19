/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "platform_common.h"
#include "wasm_runtime_common.h"
#include "bh_read_file.h"
#include "wasm_runtime.h"
#include "bh_platform.h"
#include "wasm_export.h"

using namespace std;

extern "C" {
uint32
wasm_runtime_module_realloc(WASMModuleInstanceCommon *module_inst, uint32 ptr,
                            uint32 size, void **p_native_addr);
bool
wasm_runtime_create_exec_env_and_call_wasm(
    WASMModuleInstanceCommon *module_inst, WASMFunctionInstanceCommon *function,
    uint32 argc, uint32 argv[]);
}

static std::string CWD;
static std::string MAIN_WASM = "/main.wasm";
#if WASM_ENABLE_AOT != 0
static std::string MAIN_AOT = "/main.aot";
#endif
static char *WASM_FILE_1;
#if WASM_ENABLE_AOT != 0
static char *AOT_FILE_1;
#endif

static int
foo(int a, int b);

static int
foo_native(wasm_exec_env_t exec_env, int a, int b)
{
    return a + b;
}

static NativeSymbol native_symbols[] = { {
    "foo",              // the name of WASM function name
    (void *)foo_native, // the native function pointer
    "(ii)i"             // the function prototype signature
} };

class wasm_runtime_init_test_suite : public testing::Test
{
  protected:
    // You should make the members protected s.t. they can be
    // accessed from sub-classes.

    // virtual void SetUp() will be called before each test is run.  You
    // should define it if you need to initialize the variables.
    // Otherwise, this can be skipped.
    virtual void SetUp() {}

    static void SetUpTestCase()
    {
        CWD = get_test_binary_dir();
        WASM_FILE_1 = strdup((CWD + MAIN_WASM).c_str());
#if WASM_ENABLE_AOT != 0
        AOT_FILE_1 = strdup((CWD + MAIN_AOT).c_str());
#endif
    }

    // virtual void TearDown() will be called after each test is run.
    // You should define it if there is cleanup work to do.  Otherwise,
    // you don't have to provide it.
    //
    virtual void TearDown() {}

    static void TearDownTestCase()
    {
        free(WASM_FILE_1);
#if WASM_ENABLE_AOT != 0
        free(AOT_FILE_1);
#endif
    }
};

TEST_F(wasm_runtime_init_test_suite, init_and_register_natives)
{
    EXPECT_EQ(true, wasm_runtime_init());
    int n_native_symbols = sizeof(native_symbols) / sizeof(NativeSymbol);
    EXPECT_EQ(true, wasm_runtime_register_natives("env", native_symbols,
                                                  n_native_symbols));
    EXPECT_EQ(true, wasm_runtime_register_natives_raw("env", native_symbols,
                                                      n_native_symbols));
    wasm_runtime_destroy();
}

TEST_F(wasm_runtime_init_test_suite, init_thread_env_destroy_thread_env)
{
    EXPECT_EQ(true, wasm_runtime_init_thread_env());
    wasm_runtime_destroy_thread_env();
}

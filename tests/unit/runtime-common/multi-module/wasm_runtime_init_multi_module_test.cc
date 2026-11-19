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

static char global_heap_buf[100 * 1024 * 1024] = { 0 };

static std::string CWD;
static std::string MAIN_WASM = "/main.wasm";
#if WASM_ENABLE_AOT != 0
static std::string MAIN_AOT = "/main.aot";
#endif
static char *WASM_FILE_1;
#if WASM_ENABLE_AOT != 0
static char *AOT_FILE_1;
#endif

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

TEST_F(wasm_runtime_init_test_suite, wasm_runtime_full_init)
{
    RuntimeInitArgs init_args;
    unsigned char *wasm_file_buf;
    uint32 wasm_file_size;
    wasm_module_t module = nullptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
    EXPECT_EQ(true, wasm_runtime_full_init(&init_args));
    wasm_runtime_destroy();
    init_args.n_native_symbols = 1;
    EXPECT_EQ(true, wasm_runtime_full_init(&init_args));
    wasm_runtime_destroy();

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func = (void *)malloc;
    init_args.mem_alloc_option.allocator.realloc_func = (void *)realloc;
    init_args.mem_alloc_option.allocator.free_func = (void *)free;
    EXPECT_EQ(true, wasm_runtime_full_init(&init_args));
    wasm_runtime_destroy();

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Allocator;
    init_args.mem_alloc_option.allocator.malloc_func = (void *)os_malloc;
    init_args.mem_alloc_option.allocator.realloc_func = (void *)os_realloc;
    init_args.mem_alloc_option.allocator.free_func = (void *)os_free;
    EXPECT_EQ(true, wasm_runtime_full_init(&init_args));
    /* Use valid module, and runtime need to be proper inited */
    wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(WASM_FILE_1, &wasm_file_size);
    EXPECT_NE(nullptr, wasm_file_buf);
    module = wasm_runtime_load(wasm_file_buf, wasm_file_size, nullptr, 0);
    EXPECT_NE(nullptr, module);
    EXPECT_EQ(true,
              wasm_runtime_register_module_internal(
                  "module", module, wasm_file_buf, wasm_file_size, nullptr, 0));
    wasm_runtime_destroy();

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = NULL;
    init_args.mem_alloc_option.pool.heap_size = 0;
    EXPECT_EQ(false, wasm_runtime_full_init(&init_args));
}

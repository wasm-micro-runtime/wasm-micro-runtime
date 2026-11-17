/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef THREAD_TEST_COMMON_H
#define THREAD_TEST_COMMON_H

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

#if THREAD_TEST_AOT != 0
#define FIXTURE_EXT ".aot"
#else
#define FIXTURE_EXT ".wasm"
#endif

/* Loads one fixture from THREAD_TEST_FIXTURE_DIR and calls its exports.
   Each test case instantiates its own module */
class ThreadModuleFixture : public testing::Test
{
  protected:
    void SetUp() override
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(heap_buf);
        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);
    }

    void TearDown() override
    {
        if (exec_env)
            wasm_runtime_destroy_exec_env(exec_env);
        if (module_inst)
            wasm_runtime_deinstantiate(module_inst);
        if (module)
            wasm_runtime_unload(module);
        if (file_buf)
            BH_FREE(file_buf);
        wasm_runtime_destroy();
    }

    bool Instantiate(const char *fixture)
    {
        std::string path =
            std::string(THREAD_TEST_FIXTURE_DIR) + "/" + fixture + FIXTURE_EXT;
        uint32 file_size = 0;

        file_buf = (uint8 *)bh_read_file_to_buffer(path.c_str(), &file_size);
        if (!file_buf) {
            printf("Read fixture %s failed\n", path.c_str());
            return false;
        }

        module = wasm_runtime_load(file_buf, file_size, error_buf,
                                   sizeof(error_buf));
        if (!module) {
            printf("Load %s failed: %s\n", path.c_str(), error_buf);
            return false;
        }

        module_inst = wasm_runtime_instantiate(module, 64 * 1024, 16 * 1024,
                                               error_buf, sizeof(error_buf));
        if (!module_inst) {
            printf("Instantiate %s failed: %s\n", path.c_str(), error_buf);
            return false;
        }

        exec_env = wasm_runtime_create_exec_env(module_inst, 64 * 1024);
        return exec_env != NULL;
    }

    /* Call an exported i32 function, the exception must stay unset */
    int32 CallI32(const char *name, std::vector<uint32> args)
    {
        wasm_function_inst_t func =
            wasm_runtime_lookup_function(module_inst, name);
        uint32 argv[4] = { 0 };

        EXPECT_NE(func, nullptr) << "export " << name << " not found";
        for (size_t i = 0; i < args.size(); i++)
            argv[i] = args[i];

        if (!func
            || !wasm_runtime_call_wasm(exec_env, func, (uint32)args.size(),
                                       argv))
            return -2;

        EXPECT_EQ(wasm_runtime_get_exception(module_inst), nullptr);
        return (int32)argv[0];
    }

    RuntimeInitArgs init_args;
    char heap_buf[512 * 1024];
    char error_buf[256];
    uint8 *file_buf = NULL;
    wasm_module_t module = NULL;
    wasm_module_inst_t module_inst = NULL;
    wasm_exec_env_t exec_env = NULL;
};

#endif /* THREAD_TEST_COMMON_H */

/*
 * Copyright (C) 2026 WAMR Community.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

/*
 * (module
 *   (import "wasi_snapshot_preview1" "sock_addr_resolve"
 *     (func $resolve (param i32 i32 i32 i32 i32 i32) (result i32)))
 *   (memory (export "memory") 1)
 *   (data (i32.const 0) "localhost\00")
 *   (func (export "resolve") (param i32) (result i32)
 *     (call $resolve (i32.const 0) (i32.const 16) (i32.const 32)
 *                     (i32.const 64) (local.get 0) (i32.const 128))))
 */
static const uint8_t sock_addr_resolve_wasm[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x10, 0x02, 0x60,
    0x06, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7f,
    0x01, 0x7f, 0x02, 0x2c, 0x01, 0x16, 0x77, 0x61, 0x73, 0x69, 0x5f, 0x73,
    0x6e, 0x61, 0x70, 0x73, 0x68, 0x6f, 0x74, 0x5f, 0x70, 0x72, 0x65, 0x76,
    0x69, 0x65, 0x77, 0x31, 0x11, 0x73, 0x6f, 0x63, 0x6b, 0x5f, 0x61, 0x64,
    0x64, 0x72, 0x5f, 0x72, 0x65, 0x73, 0x6f, 0x6c, 0x76, 0x65, 0x00, 0x00,
    0x03, 0x02, 0x01, 0x01, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07, 0x14, 0x02,
    0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x07, 0x72, 0x65,
    0x73, 0x6f, 0x6c, 0x76, 0x65, 0x00, 0x01, 0x0a, 0x14, 0x01, 0x12, 0x00,
    0x41, 0x00, 0x41, 0x10, 0x41, 0x20, 0x41, 0xc0, 0x00, 0x20, 0x00, 0x41,
    0x80, 0x01, 0x10, 0x00, 0x0b, 0x0b, 0x10, 0x01, 0x00, 0x41, 0x00, 0x0b,
    0x0a, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x68, 0x6f, 0x73, 0x74, 0x00
};

class LibcWasiTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        RuntimeInitArgs init_args;
        memset(&init_args, 0, sizeof(init_args));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(heap_buf);
        ASSERT_TRUE(wasm_runtime_full_init(&init_args));

        /* The loader may modify the buffer, so each test gets its own copy */
        bh_memcpy_s(wasm_buf, sizeof(wasm_buf), sock_addr_resolve_wasm,
                    sizeof(sock_addr_resolve_wasm));
        module = wasm_runtime_load(wasm_buf, sizeof(wasm_buf), error_buf,
                                   sizeof(error_buf));
        ASSERT_NE(module, nullptr) << error_buf;
        /* An empty allowlist keeps the call off the network */
        wasm_runtime_set_wasi_args(module, nullptr, 0, nullptr, 0, nullptr, 0,
                                   nullptr, 0);
        inst = wasm_runtime_instantiate(module, 8192, 8192, error_buf,
                                        sizeof(error_buf));
        ASSERT_NE(inst, nullptr) << error_buf;
        exec_env = wasm_runtime_create_exec_env(inst, 8192);
        ASSERT_NE(exec_env, nullptr);
    }

    void TearDown() override
    {
        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(inst);
        wasm_runtime_unload(module);
        wasm_runtime_destroy();
    }

    uint8_t wasm_buf[sizeof(sock_addr_resolve_wasm)];
    char heap_buf[512 * 1024];
    char error_buf[256];
    wasm_module_t module = nullptr;
    wasm_module_inst_t inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
};

TEST_F(LibcWasiTest, sock_addr_resolve_zero_capacity)
{
    wasm_function_inst_t func = wasm_runtime_lookup_function(inst, "resolve");
    ASSERT_NE(func, nullptr);

    uint32_t argv[1] = { 0 };
    ASSERT_TRUE(wasm_runtime_call_wasm(exec_env, func, 1, argv))
        << wasm_runtime_get_exception(inst);
    /* __WASI_EACCES from the empty allowlist, so the size checks passed */
    EXPECT_EQ(argv[0], 2u);
}

TEST_F(LibcWasiTest, sock_addr_resolve_oversized_capacity)
{
    wasm_function_inst_t func = wasm_runtime_lookup_function(inst, "resolve");
    ASSERT_NE(func, nullptr);

    uint32_t argv[1] = { UINT32_MAX };
    ASSERT_FALSE(wasm_runtime_call_wasm(exec_env, func, 1, argv));
    EXPECT_NE(strstr(wasm_runtime_get_exception(inst), "out of bounds"),
              nullptr);
}

/*
 * Copyright (C) 2026 WAMR Community.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

/*
 * (module
 *   (func (export "count") (param $n i32) (result i32)
 *     (local $i i32)
 *     (block $done
 *       (loop $top
 *         (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
 *         (local.set $i (i32.add (local.get $i) (i32.const 1)))
 *         (br $top)))
 *     (local.get $i)))
 */
static const uint8_t wasm_count[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01,
    0x60, 0x01, 0x7F, 0x01, 0x7F, 0x03, 0x02, 0x01, 0x00, 0x07, 0x09,
    0x01, 0x05, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x00, 0x00, 0x0A, 0x1E,
    0x01, 0x1C, 0x01, 0x01, 0x7F, 0x02, 0x40, 0x03, 0x40, 0x20, 0x01,
    0x20, 0x00, 0x4F, 0x0D, 0x01, 0x20, 0x01, 0x41, 0x01, 0x6A, 0x21,
    0x01, 0x0C, 0x00, 0x0B, 0x0B, 0x20, 0x01, 0x0B
};

class InstructionMeteringTest : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);

        /* wasm_runtime_load() modifies the buffer, so load a copy. */
        bh_memcpy_s(wasm_buf, sizeof(wasm_buf), wasm_count, sizeof(wasm_count));
        module = wasm_runtime_load(wasm_buf, sizeof(wasm_buf), error_buf,
                                   sizeof(error_buf));
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
        wasm_runtime_destroy();
    }

    /* Call "count" with the given limit, return false on a trap. */
    bool call_count(uint32_t n, int limit, uint32_t *result)
    {
        wasm_function_inst_t func =
            wasm_runtime_lookup_function(module_inst, "count");
        uint32_t argv[1] = { n };

        wasm_runtime_clear_exception(module_inst);
        wasm_runtime_set_instruction_count_limit(exec_env, limit);
        bool ok = wasm_runtime_call_wasm(exec_env, func, 1, argv);
        *result = argv[0];
        return ok;
    }

    bool limit_exceeded()
    {
        const char *exception = wasm_runtime_get_exception(module_inst);
        return exception && strstr(exception, "instruction limit exceeded");
    }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
    uint8_t wasm_buf[sizeof(wasm_count)];
    wasm_module_t module = nullptr;
    wasm_module_inst_t module_inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
};

TEST_F(InstructionMeteringTest, low_budget_stops_counting_loop)
{
    uint32_t result;

    EXPECT_FALSE(call_count(1000000, 1000, &result));
    EXPECT_TRUE(limit_exceeded());
}

TEST_F(InstructionMeteringTest, high_budget_completes_loop)
{
    uint32_t result = 0;

    EXPECT_TRUE(call_count(1000, 1000000, &result))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(result, 1000u);
}

TEST_F(InstructionMeteringTest, budget_boundary)
{
    uint32_t result = 0;
#if WASM_ENABLE_FAST_INTERP != 0
    /* After the loader rewrites it, count(n) runs 4n + 4 instructions. */
    const int cost = 4004;
#else
    /* The classic interpreter runs every opcode: 9n + 9 instructions. */
    const int cost = 9009;
#endif

    EXPECT_FALSE(call_count(1000, cost - 1, &result));
    EXPECT_TRUE(limit_exceeded());
    EXPECT_TRUE(call_count(1000, cost, &result))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(result, 1000u);
}

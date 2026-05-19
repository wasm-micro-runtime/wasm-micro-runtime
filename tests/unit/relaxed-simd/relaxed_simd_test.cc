/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Gtest coverage for the fast-interp relaxed-SIMD opcode lowering
 * gated by `WAMR_BUILD_RELAXED_SIMD=1`. Two angles:
 *
 *   1. Load-time validation — a module containing a relaxed-SIMD
 *      opcode loads cleanly (the loader's prepare_bytecode SIMD
 *      switch recognizes 0x100..0x113). Without commit 1 of the
 *      patch series the loader would reject with
 *      `"invalid opcode 0xfd 100"`.
 *
 *   2. Runtime dispatch — calling a function that executes
 *      `f32x4.relaxed_madd` returns the FMA-rounded result. The
 *      result encoding (4×i32 bit pattern packed into the low i64
 *      of the v128 via `i64x2.extract_lane 0`) is bit-identical
 *      across aarch64/x86-64 because the inputs are exact under
 *      both single-rounded (hardware FMA) and double-rounded
 *      (split mul+add) semantics — every multiplication and
 *      addition is exactly representable in f32.
 */

#include "gtest/gtest.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

class RelaxedSimdTest : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);
    }

    virtual void TearDown() { wasm_runtime_destroy(); }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
};

/*
 * Minimal wasm module that exports a single `madd` function:
 *
 *   (module
 *     (func (export "madd") (result i64)
 *       v128.const f32x4 1 2 3 4
 *       v128.const f32x4 10 20 30 40
 *       v128.const f32x4 100 200 300 400
 *       f32x4.relaxed_madd            ;; opcode 0xfd 0x85 0x02 (= 0x105)
 *       i64x2.extract_lane 0))
 *
 * Bytes below are the raw output of `wasm-tools parse` on that WAT,
 * inlined so the test has no wabt / wat-runtime dependency at run.
 */
static const uint8_t MADD_WASM[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x00, 0x01, 0x7E, 0x03, 0x02, 0x01, 0x00, 0x07, 0x08, 0x01, 0x04, 0x6D,
    0x61, 0x64, 0x64, 0x00, 0x00, 0x0A, 0x40, 0x01, 0x3E, 0x00, 0xFD, 0x0C,
    0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40,
    0x00, 0x00, 0x80, 0x40, 0xFD, 0x0C, 0x00, 0x00, 0x20, 0x41, 0x00, 0x00,
    0xA0, 0x41, 0x00, 0x00, 0xF0, 0x41, 0x00, 0x00, 0x20, 0x42, 0xFD, 0x0C,
    0x00, 0x00, 0xC8, 0x42, 0x00, 0x00, 0x48, 0x43, 0x00, 0x00, 0x96, 0x43,
    0x00, 0x00, 0xC8, 0x43, 0xFD, 0x85, 0x02, 0xFD, 0x1D, 0x00, 0x0B
};

TEST_F(RelaxedSimdTest, load_module_with_relaxed_madd)
{
    char err[128] = { 0 };
    /* The runtime API expects a mutable buffer (modifies in
     * place during load); copy into a heap buffer first. */
    uint8_t buf[sizeof(MADD_WASM)];
    memcpy(buf, MADD_WASM, sizeof(MADD_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr)
        << "load failed: " << err
        << " — make sure WAMR_BUILD_RELAXED_SIMD=1 is set";
    wasm_runtime_unload(module);
}

TEST_F(RelaxedSimdTest, invoke_relaxed_madd_returns_fma_result)
{
    char err[128] = { 0 };
    uint8_t buf[sizeof(MADD_WASM)];
    memcpy(buf, MADD_WASM, sizeof(MADD_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr) << "load failed: " << err;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 32768u, 32768u, err, (uint32_t)sizeof(err));
    ASSERT_NE(inst, nullptr) << "instantiate failed: " << err;

    wasm_function_inst_t func = wasm_runtime_lookup_function(inst, "madd");
    ASSERT_NE(func, nullptr) << "export `madd` not found";

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 32768u);
    ASSERT_NE(env, nullptr);

    uint32_t argv[2] = { 0, 0 };
    bool ok = wasm_runtime_call_wasm(env, func, 0, argv);
    EXPECT_TRUE(ok) << "call_wasm failed: " << wasm_runtime_get_exception(inst);

    /*
     * Expected: f32x4.relaxed_madd((1,2,3,4), (10,20,30,40),
     *                              (100,200,300,400))
     *         = (1*10+100, 2*20+200, 3*30+300, 4*40+400)
     *         = (110, 240, 390, 560)
     *
     * As bit patterns:
     *   f32(110) = 0x42DC0000
     *   f32(240) = 0x43700000
     *   f32(390) = 0x43C30000
     *   f32(560) = 0x440C0000
     *
     * i64x2.extract_lane 0 packs lanes 0,1 of the v128 into the
     * low i64:
     *   high i32 (argv[1]) = lane 1 = 0x43700000
     *   low  i32 (argv[0]) = lane 0 = 0x42DC0000
     *
     * (Both single-rounded FMA hardware and split mul+add
     * produce the same bit pattern here — every product and sum
     * is exactly representable in f32.)
     */
    EXPECT_EQ(argv[0], 0x42DC0000u);
    EXPECT_EQ(argv[1], 0x43700000u);

    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

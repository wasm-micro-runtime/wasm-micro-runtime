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

/*
 * Regression test for the i16-intermediate truncation bug in
 * `i32x4.relaxed_dot_i8x16_i7x16_add_s` flagged by the chatgpt-
 * codex-connector code review on PR #3 (commit "fast-interp:
 * i32x4.relaxed_dot_i8x16_i7x16_add_s preserve i16 intermediate").
 *
 *   (module
 *     (func (export "dot_add_i16_overflow") (result i64)
 *       v128.const i8x16 -128 -128 -128 -128 -128 -128 -128 -128
 *                        -128 -128 -128 -128 -128 -128 -128 -128
 *       v128.const i8x16 -128 -128 -128 -128 -128 -128 -128 -128
 *                        -128 -128 -128 -128 -128 -128 -128 -128
 *       v128.const i32x4 0 0 0 0
 *       i32x4.relaxed_dot_i8x16_i7x16_add_s
 *       i64x2.extract_lane 0))
 *
 * With a = b = 0x80 (i8 = -128) in all 16 bytes and c = 0, the
 * spec-allowed result set is {-65536, -1, 65534} per lane (the
 * three possible wrap/saturate combinations of the two pair
 * sums). The pre-fix direct-sum impl produced 65536 — outside
 * that set. The fix preserves the i16 truncation between the
 * pair sum and the extadd_pairwise, producing -65536 per lane.
 *
 *   low i64 = (lane1 << 32) | lane0 = 0xffff0000_ffff0000
 */
static const uint8_t DOT_ADD_OVERFLOW_WASM[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x00, 0x01, 0x7e, 0x03, 0x02, 0x01, 0x00, 0x07, 0x18, 0x01, 0x14, 0x64,
    0x6f, 0x74, 0x5f, 0x61, 0x64, 0x64, 0x5f, 0x69, 0x31, 0x36, 0x5f, 0x6f,
    0x76, 0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x00, 0x00, 0x0a, 0x40, 0x01,
    0x3e, 0x00, 0xfd, 0x0c, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xfd, 0x0c, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0xfd, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x93, 0x02, 0xfd,
    0x1d, 0x00, 0x0b
};

TEST_F(RelaxedSimdTest, dot_add_i16_intermediate_overflow_regression)
{
    char err[128] = { 0 };
    uint8_t buf[sizeof(DOT_ADD_OVERFLOW_WASM)];
    memcpy(buf, DOT_ADD_OVERFLOW_WASM, sizeof(DOT_ADD_OVERFLOW_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr) << "load failed: " << err;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 32768u, 32768u, err, (uint32_t)sizeof(err));
    ASSERT_NE(inst, nullptr) << "instantiate failed: " << err;

    wasm_function_inst_t func =
        wasm_runtime_lookup_function(inst, "dot_add_i16_overflow");
    ASSERT_NE(func, nullptr) << "export `dot_add_i16_overflow` not found";

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 32768u);
    ASSERT_NE(env, nullptr);

    uint32_t argv[2] = { 0, 0 };
    bool ok = wasm_runtime_call_wasm(env, func, 0, argv);
    EXPECT_TRUE(ok) << "call_wasm failed: " << wasm_runtime_get_exception(inst);

    /* Per-lane result: -65536 = 0xffff0000 (i32). i64x2.extract_lane 0
     * packs lanes 0 and 1, both = 0xffff0000:
     *   argv[0] (low  i32) = 0xffff0000
     *   argv[1] (high i32) = 0xffff0000
     * If anyone refactors the impl back to direct-sum, both lanes
     * will be 0x00010000 (= 65536) and this test will fail. */
    EXPECT_EQ(argv[0], 0xffff0000u);
    EXPECT_EQ(argv[1], 0xffff0000u);

    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

/*
 * Pinning test for `i16x8.relaxed_dot_i8x16_i7x16_s` at the same
 * i16-intermediate overflow boundary. The current impl correctly
 * truncates to i16 via `result.i16x8[lane] = (int16)sum` on
 * wasm_interp_fast.c:8103. Same input pattern (a = b = 0x80
 * everywhere); each i16 lane = (int16)32768 = -32768 = 0x8000.
 *
 *   low i64 = four i16 lanes packed little-endian
 *           = 0x8000_8000_8000_8000
 *
 * If a future refactor drops the (int16) cast in the sibling
 * op, this test fires before the bug ships.
 *
 *   (module
 *     (func (export "dot_s_i16_overflow_pin") (result i64)
 *       v128.const i8x16 -128 ... (16x)
 *       v128.const i8x16 -128 ... (16x)
 *       i16x8.relaxed_dot_i8x16_i7x16_s
 *       i64x2.extract_lane 0))
 */
static const uint8_t DOT_S_PIN_WASM[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x00, 0x01, 0x7e, 0x03, 0x02, 0x01, 0x00, 0x07, 0x1a, 0x01, 0x16, 0x64,
    0x6f, 0x74, 0x5f, 0x73, 0x5f, 0x69, 0x31, 0x36, 0x5f, 0x6f, 0x76, 0x65,
    0x72, 0x66, 0x6c, 0x6f, 0x77, 0x5f, 0x70, 0x69, 0x6e, 0x00, 0x00, 0x0a,
    0x2e, 0x01, 0x2c, 0x00, 0xfd, 0x0c, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xfd, 0x0c,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0xfd, 0x92, 0x02, 0xfd, 0x1d, 0x00, 0x0b
};

TEST_F(RelaxedSimdTest, dot_s_i16_overflow_pin_sibling_op)
{
    char err[128] = { 0 };
    uint8_t buf[sizeof(DOT_S_PIN_WASM)];
    memcpy(buf, DOT_S_PIN_WASM, sizeof(DOT_S_PIN_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr) << "load failed: " << err;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 32768u, 32768u, err, (uint32_t)sizeof(err));
    ASSERT_NE(inst, nullptr) << "instantiate failed: " << err;

    wasm_function_inst_t func =
        wasm_runtime_lookup_function(inst, "dot_s_i16_overflow_pin");
    ASSERT_NE(func, nullptr) << "export `dot_s_i16_overflow_pin` not found";

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 32768u);
    ASSERT_NE(env, nullptr);

    uint32_t argv[2] = { 0, 0 };
    bool ok = wasm_runtime_call_wasm(env, func, 0, argv);
    EXPECT_TRUE(ok) << "call_wasm failed: " << wasm_runtime_get_exception(inst);

    /* low i64 = four packed i16 lanes, all = (int16)32768 = -32768
     *         = 0x8000_8000_8000_8000
     * argv[0] (low  i32) = 0x80008000
     * argv[1] (high i32) = 0x80008000 */
    EXPECT_EQ(argv[0], 0x80008000u);
    EXPECT_EQ(argv[1], 0x80008000u);

    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

/*
 * Spec-allowed-set test for `i16x8.relaxed_q15mulr_s` at the
 * INT16_MIN * INT16_MIN overflow boundary.
 *
 *   (module
 *     (func (export "q15mulr_int16_min_squared") (result i64)
 *       v128.const i16x8 -32768 0 0 0 0 0 0 0
 *       v128.const i16x8 -32768 0 0 0 0 0 0 0
 *       i16x8.relaxed_q15mulr_s
 *       i64x2.extract_lane 0))
 *
 * Q15 multiply-with-rounding: lane = sat_s((a*b + 0x4000) >> 15).
 * For a = b = INT16_MIN:
 *   a*b      = (-32768)*(-32768) = 0x40000000
 *   + 0x4000 = 0x40004000
 *   >> 15    = 0x8000 = 32768          (overflows i16)
 *   sat_s    = 32767 = 0x7fff          (saturate, IEEE/x86 PMULHRSW)
 *   wrap     = (int16)32768 = 0x8000   (truncate, spec-allowed)
 *
 * The spec's relaxed clause permits either lowering, so the lane-0
 * value must be 0x7fff OR 0x8000. Lanes 1..7 are 0 (deterministic).
 * Encoded as the low i64 (i64x2.extract_lane 0) the spec-allowed
 * set is { 0x0000_0000_0000_7fff, 0x0000_0000_0000_8000 }.
 *
 * WAMR's hand-rolled lowering picks saturate (0x7fff); this test
 * pins the choice via membership rather than exact equality, so a
 * future switch to wrap (spec-allowed) does not break the test.
 */
static const uint8_t Q15MULR_OVERFLOW_WASM[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01,
    0x60, 0x00, 0x01, 0x7e, 0x03, 0x02, 0x01, 0x00, 0x07, 0x1d, 0x01,
    0x19, 0x71, 0x31, 0x35, 0x6d, 0x75, 0x6c, 0x72, 0x5f, 0x69, 0x6e,
    0x74, 0x31, 0x36, 0x5f, 0x6d, 0x69, 0x6e, 0x5f, 0x73, 0x71, 0x75,
    0x61, 0x72, 0x65, 0x64, 0x00, 0x00, 0x0a, 0x2e, 0x01, 0x2c, 0x00,
    0xfd, 0x0c, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x0c, 0x00, 0x80,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xfd, 0x91, 0x02, 0xfd, 0x1d, 0x00, 0x0b
};

TEST_F(RelaxedSimdTest, q15mulr_int16_min_squared_either_sat_or_wrap)
{
    char err[128] = { 0 };
    uint8_t buf[sizeof(Q15MULR_OVERFLOW_WASM)];
    memcpy(buf, Q15MULR_OVERFLOW_WASM, sizeof(Q15MULR_OVERFLOW_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr) << "load failed: " << err;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 32768u, 32768u, err, (uint32_t)sizeof(err));
    ASSERT_NE(inst, nullptr) << "instantiate failed: " << err;

    wasm_function_inst_t func =
        wasm_runtime_lookup_function(inst, "q15mulr_int16_min_squared");
    ASSERT_NE(func, nullptr) << "export `q15mulr_int16_min_squared` not found";

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 32768u);
    ASSERT_NE(env, nullptr);

    uint32_t argv[2] = { 0, 0 };
    bool ok = wasm_runtime_call_wasm(env, func, 0, argv);
    EXPECT_TRUE(ok) << "call_wasm failed: " << wasm_runtime_get_exception(inst);

    /* Lanes 1..3 must be 0 (deterministic). Encoded in argv: lanes
     * 1..3 occupy bits 16..63 of the 64-bit packed result.
     *   argv[0] (low i32)  = (lane1 << 16) | lane0
     *   argv[1] (high i32) = (lane3 << 16) | lane2 */
    EXPECT_EQ(argv[1], 0u) << "lanes 2,3 must be zero";
    EXPECT_EQ((argv[0] >> 16) & 0xffffu, 0u) << "lane 1 must be zero";

    /* Lane 0 = low 16 bits of argv[0]: either 0x7fff (sat) or
     * 0x8000 (wrap). Both spec-conformant per the relaxed-SIMD
     * implementation-defined clause for q15mulr_s. */
    uint32_t lane0 = argv[0] & 0xffffu;
    EXPECT_TRUE(lane0 == 0x7fffu || lane0 == 0x8000u)
        << "lane 0 = 0x" << std::hex << lane0
        << ", expected 0x7fff (saturate) or 0x8000 (wrap)";

    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

/*
 * Spec-allowed-set test for `f32x4.relaxed_madd` at the
 * (Inf * 0 + c) invalid-multiply boundary.
 *
 *   (module
 *     (func (export "madd_inf_times_zero_lo") (result i64)
 *       v128.const f32x4 inf inf inf inf
 *       v128.const f32x4 0 0 0 0
 *       v128.const f32x4 1.0 2.0 3.0 4.0
 *       f32x4.relaxed_madd
 *       i64x2.extract_lane 0)
 *     (func (export "madd_inf_times_zero_hi") (result i64) ;; lane 1)
 *
 * IEEE 754 §7.2: Inf × 0 is an invalid operation and produces NaN
 * (regardless of the subsequent add of `c`). Both fused-multiply-
 * add (`fma(Inf, 0, c)`) and unfused (`Inf * 0 + c`) lowerings of
 * relaxed_madd produce a NaN here — so the choice between them
 * doesn't affect the *kind* of result, only its specific bit
 * pattern. The relaxed-SIMD spec leaves the NaN bit pattern
 * implementation-defined, so the test checks the IEEE-754 NaN
 * predicate (exponent all-ones, fraction non-zero) per lane
 * rather than an exact bit pattern.
 *
 * This case is the relevant adversarial input for "do we
 * propagate NaN through the FMA path correctly when one of the
 * inputs is +Inf and another is +0?" — exactly the kind of
 * boundary the spec test set doesn't explicitly cover.
 */
static const uint8_t MADD_INF_TIMES_ZERO_WASM[] = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60,
    0x00, 0x01, 0x7e, 0x03, 0x03, 0x02, 0x00, 0x00, 0x07, 0x33, 0x02, 0x16,
    0x6d, 0x61, 0x64, 0x64, 0x5f, 0x69, 0x6e, 0x66, 0x5f, 0x74, 0x69, 0x6d,
    0x65, 0x73, 0x5f, 0x7a, 0x65, 0x72, 0x6f, 0x5f, 0x6c, 0x6f, 0x00, 0x00,
    0x16, 0x6d, 0x61, 0x64, 0x64, 0x5f, 0x69, 0x6e, 0x66, 0x5f, 0x74, 0x69,
    0x6d, 0x65, 0x73, 0x5f, 0x7a, 0x65, 0x72, 0x6f, 0x5f, 0x68, 0x69, 0x00,
    0x01, 0x0a, 0x7f, 0x02, 0x3e, 0x00, 0xfd, 0x0c, 0x00, 0x00, 0x80, 0x7f,
    0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f,
    0xfd, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x0c, 0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x80, 0x40,
    0xfd, 0x85, 0x02, 0xfd, 0x1d, 0x00, 0x0b, 0x3e, 0x00, 0xfd, 0x0c, 0x00,
    0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00,
    0x00, 0x80, 0x7f, 0xfd, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfd, 0x0c, 0x00,
    0x00, 0x80, 0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x40, 0x40, 0x00,
    0x00, 0x80, 0x40, 0xfd, 0x85, 0x02, 0xfd, 0x1d, 0x01, 0x0b
};

/* Helper: true iff the f32 bit pattern is any NaN
 * (exponent = 0xff, fraction != 0). */
static bool
f32_bits_are_nan(uint32_t bits)
{
    uint32_t exp = (bits >> 23) & 0xff;
    uint32_t frac = bits & 0x7fffff;
    return exp == 0xff && frac != 0u;
}

TEST_F(RelaxedSimdTest, madd_inf_times_zero_propagates_nan)
{
    char err[128] = { 0 };
    uint8_t buf[sizeof(MADD_INF_TIMES_ZERO_WASM)];
    memcpy(buf, MADD_INF_TIMES_ZERO_WASM, sizeof(MADD_INF_TIMES_ZERO_WASM));

    wasm_module_t module = wasm_runtime_load(buf, (uint32_t)sizeof(buf), err,
                                             (uint32_t)sizeof(err));
    ASSERT_NE(module, nullptr) << "load failed: " << err;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 32768u, 32768u, err, (uint32_t)sizeof(err));
    ASSERT_NE(inst, nullptr) << "instantiate failed: " << err;

    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 32768u);
    ASSERT_NE(env, nullptr);

    /* Call the lo half (lanes 0,1) then the hi half (lanes 2,3);
     * each call returns one i64 packing two f32 lanes:
     *   argv[0] = lane2k bits, argv[1] = lane2k+1 bits */
    for (uint32_t half = 0; half < 2; half++) {
        const char *name =
            half == 0 ? "madd_inf_times_zero_lo" : "madd_inf_times_zero_hi";
        wasm_function_inst_t func = wasm_runtime_lookup_function(inst, name);
        ASSERT_NE(func, nullptr) << "export `" << name << "` not found";

        uint32_t argv[2] = { 0, 0 };
        bool ok = wasm_runtime_call_wasm(env, func, 0, argv);
        EXPECT_TRUE(ok) << "call_wasm `" << name
                        << "` failed: " << wasm_runtime_get_exception(inst);

        EXPECT_TRUE(f32_bits_are_nan(argv[0]))
            << name << " lane " << (2 * half) << " not NaN: bits = 0x"
            << std::hex << argv[0];
        EXPECT_TRUE(f32_bits_are_nan(argv[1]))
            << name << " lane " << (2 * half + 1) << " not NaN: bits = 0x"
            << std::hex << argv[1];
    }

    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

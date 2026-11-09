/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_runtime.h"
#include "test_invoke_native.h"
#include <string.h>
#include <stdlib.h>

#ifdef STORE_I64
#undef STORE_I64
#endif
#ifdef STORE_F64
#undef STORE_F64
#endif

#define STORE_I64(addr, value)                                              \
    ((void)(((uint32 *)(addr))[0] = (uint32)((uint64)(value) & 0xFFFFFFFF), \
            ((uint32 *)(addr))[1] = (uint32)((uint64)(value) >> 32)))

#define STORE_F64(addr, value)                                       \
    ((void)(((uint32 *)(addr))[0] = ((uint32 *)(void *)&(value))[0], \
            ((uint32 *)(addr))[1] = ((uint32 *)(void *)&(value))[1]))

#define I32 VALUE_TYPE_I32
#define I64 VALUE_TYPE_I64
#define F32 VALUE_TYPE_F32
#define F64 VALUE_TYPE_F64

typedef struct TestVal {
    union {
        int32 i32;
        int64 i64;
        float32 f32;
        float64 f64;
    } u;
    uint8 type;
} TestVal;

#define T_I32(X) { { .i32 = (int32)(X) }, VALUE_TYPE_I32 }
#define T_I64(X) { { .i64 = (int64)(X) }, VALUE_TYPE_I64 }
#define T_F32(X) { { .f32 = (float32)(X) }, VALUE_TYPE_F32 }
#define T_F64(X) { { .f64 = (float64)(X) }, VALUE_TYPE_F64 }

static WASMFuncType *
create_func_type(uint32 param_count, uint32 result_count,
                 const uint8 *param_types, const uint8 *result_types)
{
    WASMFuncType *ft =
        malloc(sizeof(WASMFuncType) + param_count + result_count);
    if (!ft)
        return NULL;
    memset(ft, 0, sizeof(WASMFuncType) + param_count + result_count);
    ft->param_count = param_count;
    ft->result_count = result_count;
    for (uint32 i = 0; i < param_count; i++) {
        ft->types[i] = param_types[i];
    }
    for (uint32 i = 0; i < result_count; i++) {
        ft->types[param_count + i] = result_types[i];
    }
    return ft;
}

static struct {
    int call_count;
    int i32_vals[16];
    int64 i64_vals[16];
    float f32_vals[16];
    double f64_vals[16];
    int mixed_i32[16];
    int64 mixed_i64[16];
    float mixed_f32[16];
    double mixed_f64[16];
} g_test_results;

static int g_test_failed = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static void
test_eq(long long val, long long expected, int line)
{
    if (val != expected) {
        printf("  FAIL: line %d: expected %lld, got %lld\n", line, expected,
               val);
        g_test_failed = 1;
    }
}

static void
test_float_eq(double val, double expected, int line)
{
    double diff = val - expected;
    if (diff < 0)
        diff = -diff;
    if (diff > 0.0001) {
        printf("  FAIL: line %d: expected %f, got %f\n", line, expected, val);
        g_test_failed = 1;
    }
}

#define TEST_EQ(val, expected) \
    test_eq((long long)(val), (long long)(expected), __LINE__)
#define TEST_FLOAT_EQ(val, expected) \
    test_float_eq((double)(val), (double)(expected), __LINE__)

/* --- Native test functions --- */

static void
native_i32_overflow(WASMExecEnv *exec_env, int r0, int r1, int r2, int r3,
                    int r4, int r5, int r6, int s0, int s1, int s2, int s3)
{
    g_test_results.call_count++;
    g_test_results.i32_vals[0] = r0;
    g_test_results.i32_vals[1] = r1;
    g_test_results.i32_vals[2] = r2;
    g_test_results.i32_vals[3] = r3;
    g_test_results.i32_vals[4] = r4;
    g_test_results.i32_vals[5] = r5;
    g_test_results.i32_vals[6] = r6;
    g_test_results.i32_vals[7] = s0;
    g_test_results.i32_vals[8] = s1;
    g_test_results.i32_vals[9] = s2;
    g_test_results.i32_vals[10] = s3;
}

static void
native_i64_overflow(WASMExecEnv *exec_env, int64 r0, int64 r1, int64 r2,
                    int64 r3, int64 r4, int64 r5, int64 r6, int64 s0, int64 s1,
                    int64 s2, int64 s3)
{
    g_test_results.call_count++;
    g_test_results.i64_vals[0] = r0;
    g_test_results.i64_vals[1] = r1;
    g_test_results.i64_vals[2] = r2;
    g_test_results.i64_vals[3] = r3;
    g_test_results.i64_vals[4] = r4;
    g_test_results.i64_vals[5] = r5;
    g_test_results.i64_vals[6] = r6;
    g_test_results.i64_vals[7] = s0;
    g_test_results.i64_vals[8] = s1;
    g_test_results.i64_vals[9] = s2;
    g_test_results.i64_vals[10] = s3;
}

static void
native_i32_i32_i64_on_stack(WASMExecEnv *exec_env, int r0, int r1, int r2,
                            int r3, int r4, int r5, int r6, int s0, int s1,
                            int64 s2)
{
    g_test_results.call_count++;
    g_test_results.mixed_i32[0] = r0;
    g_test_results.mixed_i32[1] = r1;
    g_test_results.mixed_i32[2] = r2;
    g_test_results.mixed_i32[3] = r3;
    g_test_results.mixed_i32[4] = r4;
    g_test_results.mixed_i32[5] = r5;
    g_test_results.mixed_i32[6] = r6;
    g_test_results.mixed_i32[7] = s0;
    g_test_results.mixed_i32[8] = s1;
    g_test_results.mixed_i64[0] = s2;
}

static void
native_f32_overflow(WASMExecEnv *exec_env, float fr0, float fr1, float fr2,
                    float fr3, float fr4, float fr5, float fr6, float fr7,
                    float fs0, float fs1, float fs2, float fs3)
{
    g_test_results.call_count++;
    g_test_results.f32_vals[0] = fr0;
    g_test_results.f32_vals[1] = fr1;
    g_test_results.f32_vals[2] = fr2;
    g_test_results.f32_vals[3] = fr3;
    g_test_results.f32_vals[4] = fr4;
    g_test_results.f32_vals[5] = fr5;
    g_test_results.f32_vals[6] = fr6;
    g_test_results.f32_vals[7] = fr7;
    g_test_results.f32_vals[8] = fs0;
    g_test_results.f32_vals[9] = fs1;
    g_test_results.f32_vals[10] = fs2;
    g_test_results.f32_vals[11] = fs3;
}

static void
native_f64_overflow(WASMExecEnv *exec_env, double fr0, double fr1, double fr2,
                    double fr3, double fr4, double fr5, double fr6, double fr7,
                    double fs0, double fs1, double fs2, double fs3)
{
    g_test_results.call_count++;
    g_test_results.f64_vals[0] = fr0;
    g_test_results.f64_vals[1] = fr1;
    g_test_results.f64_vals[2] = fr2;
    g_test_results.f64_vals[3] = fr3;
    g_test_results.f64_vals[4] = fr4;
    g_test_results.f64_vals[5] = fr5;
    g_test_results.f64_vals[6] = fr6;
    g_test_results.f64_vals[7] = fr7;
    g_test_results.f64_vals[8] = fs0;
    g_test_results.f64_vals[9] = fs1;
    g_test_results.f64_vals[10] = fs2;
    g_test_results.f64_vals[11] = fs3;
}

static void
native_f32_f32_f64_on_stack(WASMExecEnv *exec_env, float fr0, float fr1,
                            float fr2, float fr3, float fr4, float fr5,
                            float fr6, float fr7, float fs0, float fs1,
                            double fs2)
{
    g_test_results.call_count++;
    g_test_results.f32_vals[0] = fr0;
    g_test_results.f32_vals[1] = fr1;
    g_test_results.f32_vals[2] = fr2;
    g_test_results.f32_vals[3] = fr3;
    g_test_results.f32_vals[4] = fr4;
    g_test_results.f32_vals[5] = fr5;
    g_test_results.f32_vals[6] = fr6;
    g_test_results.f32_vals[7] = fr7;
    g_test_results.mixed_f32[0] = fs0;
    g_test_results.mixed_f32[1] = fs1;
    g_test_results.mixed_f64[0] = fs2;
}

static void
native_mixed_overflow(WASMExecEnv *exec_env, int r0, int r1, int r2, int r3,
                      int r4, int r5, int r6, float fr0, float fr1, float fr2,
                      float fr3, float fr4, float fr5, float fr6, float fr7,
                      int s0, float fs0, int64 s1, double fs1)
{
    g_test_results.call_count++;
    g_test_results.i32_vals[0] = r0;
    g_test_results.i32_vals[1] = r1;
    g_test_results.i32_vals[2] = r2;
    g_test_results.i32_vals[3] = r3;
    g_test_results.i32_vals[4] = r4;
    g_test_results.i32_vals[5] = r5;
    g_test_results.i32_vals[6] = r6;

    g_test_results.f32_vals[0] = fr0;
    g_test_results.f32_vals[1] = fr1;
    g_test_results.f32_vals[2] = fr2;
    g_test_results.f32_vals[3] = fr3;
    g_test_results.f32_vals[4] = fr4;
    g_test_results.f32_vals[5] = fr5;
    g_test_results.f32_vals[6] = fr6;
    g_test_results.f32_vals[7] = fr7;

    g_test_results.mixed_i32[0] = s0;
    g_test_results.mixed_f32[0] = fs0;
    g_test_results.mixed_i64[0] = s1;
    g_test_results.mixed_f64[0] = fs1;
}

static void
native_i32_i64_alignment(WASMExecEnv *exec_env, int r0, int r1, int r2, int r3,
                         int r4, int r5, int r6, int s0, int64 s1)
{
    g_test_results.call_count++;
    g_test_results.mixed_i32[0] = s0;
    g_test_results.mixed_i64[0] = s1;
}

static void
native_unaligned_total_stack_size(WASMExecEnv *exec_env, int r0, int r1, int r2,
                                  int r3, int r4, int r5, int r6, int s0,
                                  int s1, int s2)
{
    g_test_results.call_count++;
    g_test_results.mixed_i32[0] = s0;
    g_test_results.mixed_i32[1] = s1;
    g_test_results.mixed_i32[2] = s2;
}

static void
native_float_double_alignment(WASMExecEnv *exec_env, float fr0, float fr1,
                              float fr2, float fr3, float fr4, float fr5,
                              float fr6, float fr7, float fs0, double fs1)
{
    g_test_results.call_count++;
    g_test_results.mixed_f32[0] = fs0;
    g_test_results.mixed_f64[0] = fs1;
}

static int32
native_return_i32(WASMExecEnv *exec_env)
{
    return 0x12345678;
}

static int64
native_return_i64(WASMExecEnv *exec_env)
{
    return 0x12345678ABCDEFFFll;
}

static float32
native_return_f32(WASMExecEnv *exec_env)
{
    return 1234.5678f;
}

static float64
native_return_f64(WASMExecEnv *exec_env)
{
    return 87654321.12345678;
}

static void
run_generic_test(WASMExecEnv *exec_env, void *func_ptr, const TestVal *args,
                 uint32 param_count, uint8 ret_type, uint32 *argv_out)
{
    uint32 argv[128];
    uint32 *p = argv;
    uint8 *param_types = NULL;

    if (param_count > 0) {
        param_types = malloc(param_count);
        if (!param_types)
            return;
        for (uint32 i = 0; i < param_count; i++) {
            param_types[i] = args[i].type;
            if (args[i].type == VALUE_TYPE_I32) {
                *p++ = (uint32)args[i].u.i32;
            }
            else if (args[i].type == VALUE_TYPE_I64) {
                STORE_I64(p, args[i].u.i64);
                p += 2;
            }
            else if (args[i].type == VALUE_TYPE_F32) {
                *(float32 *)p++ = args[i].u.f32;
            }
            else if (args[i].type == VALUE_TYPE_F64) {
                STORE_F64(p, args[i].u.f64);
                p += 2;
            }
        }
    }

    WASMFuncType *func_type =
        create_func_type(param_count, ret_type ? 1 : 0, param_types,
                         ret_type ? &ret_type : NULL);
    if (param_types) {
        free(param_types);
    }

    if (!func_type)
        return;

    wasm_runtime_invoke_native(exec_env, func_ptr, func_type, NULL, NULL,
                               param_count > 0 ? argv : NULL, p - argv, argv);

    if (argv_out) {
        memcpy(argv_out, argv, 8 * sizeof(uint32));
    }

    free(func_type);
}

static void
run_test_i32_overflow(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_I32(100), T_I32(101), T_I32(102), T_I32(103),
                       T_I32(104), T_I32(105), T_I32(106), T_I32(107),
                       T_I32(108), T_I32(109), T_I32(110) };

    run_generic_test(exec_env, native_i32_overflow, args, 11, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 11; i++) {
        TEST_EQ(g_test_results.i32_vals[i], 100 + i);
    }
}

static void
run_test_i64_overflow(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_I64(200), T_I64(201), T_I64(202), T_I64(203),
                       T_I64(204), T_I64(205), T_I64(206), T_I64(207),
                       T_I64(208), T_I64(209), T_I64(210) };

    run_generic_test(exec_env, native_i64_overflow, args, 11, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 11; i++) {
        TEST_EQ(g_test_results.i64_vals[i], 200 + i);
    }
}

static void
run_test_i32_i32_i64_on_stack(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_I32(300), T_I32(301),
                       T_I32(302), T_I32(303),
                       T_I32(304), T_I32(305),
                       T_I32(306), T_I32(307),
                       T_I32(308), T_I64(0x1122334455667788ll) };

    run_generic_test(exec_env, native_i32_i32_i64_on_stack, args, 10, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 9; i++) {
        TEST_EQ(g_test_results.mixed_i32[i], 300 + i);
    }
    TEST_EQ(g_test_results.mixed_i64[0], 0x1122334455667788ll);
}

static void
run_test_f32_overflow(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_F32(400.0f), T_F32(401.0f), T_F32(402.0f),
                       T_F32(403.0f), T_F32(404.0f), T_F32(405.0f),
                       T_F32(406.0f), T_F32(407.0f), T_F32(408.0f),
                       T_F32(409.0f), T_F32(410.0f), T_F32(411.0f) };

    run_generic_test(exec_env, native_f32_overflow, args, 12, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 12; i++) {
        TEST_FLOAT_EQ(g_test_results.f32_vals[i], 400.0f + i);
    }
}

static void
run_test_f64_overflow(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_F64(500.0), T_F64(501.0), T_F64(502.0), T_F64(503.0),
                       T_F64(504.0), T_F64(505.0), T_F64(506.0), T_F64(507.0),
                       T_F64(508.0), T_F64(509.0), T_F64(510.0), T_F64(511.0) };

    run_generic_test(exec_env, native_f64_overflow, args, 12, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 12; i++) {
        TEST_FLOAT_EQ(g_test_results.f64_vals[i], 500.0 + i);
    }
}

static void
run_test_f32_f32_f64_on_stack(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_F32(600.0f), T_F32(601.0f), T_F32(602.0f),
                       T_F32(603.0f), T_F32(604.0f), T_F32(605.0f),
                       T_F32(606.0f), T_F32(607.0f), T_F32(608.0f),
                       T_F32(609.0f), T_F64(999.888) };

    run_generic_test(exec_env, native_f32_f32_f64_on_stack, args, 11, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 8; i++) {
        TEST_FLOAT_EQ(g_test_results.f32_vals[i], 600.0f + i);
    }
    TEST_FLOAT_EQ(g_test_results.mixed_f32[0], 608.0f);
    TEST_FLOAT_EQ(g_test_results.mixed_f32[1], 609.0f);
    TEST_FLOAT_EQ(g_test_results.mixed_f64[0], 999.888);
}

static void
run_test_mixed_overflow(WASMExecEnv *exec_env)
{
    TestVal args[] = {
        T_I32(700),       T_I32(701),    T_I32(702),
        T_I32(703),       T_I32(704),    T_I32(705),
        T_I32(706),       T_F32(800.0f), T_F32(801.0f),
        T_F32(802.0f),    T_F32(803.0f), T_F32(804.0f),
        T_F32(805.0f),    T_F32(806.0f), T_F32(807.0f),
        T_I32(707),       T_F32(808.0f), T_I64(0x5555666677778888ll),
        T_F64(12345.6789)
    };

    run_generic_test(exec_env, native_mixed_overflow, args, 19, 0, NULL);

    TEST_EQ(g_test_results.call_count, 1);
    for (int i = 0; i < 7; i++) {
        TEST_EQ(g_test_results.i32_vals[i], 700 + i);
    }
    for (int i = 0; i < 8; i++) {
        TEST_FLOAT_EQ(g_test_results.f32_vals[i], 800.0f + i);
    }
    TEST_EQ(g_test_results.mixed_i32[0], 707);
    TEST_FLOAT_EQ(g_test_results.mixed_f32[0], 808.0f);
    TEST_EQ(g_test_results.mixed_i64[0], 0x5555666677778888ll);
    TEST_FLOAT_EQ(g_test_results.mixed_f64[0], 12345.6789);
}

static void
run_test_i32_i64_alignment(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_I32(0), T_I32(1),  T_I32(2),
                       T_I32(3), T_I32(4),  T_I32(5),
                       T_I32(6), T_I32(42), T_I64(0xabcdef0123456789ll) };
    run_generic_test(exec_env, native_i32_i64_alignment, args, 9, 0, NULL);
    TEST_EQ(g_test_results.call_count, 1);
    TEST_EQ(g_test_results.mixed_i32[0], 42);
    TEST_EQ(g_test_results.mixed_i64[0], 0xabcdef0123456789ll);
}

static void
run_test_unaligned_total_stack_size(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_I32(0), T_I32(1), T_I32(2),  T_I32(3),  T_I32(4),
                       T_I32(5), T_I32(6), T_I32(70), T_I32(71), T_I32(72) };
    run_generic_test(exec_env, native_unaligned_total_stack_size, args, 10, 0,
                     NULL);
    TEST_EQ(g_test_results.call_count, 1);
    TEST_EQ(g_test_results.mixed_i32[0], 70);
    TEST_EQ(g_test_results.mixed_i32[1], 71);
    TEST_EQ(g_test_results.mixed_i32[2], 72);
}

static void
run_test_float_double_alignment(WASMExecEnv *exec_env)
{
    TestVal args[] = { T_F32(0.0f), T_F32(1.0f), T_F32(2.0f), T_F32(3.0f),
                       T_F32(4.0f), T_F32(5.0f), T_F32(6.0f), T_F32(7.0f),
                       T_F32(8.5f), T_F64(9.25) };
    run_generic_test(exec_env, native_float_double_alignment, args, 10, 0,
                     NULL);
    TEST_EQ(g_test_results.call_count, 1);
    TEST_FLOAT_EQ(g_test_results.mixed_f32[0], 8.5f);
    TEST_FLOAT_EQ(g_test_results.mixed_f64[0], 9.25);
}

static void
run_test_return_values(WASMExecEnv *exec_env)
{
    uint32 ret_buf[8];
    uint8 type_i32 = I32;
    uint8 type_i64 = I64;
    uint8 type_f32 = F32;
    uint8 type_f64 = F64;

    /* i32 */
    run_generic_test(exec_env, native_return_i32, NULL, 0, type_i32, ret_buf);
    TEST_EQ(ret_buf[0], 0x12345678);

    /* i64 */
    run_generic_test(exec_env, native_return_i64, NULL, 0, type_i64, ret_buf);
    TEST_EQ(*(int64 *)ret_buf, 0x12345678ABCDEFFFll);

    /* f32 */
    run_generic_test(exec_env, native_return_f32, NULL, 0, type_f32, ret_buf);
    TEST_FLOAT_EQ(*(float32 *)ret_buf, 1234.5678f);

    /* f64 */
    run_generic_test(exec_env, native_return_f64, NULL, 0, type_f64, ret_buf);
    TEST_FLOAT_EQ(*(float64 *)ret_buf, 87654321.12345678);
}

static void
execute_test(const char *test_name, void (*test_func)(WASMExecEnv *),
             WASMExecEnv *exec_env)
{
    printf("Running %s...\n", test_name);
    g_test_failed = 0;
    memset(&g_test_results, 0, sizeof(g_test_results));
    test_func(exec_env);
    if (g_test_failed) {
        printf("RESULT: FAIL\n\n");
        failed_tests++;
    }
    else {
        printf("RESULT: PASS\n\n");
        passed_tests++;
    }
}

int
test_invoke_native()
{
    WASMModuleInstance module_inst = { 0 };
    WASMExecEnv exec_env = { 0 };

    passed_tests = 0;
    failed_tests = 0;

    module_inst.module_type = Wasm_Module_Bytecode;
    exec_env.module_inst = (WASMModuleInstanceCommon *)&module_inst;

    printf("=========================================\n");
    printf("Starting Native Invoke Tests...\n");
    printf("=========================================\n");

    execute_test("run_test_i32_overflow", run_test_i32_overflow, &exec_env);
    execute_test("run_test_i64_overflow", run_test_i64_overflow, &exec_env);
    execute_test("run_test_i32_i32_i64_on_stack", run_test_i32_i32_i64_on_stack,
                 &exec_env);
    execute_test("run_test_f32_overflow", run_test_f32_overflow, &exec_env);
    execute_test("run_test_f64_overflow", run_test_f64_overflow, &exec_env);
    execute_test("run_test_f32_f32_f64_on_stack", run_test_f32_f32_f64_on_stack,
                 &exec_env);
    execute_test("run_test_mixed_overflow", run_test_mixed_overflow, &exec_env);
    execute_test("run_test_i32_i64_alignment", run_test_i32_i64_alignment,
                 &exec_env);
    execute_test("run_test_unaligned_total_stack_size",
                 run_test_unaligned_total_stack_size, &exec_env);
    execute_test("run_test_float_double_alignment",
                 run_test_float_double_alignment, &exec_env);
    execute_test("run_test_return_values", run_test_return_values, &exec_env);

    printf("=========================================\n");
    printf("Tests Summary: Passed: %d, Failed: %d\n", passed_tests,
           failed_tests);
    printf("=========================================\n");

    return failed_tests;
}

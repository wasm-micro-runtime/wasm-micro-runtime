/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <string.h>

#include "platform_common.h"
#include "wasm.h"
#include "wasm_runtime.h"
#include "wasm_runtime_common.h"

static bool test_failed;

#if WASM_ENABLE_SIMD != 0
#ifdef _MSC_VER
typedef union __declspec(intrin_type) __declspec(align(8)) TestV128 {
    __int8 i8[16];
    __int16 i16[8];
    __int32 i32[4];
    __int64 i64[2];
    unsigned __int8 u8[16];
    unsigned __int16 u16[8];
    unsigned __int32 u32[4];
    unsigned __int64 u64[2];
} TestV128;
#elif defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64) \
    || defined(BUILD_TARGET_RISCV64_LP64D)                         \
    || defined(BUILD_TARGET_RISCV64_LP64)
typedef long long TestV128
    __attribute__((__vector_size__(16), __may_alias__, __aligned__(1)));
#elif defined(BUILD_TARGET_AARCH64)
#include <arm_neon.h>
typedef uint32x4_t TestV128;
#endif

static bool
test_v128_matches(TestV128 value, uint32 expected)
{
    uint32 lanes[4];
    uint32 i;

    memcpy(lanes, &value, sizeof(lanes));
    for (i = 0; i < 4; i++) {
        if (lanes[i] != expected + i)
            return false;
    }
    return true;
}

static TestV128
test_native_v128(WASMModuleInstance *module_inst, TestV128 arg0, TestV128 arg1,
                 TestV128 arg2, TestV128 arg3, TestV128 arg4, TestV128 arg5,
                 TestV128 arg6, TestV128 arg7, TestV128 arg8)
{
    if (!test_v128_matches(arg0, 0x100) || !test_v128_matches(arg1, 0x200)
        || !test_v128_matches(arg2, 0x300) || !test_v128_matches(arg3, 0x400)
        || !test_v128_matches(arg4, 0x500) || !test_v128_matches(arg5, 0x600)
        || !test_v128_matches(arg6, 0x700) || !test_v128_matches(arg7, 0x800)
        || !test_v128_matches(arg8, 0x900)) {
        printf("test_native_v128 validation failed\n");
        test_failed = true;
    }
    return arg8;
}
#endif /* WASM_ENABLE_SIMD != 0 */

static void
test_native_args1(WASMModuleInstance *module_inst, int arg0, uint64_t arg1,
                  float arg2, double arg3, int arg4, int64_t arg5, int64_t arg6,
                  int arg7, double arg8, float arg9, int arg10, double arg11,
                  float arg12, int64_t arg13, uint64_t arg14, float arg15,
                  double arg16, int64_t arg17, uint64_t arg18, float arg19)
{
    printf("##test_native_args1 result:\n");
    printf("arg0: 0x%X, arg1: 0x%X%08X, arg2: %f, arg3: %f\n", arg0,
           (int32)(arg1 >> 32), (int32)arg1, (double)arg2, arg3);
    printf("arg4: 0x%X, arg5: 0x%X%08X, arg6: 0x%X%08X, arg7: 0x%X\n", arg4,
           (int32)(arg5 >> 32), (int32)arg5, (int32)(arg6 >> 32), (int32)arg6,
           arg7);
    printf("arg8: %f, arg9: %f, arg10: 0x%X, arg11: %f\n", arg8, (double)arg9,
           arg10, arg11);
    printf("arg12: %f, arg13: 0x%X%08X, arg14: 0x%X%08X, arg15: %f\n",
           (double)arg12, (int32)(arg13 >> 32), (int32)arg13,
           (int32)(arg14 >> 32), (int32)arg14, (double)arg15);
    printf("arg16: %f, arg17: 0x%X%08X, arg18: 0x%X%08X, arg19: %f\n", arg16,
           (int32)(arg17 >> 32), (int32)arg17, (int32)(arg18 >> 32),
           (int32)arg18, (double)arg19);

    if (arg0 != 0x12345678 || arg1 != 0xFFFFFFFF87654321ULL
        || arg2 != 1234.5678f || arg3 != 567890.1234 || arg4 != 0x11111111
        || (uint64)arg5 != 0xAAAAAAAABBBBBBBBULL || arg6 != 0x7788888899LL
        || arg7 != 0x3456 || arg8 != 8888.7777 || arg9 != 7777.8888f
        || arg10 != 0x66666 || arg11 != 999999.88888 || arg12 != 555555.22f
        || arg13 != 0xBBBBBAAAAAAAALL || arg14 != 0x3333AAAABBBBULL
        || arg15 != 88.77f || arg16 != 9999.01234 || arg17 != 0x1111122222222LL
        || arg18 != 0x444455555555ULL || arg19 != 77.88f) {
        printf("test_native_args1 validation failed\n");
        test_failed = true;
    }
}

static void
test_native_args2(WASMModuleInstance *module_inst, uint64_t arg1, float arg2,
                  double arg3, int arg4, int64_t arg5, int64_t arg6, int arg7,
                  double arg8, float arg9, int arg10, double arg11, float arg12,
                  int64_t arg13, uint64_t arg14, float arg15, double arg16,
                  int64_t arg17, uint64_t arg18, float arg19)
{
    printf("##test_native_args2 result:\n");
    printf("arg1: 0x%X%08X, arg2: %f, arg3: %f\n", (int32)(arg1 >> 32),
           (int32)arg1, (double)arg2, arg3);
    printf("arg4: 0x%X, arg5: 0x%X%08X, arg6: 0x%X%08X, arg7: 0x%X\n", arg4,
           (int32)(arg5 >> 32), (int32)arg5, (int32)(arg6 >> 32), (int32)arg6,
           arg7);
    printf("arg8: %f, arg9: %f, arg10: 0x%X, arg11: %f\n", arg8, (double)arg9,
           arg10, arg11);
    printf("arg12: %f, arg13: 0x%X%08X, arg14: 0x%X%08X, arg15: %f\n",
           (double)arg12, (int32)(arg13 >> 32), (int32)arg13,
           (int32)(arg14 >> 32), (int32)arg14, (double)arg15);
    printf("arg16: %f, arg17: 0x%X%08X, arg18: 0x%X%08X, arg19: %f\n", arg16,
           (int32)(arg17 >> 32), (int32)arg17, (int32)(arg18 >> 32),
           (int32)arg18, (double)arg19);

    if (arg1 != 0xFFFFFFFF87654321ULL || arg2 != 1234.5678f
        || arg3 != 567890.1234 || arg4 != 0x11111111
        || (uint64)arg5 != 0xAAAAAAAABBBBBBBBULL || arg6 != 0x7788888899LL
        || arg7 != 0x3456 || arg8 != 8888.7777 || arg9 != 7777.8888f
        || arg10 != 0x66666 || arg11 != 999999.88888 || arg12 != 555555.22f
        || arg13 != 0xBBBBBAAAAAAAALL || arg14 != 0x3333AAAABBBBULL
        || arg15 != 88.77f || arg16 != 9999.01234 || arg17 != 0x1111122222222LL
        || arg18 != 0x444455555555ULL || arg19 != 77.88f) {
        printf("test_native_args2 validation failed\n");
        test_failed = true;
    }
}

static int32
test_return_i32(WASMModuleInstance *module_inst)
{
    return 0x12345678;
}

static int64
test_return_i64(WASMModuleInstance *module_inst)
{
    return 0x12345678ABCDEFFFll;
}

static float32
test_return_f32(WASMModuleInstance *module_inst)
{
    return 1234.5678f;
}

static float64
test_return_f64(WASMModuleInstance *module_inst)
{
    return 87654321.12345678;
}

static void
test_store_i64(uint32 *addr, uint64 value)
{
    union {
        uint64 val;
        uint32 parts[2];
    } u;

    u.val = value;
    addr[0] = u.parts[0];
    addr[1] = u.parts[1];
}

static void
test_store_f32(uint32 *addr, float32 value)
{
    union {
        float32 val;
        uint32 part;
    } u;

    u.val = value;
    addr[0] = u.part;
}

static void
test_store_f64(uint32 *addr, float64 value)
{
    union {
        float64 val;
        uint32 parts[2];
    } u;

    u.val = value;
    addr[0] = u.parts[0];
    addr[1] = u.parts[1];
}

static int64
test_load_i64(const uint32 *addr)
{
    union {
        int64 val;
        uint32 parts[2];
    } u;

    u.parts[0] = addr[0];
    u.parts[1] = addr[1];
    return u.val;
}

static float32
test_load_f32(const uint32 *addr)
{
    union {
        float32 val;
        uint32 part;
    } u;

    u.part = addr[0];
    return u.val;
}

static float64
test_load_f64(const uint32 *addr)
{
    union {
        float64 val;
        uint32 parts[2];
    } u;

    u.parts[0] = addr[0];
    u.parts[1] = addr[1];
    return u.val;
}

#define I32 VALUE_TYPE_I32
#define I64 VALUE_TYPE_I64
#define F32 VALUE_TYPE_F32
#define F64 VALUE_TYPE_F64
#define V128_TYPE VALUE_TYPE_V128

typedef struct WASMTypeTest {
    uint16 param_count;
    /* only one result is supported currently */
    uint16 result_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 ref_count;
    /* types of params and results */
    uint8 types[128];
} WASMTypeTest;

int
test_invoke_native(void)
{
    uint32 argv[128], *p = argv;
    int64 result_i64;
    float32 result_f32;
    float64 result_f64;
    WASMTypeTest func_type1 = { 20, 0, 0, 0, 0, { I32, I64, F32, F64, I32,
                                                  I64, I64, I32, F64, F32,
                                                  I32, F64, F32, I64, I64,
                                                  F32, F64, I64, I64, F32 } };
    WASMTypeTest func_type2 = { 19,
                                0,
                                0,
                                0,
                                0,
                                { I64, F32, F64, I32, I64, I64, I32, F64, F32,
                                  I32, F64, F32, I64, I64, F32, F64, I64, I64,
                                  F32 } };
    WASMTypeTest func_type_i32 = { 0, 1, 0, 0, 0, { I32 } };
    WASMTypeTest func_type_i64 = { 0, 1, 0, 0, 0, { I64 } };
    WASMTypeTest func_type_f32 = { 0, 1, 0, 0, 0, { F32 } };
    WASMTypeTest func_type_f64 = { 0, 1, 0, 0, 0, { F64 } };
#if WASM_ENABLE_SIMD != 0
    WASMTypeTest func_type_v128 = {
        9,
        1,
        36,
        4,
        0,
        { V128_TYPE, V128_TYPE, V128_TYPE, V128_TYPE, V128_TYPE, V128_TYPE,
          V128_TYPE, V128_TYPE, V128_TYPE, V128_TYPE }
    };
    uint32 v128_argv[9 * 4];
    uint32 i, j;
#endif
    WASMModuleInstance module_inst = { 0 };
    WASMExecEnv exec_env = { 0 };

    test_failed = false;

    module_inst.module_type = Wasm_Module_Bytecode;
    exec_env.module_inst = (WASMModuleInstanceCommon *)&module_inst;

    *p++ = 0x12345678;
    test_store_i64(p, 0xFFFFFFFF87654321ULL);
    p += 2;
    test_store_f32(p++, 1234.5678f);
    test_store_f64(p, 567890.1234);
    p += 2;

    *p++ = 0x11111111;
    test_store_i64(p, 0xAAAAAAAABBBBBBBBULL);
    p += 2;
    test_store_i64(p, 0x7788888899ULL);
    p += 2;
    *p++ = 0x3456;

    test_store_f64(p, 8888.7777);
    p += 2;
    test_store_f32(p++, 7777.8888f);
    *p++ = 0x66666;
    test_store_f64(p, 999999.88888);
    p += 2;

    test_store_f32(p++, 555555.22f);
    test_store_i64(p, 0xBBBBBAAAAAAAAULL);
    p += 2;
    test_store_i64(p, 0x3333AAAABBBBULL);
    p += 2;
    test_store_f32(p++, 88.77f);

    test_store_f64(p, 9999.01234);
    p += 2;
    test_store_i64(p, 0x1111122222222ULL);
    p += 2;
    test_store_i64(p, 0x444455555555ULL);
    p += 2;
    test_store_f32(p++, 77.88f);

    if (!wasm_runtime_invoke_native(&exec_env, test_native_args1,
                                    (WASMType *)&func_type1, NULL, NULL, argv,
                                    p - argv, argv)) {
        printf("test_native_args1 invocation failed\n");
        test_failed = true;
    }
    printf("\n");

    if (!wasm_runtime_invoke_native(&exec_env, test_native_args2,
                                    (WASMType *)&func_type2, NULL, NULL,
                                    argv + 1, p - argv - 1, argv)) {
        printf("test_native_args2 invocation failed\n");
        test_failed = true;
    }
    printf("\n");

    if (!wasm_runtime_invoke_native(&exec_env, test_return_i32,
                                    (WASMType *)&func_type_i32, NULL, NULL,
                                    NULL, 0, argv)) {
        printf("test_return_i32 invocation failed\n");
        test_failed = true;
    }
    printf("test_return_i32: 0x%X\n\n", argv[0]);
    if (argv[0] != 0x12345678)
        test_failed = true;

    if (!wasm_runtime_invoke_native(&exec_env, test_return_i64,
                                    (WASMType *)&func_type_i64, NULL, NULL,
                                    NULL, 0, argv)) {
        printf("test_return_i64 invocation failed\n");
        test_failed = true;
    }
    result_i64 = test_load_i64(argv);
    printf("test_return_i64: 0x%X%08X\n\n", (int32)(result_i64 >> 32),
           (int32)result_i64);
    if (result_i64 != 0x12345678ABCDEFFFLL)
        test_failed = true;

    if (!wasm_runtime_invoke_native(&exec_env, test_return_f32,
                                    (WASMType *)&func_type_f32, NULL, NULL,
                                    NULL, 0, argv)) {
        printf("test_return_f32 invocation failed\n");
        test_failed = true;
    }
    result_f32 = test_load_f32(argv);
    printf("test_return_f32: %f\n\n", (double)result_f32);
    if (result_f32 != 1234.5678f)
        test_failed = true;

    if (!wasm_runtime_invoke_native(&exec_env, test_return_f64,
                                    (WASMType *)&func_type_f64, NULL, NULL,
                                    NULL, 0, argv)) {
        printf("test_return_f64 invocation failed\n");
        test_failed = true;
    }
    result_f64 = test_load_f64(argv);
    printf("test_return_f64: %f\n\n", result_f64);
    if (result_f64 != 87654321.12345678)
        test_failed = true;

#if WASM_ENABLE_SIMD != 0
    for (i = 0; i < 9; i++) {
        for (j = 0; j < 4; j++)
            v128_argv[(i * 4) + j] = ((i + 1) * 0x100) + j;
    }

    if (!wasm_runtime_invoke_native(&exec_env, test_native_v128,
                                    (WASMType *)&func_type_v128, NULL, NULL,
                                    v128_argv, 9 * 4, v128_argv)) {
        printf("test_native_v128 invocation failed\n");
        test_failed = true;
    }
    for (i = 0; i < 4; i++) {
        if (v128_argv[i] != 0x900 + i) {
            printf("test_native_v128 return validation failed\n");
            test_failed = true;
            break;
        }
    }
#endif

    return test_failed ? 1 : 0;
}

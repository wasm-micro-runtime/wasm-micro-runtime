/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_safe_arith.h"

#include <limits>

/*
 * The oracle uses 128-bit integers when the compiler provides them. MSVC has
 * no __int128, so the 64-bit oracle tests are skipped there.
 */
#if defined(__SIZEOF_INT128__)
#define BH_SAFE_ARITH_ORACLE_64 1
typedef unsigned __int128 wide_u;
typedef __int128 wide_s;
#else
typedef uint64 wide_u;
typedef int64 wide_s;
#endif

static_assert(sizeof(uint8) == 1, "uint8 must be 8 bits");
static_assert(sizeof(uint16) == 2, "uint16 must be 16 bits");
static_assert(sizeof(uint32) == 4, "uint32 must be 32 bits");
static_assert(sizeof(uint64) == 8, "uint64 must be 64 bits");
static_assert(sizeof(int8) == 1, "int8 must be 8 bits");
static_assert(sizeof(int16) == 2, "int16 must be 16 bits");
static_assert(sizeof(int32) == 4, "int32 must be 32 bits");
static_assert(sizeof(int64) == 8, "int64 must be 64 bits");

/* Dispatch to the public and portable functions of one type */
template<typename T>
struct safe_ops;

#define SAFE_OPS(T, SIGN, W)                                             \
    template<>                                                           \
    struct safe_ops<T> {                                                 \
        static bool add(T a, T b, T *res)                                \
        {                                                                \
            return bh_safe_add_##SIGN##W##_overflow(a, b, res);          \
        }                                                                \
        static bool sub(T a, T b, T *res)                                \
        {                                                                \
            return bh_safe_sub_##SIGN##W##_overflow(a, b, res);          \
        }                                                                \
        static bool mul(T a, T b, T *res)                                \
        {                                                                \
            return bh_safe_mul_##SIGN##W##_overflow(a, b, res);          \
        }                                                                \
        static bool add_portable(T a, T b, T *res)                       \
        {                                                                \
            return bh_safe_add_##SIGN##W##_overflow_portable(a, b, res); \
        }                                                                \
        static bool sub_portable(T a, T b, T *res)                       \
        {                                                                \
            return bh_safe_sub_##SIGN##W##_overflow_portable(a, b, res); \
        }                                                                \
        static bool mul_portable(T a, T b, T *res)                       \
        {                                                                \
            return bh_safe_mul_##SIGN##W##_overflow_portable(a, b, res); \
        }                                                                \
    };

SAFE_OPS(uint8, u, 8)
SAFE_OPS(uint16, u, 16)
SAFE_OPS(uint32, u, 32)
SAFE_OPS(uint64, u, 64)
SAFE_OPS(int8, s, 8)
SAFE_OPS(int16, s, 16)
SAFE_OPS(int32, s, 32)
SAFE_OPS(int64, s, 64)

/* Check one result of a public function against the expected result */
template<typename T>
static void
expect_op(const char *op, T a, T b, bool (*fn)(T, T, T *),
          bool (*fn_portable)(T, T, T *), bool expect_overflow, T expect_res)
{
    T res = 0;
    T res_portable = 0;
    bool overflow = fn(a, b, &res);
    bool overflow_portable = fn_portable(a, b, &res_portable);

    EXPECT_EQ(overflow, expect_overflow) << op << " " << +a << " " << +b;
    EXPECT_EQ(res, expect_res) << op << " " << +a << " " << +b;
    EXPECT_EQ(overflow_portable, overflow) << op << " " << +a << " " << +b;
    EXPECT_EQ(res_portable, res) << op << " " << +a << " " << +b;
}

/* Check one unsigned operand pair against the exact result */
template<typename T>
static void
check_unsigned_pair(T a, T b)
{
    wide_u wa = a;
    wide_u wb = b;
    wide_u max = std::numeric_limits<T>::max();

    expect_op("add", a, b, safe_ops<T>::add, safe_ops<T>::add_portable,
              wa + wb > max, (T)(wa + wb));
    expect_op("sub", a, b, safe_ops<T>::sub, safe_ops<T>::sub_portable, wb > wa,
              (T)(wa - wb));
    expect_op("mul", a, b, safe_ops<T>::mul, safe_ops<T>::mul_portable,
              wa * wb > max, (T)(wa * wb));
}

/* Check one signed operand pair against the exact result */
template<typename T>
static void
check_signed_pair(T a, T b)
{
    wide_s wa = a;
    wide_s wb = b;
    wide_s min = std::numeric_limits<T>::min();
    wide_s max = std::numeric_limits<T>::max();

    expect_op("add", a, b, safe_ops<T>::add, safe_ops<T>::add_portable,
              wa + wb > max || wa + wb < min, (T)(wa + wb));
    expect_op("sub", a, b, safe_ops<T>::sub, safe_ops<T>::sub_portable,
              wa - wb > max || wa - wb < min, (T)(wa - wb));
    expect_op("mul", a, b, safe_ops<T>::mul, safe_ops<T>::mul_portable,
              wa * wb > max || wa * wb < min, (T)(wa * wb));
}

template<typename T>
static void
run_unsigned_grid()
{
    T max = std::numeric_limits<T>::max();
    T values[] = { 0, 1, 2, (T)(max / 2), (T)(max - 1), max };
    for (T a : values) {
        for (T b : values) {
            check_unsigned_pair<T>(a, b);
        }
    }
}

template<typename T>
static void
run_signed_grid()
{
    T min = std::numeric_limits<T>::min();
    T max = std::numeric_limits<T>::max();
    T values[] = { min, (T)(min + 1), (T)-1, 0, 1, (T)(max - 1), max };
    for (T a : values) {
        for (T b : values) {
            check_signed_pair<T>(a, b);
        }
    }
}

class bh_safe_arith_test_suite : public testing::Test
{
  protected:
    virtual void SetUp() {}
    virtual void TearDown() {}
};

TEST_F(bh_safe_arith_test_suite, unsigned_grid_8)
{
    run_unsigned_grid<uint8>();
}

TEST_F(bh_safe_arith_test_suite, unsigned_grid_16)
{
    run_unsigned_grid<uint16>();
}

TEST_F(bh_safe_arith_test_suite, unsigned_grid_32)
{
    run_unsigned_grid<uint32>();
}

TEST_F(bh_safe_arith_test_suite, unsigned_grid_64)
{
#if defined(BH_SAFE_ARITH_ORACLE_64)
    run_unsigned_grid<uint64>();
#else
    GTEST_SKIP() << "no 128-bit oracle on this compiler";
#endif
}

TEST_F(bh_safe_arith_test_suite, signed_grid_8)
{
    run_signed_grid<int8>();
}

TEST_F(bh_safe_arith_test_suite, signed_grid_16)
{
    run_signed_grid<int16>();
}

TEST_F(bh_safe_arith_test_suite, signed_grid_32)
{
    run_signed_grid<int32>();
}

TEST_F(bh_safe_arith_test_suite, signed_grid_64)
{
#if defined(BH_SAFE_ARITH_ORACLE_64)
    run_signed_grid<int64>();
#else
    GTEST_SKIP() << "no 128-bit oracle on this compiler";
#endif
}

TEST_F(bh_safe_arith_test_suite, signed_special_values)
{
    int8 r8;
    int16 r16;
    int32 r32;
    int64 r64;

    /* MIN * -1 overflows, and wraps back to MIN */
    EXPECT_TRUE(bh_safe_mul_s8_overflow(INT8_MIN, -1, &r8));
    EXPECT_EQ(r8, INT8_MIN);
    EXPECT_TRUE(bh_safe_mul_s16_overflow(INT16_MIN, -1, &r16));
    EXPECT_EQ(r16, INT16_MIN);
    EXPECT_TRUE(bh_safe_mul_s32_overflow(INT32_MIN, -1, &r32));
    EXPECT_EQ(r32, INT32_MIN);
    EXPECT_TRUE(bh_safe_mul_s64_overflow(INT64_MIN, -1, &r64));
    EXPECT_EQ(r64, INT64_MIN);

    /* MIN - 1 and MAX + 1 overflow */
    EXPECT_TRUE(bh_safe_sub_s8_overflow(INT8_MIN, 1, &r8));
    EXPECT_EQ(r8, INT8_MAX);
    EXPECT_TRUE(bh_safe_sub_s64_overflow(INT64_MIN, 1, &r64));
    EXPECT_EQ(r64, INT64_MAX);
    EXPECT_TRUE(bh_safe_add_s8_overflow(INT8_MAX, 1, &r8));
    EXPECT_EQ(r8, INT8_MIN);
    EXPECT_TRUE(bh_safe_add_s64_overflow(INT64_MAX, 1, &r64));
    EXPECT_EQ(r64, INT64_MIN);

    /* -1 + 1 does not overflow */
    EXPECT_FALSE(bh_safe_add_s8_overflow(-1, 1, &r8));
    EXPECT_EQ(r8, 0);
    EXPECT_FALSE(bh_safe_add_s64_overflow(-1, 1, &r64));
    EXPECT_EQ(r64, 0);
}

TEST_F(bh_safe_arith_test_suite, memory64_regression_values)
{
    uint64 res;

    /* memory64 constant-fold load: 0xFFFFFFFFFFFFFFF0 + 0x1F wraps to 0xF */
    EXPECT_TRUE(bh_safe_add_u64_overflow(0xFFFFFFFFFFFFFFF0ULL, 0x1FULL, &res));
    EXPECT_EQ(res, 0xFULL);

    /* memory64 bulk copy: 0xFFFFFFFFFFFFFE00 + 0x200 wraps to 0 */
    EXPECT_TRUE(
        bh_safe_add_u64_overflow(0xFFFFFFFFFFFFFE00ULL, 0x200ULL, &res));
    EXPECT_EQ(res, 0ULL);

    /* Boundary: 0xFFFFFFFFFFFFFFF0 + 0x0F is exactly UINT64_MAX */
    EXPECT_FALSE(
        bh_safe_add_u64_overflow(0xFFFFFFFFFFFFFFF0ULL, 0x0FULL, &res));
    EXPECT_EQ(res, 0xFFFFFFFFFFFFFFFFULL);
}

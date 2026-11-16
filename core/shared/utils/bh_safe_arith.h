/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _BH_SAFE_ARITH_H
#define _BH_SAFE_ARITH_H

#include "bh_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Overflow-checked integer arithmetic.
 *
 * bh_safe_<op>_<sign><width>_overflow(a, b, res) computes a <op> b in
 * <width> bits, where <op> is add, sub or mul, <sign> is u or s, and
 * <width> is 8, 16, 32 or 64. It returns true if the mathematical result
 * does not fit in the type. *res always receives the wrapped result, the
 * same as GCC's __builtin_<op>_overflow.
 *
 * Only fixed-width types are used, because int/long/long long change width
 * across platforms. Signed overflow is never computed directly, since that
 * is undefined behavior.
 *
 * The _portable variants are always compiled. The public functions call the
 * compiler builtins on GCC and Clang, and the portable variants elsewhere.
 *
 * TODO: MSVC currently uses the portable variants. Replace the unsigned
 * paths with _addcarry_u{32,64}, _subborrow_u{32,64} and _umul128 intrinsics.
 * Not verified on MSVC yet.
 */

/* Unsigned portable implementations. Overflow is wrapped around */

/* res = a + b - 2^N */
#define BH_SAFE_ADD_U_PORTABLE(W)                            \
    static inline bool bh_safe_add_u##W##_overflow_portable( \
        uint##W a, uint##W b, uint##W *res)                  \
    {                                                        \
        *res = (uint##W)(a + b);                             \
        return *res < a;                                     \
    }

#define BH_SAFE_SUB_U_PORTABLE(W)                            \
    static inline bool bh_safe_sub_u##W##_overflow_portable( \
        uint##W a, uint##W b, uint##W *res)                  \
    {                                                        \
        *res = (uint##W)(a - b);                             \
        return b > a;                                        \
    }

#define BH_SAFE_MUL_U_NARROW_PORTABLE(W)                     \
    static inline bool bh_safe_mul_u##W##_overflow_portable( \
        uint##W a, uint##W b, uint##W *res)                  \
    {                                                        \
        uint64 r = (uint64)a * b;                            \
        *res = (uint##W)r;                                   \
        return r > UINT##W##_MAX;                            \
    }

#define BH_SAFE_MUL_U64_PORTABLE                                             \
    static inline bool bh_safe_mul_u64_overflow_portable(uint64 a, uint64 b, \
                                                         uint64 *res)        \
    {                                                                        \
        *res = a * b;                                                        \
        return a != 0 && b > UINT64_MAX / a;                                 \
    }

BH_SAFE_ADD_U_PORTABLE(8)
BH_SAFE_ADD_U_PORTABLE(16)
BH_SAFE_ADD_U_PORTABLE(32)
BH_SAFE_ADD_U_PORTABLE(64)
BH_SAFE_SUB_U_PORTABLE(8)
BH_SAFE_SUB_U_PORTABLE(16)
BH_SAFE_SUB_U_PORTABLE(32)
BH_SAFE_SUB_U_PORTABLE(64)
BH_SAFE_MUL_U_NARROW_PORTABLE(8)
BH_SAFE_MUL_U_NARROW_PORTABLE(16)
BH_SAFE_MUL_U_NARROW_PORTABLE(32)
BH_SAFE_MUL_U64_PORTABLE

/* Signed portable implementations. Overflow is undefined behavior in C */

#define BH_SAFE_ADD_S_PORTABLE(W)                            \
    static inline bool bh_safe_add_s##W##_overflow_portable( \
        int##W a, int##W b, int##W *res)                     \
    {                                                        \
        bool overflow;                                       \
        if (b > 0) {                                         \
            overflow = a > INT##W##_MAX - b;                 \
        }                                                    \
        else {                                               \
            overflow = a < INT##W##_MIN - b;                 \
        }                                                    \
        *res = (int##W)((uint##W)a + (uint##W)b);            \
        return overflow;                                     \
    }

#define BH_SAFE_SUB_S_PORTABLE(W)                            \
    static inline bool bh_safe_sub_s##W##_overflow_portable( \
        int##W a, int##W b, int##W *res)                     \
    {                                                        \
        bool overflow;                                       \
        if (b < 0) {                                         \
            overflow = a > INT##W##_MAX + b;                 \
        }                                                    \
        else {                                               \
            overflow = a < INT##W##_MIN + b;                 \
        }                                                    \
        *res = (int##W)((uint##W)a - (uint##W)b);            \
        return overflow;                                     \
    }

/*
 * Multiply the magnitudes as unsigned values, then check the sign-adjusted
 * range. The magnitude of INT##W##_MIN is 2^(W-1), so the negative limit is
 * INT##W##_MAX + 1.
 */
#define BH_SAFE_MUL_S_PORTABLE(W)                                       \
    static inline bool bh_safe_mul_s##W##_overflow_portable(            \
        int##W a, int##W b, int##W *res)                                \
    {                                                                   \
        bool negative = (a < 0) != (b < 0);                             \
        uint64 ua = a < 0 ? (uint64)0 - (uint64)a : (uint64)a;          \
        uint64 ub = b < 0 ? (uint64)0 - (uint64)b : (uint64)b;          \
        uint64 mag, limit;                                              \
        bool mag_overflow;                                              \
                                                                        \
        mag_overflow = bh_safe_mul_u64_overflow_portable(ua, ub, &mag); \
        limit = (uint64)INT##W##_MAX;                                   \
        if (negative) {                                                 \
            limit += 1;                                                 \
        }                                                               \
        *res = (int##W)(negative ? (uint64)0 - mag : mag);              \
        return mag_overflow || mag > limit;                             \
    }

BH_SAFE_ADD_S_PORTABLE(8)
BH_SAFE_ADD_S_PORTABLE(16)
BH_SAFE_ADD_S_PORTABLE(32)
BH_SAFE_ADD_S_PORTABLE(64)
BH_SAFE_SUB_S_PORTABLE(8)
BH_SAFE_SUB_S_PORTABLE(16)
BH_SAFE_SUB_S_PORTABLE(32)
BH_SAFE_SUB_S_PORTABLE(64)
BH_SAFE_MUL_S_PORTABLE(8)
BH_SAFE_MUL_S_PORTABLE(16)
BH_SAFE_MUL_S_PORTABLE(32)
BH_SAFE_MUL_S_PORTABLE(64)

/* Public functions */

#if defined(__GNUC__) || defined(__clang__)
#define BH_SAFE_DEFINE(OP, SIGN, W, TYPE)                                  \
    static inline bool bh_safe_##OP##_##SIGN##W##_overflow(TYPE a, TYPE b, \
                                                           TYPE *res)      \
    {                                                                      \
        return __builtin_##OP##_overflow(a, b, res);                       \
    }
#else
#define BH_SAFE_DEFINE(OP, SIGN, W, TYPE)                                  \
    static inline bool bh_safe_##OP##_##SIGN##W##_overflow(TYPE a, TYPE b, \
                                                           TYPE *res)      \
    {                                                                      \
        return bh_safe_##OP##_##SIGN##W##_overflow_portable(a, b, res);    \
    }
#endif

BH_SAFE_DEFINE(add, u, 8, uint8)
BH_SAFE_DEFINE(add, u, 16, uint16)
BH_SAFE_DEFINE(add, u, 32, uint32)
BH_SAFE_DEFINE(add, u, 64, uint64)
BH_SAFE_DEFINE(sub, u, 8, uint8)
BH_SAFE_DEFINE(sub, u, 16, uint16)
BH_SAFE_DEFINE(sub, u, 32, uint32)
BH_SAFE_DEFINE(sub, u, 64, uint64)
BH_SAFE_DEFINE(mul, u, 8, uint8)
BH_SAFE_DEFINE(mul, u, 16, uint16)
BH_SAFE_DEFINE(mul, u, 32, uint32)
BH_SAFE_DEFINE(mul, u, 64, uint64)
BH_SAFE_DEFINE(add, s, 8, int8)
BH_SAFE_DEFINE(add, s, 16, int16)
BH_SAFE_DEFINE(add, s, 32, int32)
BH_SAFE_DEFINE(add, s, 64, int64)
BH_SAFE_DEFINE(sub, s, 8, int8)
BH_SAFE_DEFINE(sub, s, 16, int16)
BH_SAFE_DEFINE(sub, s, 32, int32)
BH_SAFE_DEFINE(sub, s, 64, int64)
BH_SAFE_DEFINE(mul, s, 8, int8)
BH_SAFE_DEFINE(mul, s, 16, int16)
BH_SAFE_DEFINE(mul, s, 32, int32)
BH_SAFE_DEFINE(mul, s, 64, int64)

#undef BH_SAFE_DEFINE
#undef BH_SAFE_ADD_U_PORTABLE
#undef BH_SAFE_SUB_U_PORTABLE
#undef BH_SAFE_MUL_U_NARROW_PORTABLE
#undef BH_SAFE_MUL_U64_PORTABLE
#undef BH_SAFE_ADD_S_PORTABLE
#undef BH_SAFE_SUB_S_PORTABLE
#undef BH_SAFE_MUL_S_PORTABLE

#ifdef __cplusplus
}
#endif

#endif /* end of _BH_SAFE_ARITH_H */

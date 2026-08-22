/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _WASM_IEEE754_H
#define _WASM_IEEE754_H

#ifdef __cplusplus
extern "C" {
#endif

union ieee754_float {
    float f;

    /* This is the IEEE 754 single-precision format.  */
    union {
        struct {
            unsigned int negative : 1;
            unsigned int exponent : 8;
            unsigned int mantissa : 23;
        } ieee_big_endian;
        struct {
            unsigned int mantissa : 23;
            unsigned int exponent : 8;
            unsigned int negative : 1;
        } ieee_little_endian;
    } ieee;
};

union ieee754_double {
    double d;

    /* This is the IEEE 754 double-precision format.  */
    union {
        struct {
            unsigned int negative : 1;
            unsigned int exponent : 11;
            /* Together these comprise the mantissa.  */
            unsigned int mantissa0 : 20;
            unsigned int mantissa1 : 32;
        } ieee_big_endian;

        struct {
            /* Together these comprise the mantissa.  */
            unsigned int mantissa1 : 32;
            unsigned int mantissa0 : 20;
            unsigned int exponent : 11;
            unsigned int negative : 1;
        } ieee_little_endian;
    } ieee;
};

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1) /* NOLINT */

#ifdef __cplusplus
}
#endif

#endif /* end of _WASM_IEEE754_H */
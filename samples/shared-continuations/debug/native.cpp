/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
extern "C" int
native_rust(int);
extern "C" __attribute__((noinline)) int
native_cpp(int value)
{
    volatile int result = native_rust(value);
    return result + 1;
}

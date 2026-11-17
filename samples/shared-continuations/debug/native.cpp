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

#ifdef DIAGNOSTIC_ERRORS
#include <stdexcept>
extern "C" __attribute__((noinline)) int
native_cpp_error(void)
{
    try {
        throw std::runtime_error("native C++ operation failed");
    } catch (const std::exception &) {
        return -11;
    }
}
#endif

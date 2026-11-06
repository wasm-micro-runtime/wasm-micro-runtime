/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
__attribute__((import_module("example:native"), import_name("call"))) int
native_call(int);
__attribute__((noinline)) int
guest_leaf(int value)
{
    if (value == 0)
        __builtin_trap();
    if (value > 100)
        return guest_leaf(value - 1) + 1;
    int result = native_call(value);
    if (result < 0)
        __builtin_trap();
    return result + 1;
}
__attribute__((noinline)) int
guest_middle(int value)
{
    return guest_leaf(value) + 1;
}
__attribute__((export_name("run"))) int
guest_root(int value)
{
    return guest_middle(value) + 1;
}

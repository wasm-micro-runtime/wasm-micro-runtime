/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
extern int
native_checkpoint(int);
__attribute__((noinline)) int
native_bridge(int value)
{
    return native_checkpoint(value) + 1;
}

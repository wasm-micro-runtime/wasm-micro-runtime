/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Three noinline functions producing a 3-frame WASM call stack at the
   trap (plus app_main). addr2line.py must report all four frames with
   distinct indices. */

__attribute__((noinline)) void
bot(void)
{
    __builtin_trap();
}

__attribute__((noinline)) void
mid(void)
{
    bot();
}

__attribute__((noinline)) void
top(void)
{
    mid();
}

void
app_main(void)
{
    top();
}

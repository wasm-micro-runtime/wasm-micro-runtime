/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Baseline test: single function, single trap. The address of
   __builtin_trap() must symbolicate back to file=simple.c, function=crash. */

void
crash(void)
{
    __builtin_trap();
}

void
app_main(void)
{
    crash();
}

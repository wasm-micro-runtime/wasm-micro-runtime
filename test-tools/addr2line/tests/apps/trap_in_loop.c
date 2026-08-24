/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Trap inside a for-loop body. The trap address is neither at function
   entry nor at function exit; it's mid-function. addr2line.py must
   resolve it to the trap line, not the function declaration line. */

static volatile int sink;

void
crash(void)
{
    for (int i = 0; i < 10; i++) {
        sink = i;
        if (i == 7) {
            __builtin_trap();
        }
    }
}

void
app_main(void)
{
    crash();
}

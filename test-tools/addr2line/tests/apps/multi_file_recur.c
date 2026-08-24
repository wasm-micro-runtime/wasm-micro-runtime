/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

static volatile int sink;

int
recur(int depth)
{
    /* Allocate stack space and use it so the call isn't trivially
       optimized away. Trap inside the loop body. */
    volatile char buf[64];
    buf[0] = (char)depth;
    buf[63] = (char)(depth >> 8);
    sink = buf[0] + buf[63];
    if (depth == 100) {
        __builtin_trap();
    }
    /* Use the return value after the call to prevent tail-call
       conversion to a loop under -Oz -flto. */
    int r = recur(depth + 1);
    return r + buf[0];
}

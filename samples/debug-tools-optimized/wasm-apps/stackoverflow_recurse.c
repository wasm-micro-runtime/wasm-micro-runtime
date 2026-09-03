/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

static volatile int sink;

int
recurse(int depth)
{
    /* Allocate stack space each frame to accelerate overflow */
    volatile char buf[128];
    buf[0] = (char)depth;
    buf[127] = (char)(depth >> 8);
    sink = buf[0] + buf[127];
    if (depth == 10000) {
        __builtin_trap();
    }
    /* TODO: Use return value after the call to prevent tail-call optimization,
     * it will stack overflow */
    int r = recurse(depth + 1);
    return r + buf[0];
    /* TODO: will optimize to loop, and it will trap when reach certain level */
    // return recurse(depth + 1);
}

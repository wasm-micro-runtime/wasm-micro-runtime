/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Forced-inline helper containing the trap. The trap address falls
   inside the inlined region, so addr2line.py must report both
   `inner` and `outer` under a single runtime frame, with `inner`
   carrying the `(inlined into outer)` suffix. */

__attribute__((always_inline)) static inline void
inner(void)
{
    __builtin_trap();
}

void
outer(void)
{
    inner();
}

void
app_main(void)
{
    outer();
}

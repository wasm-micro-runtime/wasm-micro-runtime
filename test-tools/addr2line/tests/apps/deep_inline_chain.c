/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Four nested always_inline helpers. The trap inside level4 must
   produce a 4-deep inline chain in the output, with consistent
   indentation across all four frames. */

__attribute__((always_inline)) static inline void
level4(void)
{
    __builtin_trap();
}
__attribute__((always_inline)) static inline void
level3(void)
{
    level4();
}
__attribute__((always_inline)) static inline void
level2(void)
{
    level3();
}
__attribute__((always_inline)) static inline void
level1(void)
{
    level2();
}

void
app_main(void)
{
    level1();
}

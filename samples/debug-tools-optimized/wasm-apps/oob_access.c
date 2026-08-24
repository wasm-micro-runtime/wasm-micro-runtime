/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

void
do_bad_access(int offset)
{
    volatile int *p = (volatile int *)0;
    /* Write to an address way beyond linear memory to trigger OOB trap */
    p[offset] = 0xDEAD;
}

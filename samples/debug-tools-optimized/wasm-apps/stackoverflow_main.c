/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Recursive stack overflow for coredump debug demo.
   Multi-file: app_main (this file) -> recurse (stackoverflow_recurse.c) */

int
recurse(int depth);

void
app_main(void)
{
    recurse(0);
}

/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Deliberate out-of-bounds memory access for coredump debug demo.
   Multi-file: app_main -> trigger_oob (this file) -> do_bad_access
   (oob_access.c) Under -Oz, these get inlined into app_main. The debug
   companion retains DWARF inline info so the full chain can be recovered
   offline. */

void
do_bad_access(int offset);

void
trigger_oob(void)
{
    /* 0x7FFFFFFF is well beyond any WASM linear memory */
    do_bad_access(0x7FFFFFFF);
}

void
app_main(void)
{
    trigger_oob();
}

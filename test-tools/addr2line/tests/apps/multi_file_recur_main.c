/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Multi-file recursion: app_main calls into recur() defined in another
   TU. Compiled with -Oz -g -flto, the linker can inline cross-TU and the
   trap address resolves to a chain that crosses files. This is the
   canonical case that triggers the LLVM symbolizer wasm bug on clang < 22. */

int
recur(int depth);

void
app_main(void)
{
    recur(0);
}

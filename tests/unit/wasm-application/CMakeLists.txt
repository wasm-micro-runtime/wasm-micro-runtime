# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# wasm_application.c behaves differently depending on the feature switches
# (the libc-wasi entry point, memory64, GC ref types), so its cases are
# grouped by feature set: base/ covers the default one, and later variants get
# their own subdirectory.
add_subdirectory(base)

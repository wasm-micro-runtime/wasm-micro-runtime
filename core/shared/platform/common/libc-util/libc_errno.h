/*
 * Copyright (C) 2023 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_ERRNO_H
#define WASI_ERRNO_H

#if !defined(__wasm32__) && !defined(__EMSCRIPTEN__)
#include "platform_wasi_types.h"
#else
#include <wasi/api.h>
#endif

// Converts an errno error code to a WASI error code.
__wasi_errno_t
convert_errno(int error);

#endif /* end of WASI_ERRNO_H */
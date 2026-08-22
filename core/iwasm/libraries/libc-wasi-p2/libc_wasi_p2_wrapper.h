/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef LIBC_WASI_P2_WRAPPER_H
#define LIBC_WASI_P2_WRAPPER_H

#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WASMFuncType WASMFuncType;

typedef struct wasi_p2_module {
    const char *module_name;
    const char *version;
    const NativeSymbol *symbols;
    uint32_t symbol_count;
} wasi_p2_module_t;

uint32_t
get_libc_wasi_p2_export_apis(wasi_p2_module_t **p_libc_wasi_p2_apis);

bool
wasm_native_register_wasi_p2_modules();

bool
wasm_check_wasi_p2_version(const char *required_interface);

void
wasm_native_unregister_wasi_p2_modules();

bool
wasm_native_register_wasi_p2_module(const char *module_name);

void
wasm_native_unregister_wasi_p2_module(const char *module_name);

bool
wasm_native_register_wasi_p2_module_func(const char *module_name,
                                         const char *func_name);

void
wasm_native_unregister_wasi_p2_module_func(const char *module_name,
                                           const char *func_name);

struct WASMFuncType;

#ifdef __cplusplus
}
#endif

#endif /* end of LIBC_WASI_P2_WRAPPER_H */

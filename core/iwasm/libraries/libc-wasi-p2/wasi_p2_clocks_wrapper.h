/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_CLOCKS_WRAPPER_H
#define WASI_P2_CLOCKS_WRAPPER_H

#include "wasm_export.h"
#include "wasi_p2_clocks.h"

#ifdef __cplusplus
extern "C" {
#endif

void
wasi_wall_clock_now_wrapper(wasm_exec_env_t exec_env, uint32_t offset_addr);

void
wasi_wall_clock_resolution_wrapper(wasm_exec_env_t exec_env,
                                   uint32_t offset_addr);

uint64_t
wasi_monotonic_clock_now_wrapper(wasm_exec_env_t exec_env);

uint64_t
wasi_monotonic_clock_resolution_wrapper(wasm_exec_env_t exec_env);

int32_t
wasi_monotonic_clock_subscribe_instant_wrapper(wasm_exec_env_t exec_env,
                                               int64_t when);

int32_t
wasi_monotonic_clock_subscribe_duration_wrapper(wasm_exec_env_t exec_env,
                                                int64_t when);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_CLOCKS_WRAPPER_H */

/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_RANDOM_WRAPPER_H
#define WASI_P2_RANDOM_WRAPPER_H

#include "wasm_export.h"
#include "wasi_p2_random.h"

#ifdef __cplusplus
extern "C" {
#endif

void
wasi_random_get_random_bytes_wrapper(wasm_exec_env_t exec_env, uint64_t len,
                                     uint32_t offset_addr);

uint64_t
wasi_random_get_random_u64_wrapper(wasm_exec_env_t exec_env);

void
wasi_random_get_insecure_random_bytes_wrapper(wasm_exec_env_t exec_env,
                                              uint64_t len,
                                              uint32_t offset_addr);

uint64_t
wasi_random_get_insecure_random_u64_wrapper(wasm_exec_env_t exec_env);

void
wasi_random_insecure_seed_wrapper(wasm_exec_env_t exec_env,
                                  uint32_t offset_addr);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_RANDOM_WRAPPER_H */

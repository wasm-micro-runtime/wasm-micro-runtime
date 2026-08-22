/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_COMMON_H
#define WASI_P2_COMMON_H

#include <stdbool.h>

#include "wasi_p2_types.h"
#include "wasm_export.h"
#include "wasm_component_runtime.h"
#include "component-model/wasm_canonical_abi.h"
#include "component-model/wasm_component_canonical.h"
#include "wasm_component_host_resource.h"
#include "wasi_p2_sockets_wrapper.h"
#include "wasi_p2_filesystem_wrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

wasi_network_error_code_t
errno_to_wasi_network(int err);

const char *
wasi_network_error_code_to_string(wasi_network_error_code_t error_code);

bool
copy_string_to_wasm(wasm_exec_env_t exec_env, int32_t *wasm_offset,
                    const char *str);

int32_t
wasi_p2_write_option_ip_address(wasm_exec_env_t exec_env, bool is_some,
                                const wasi_ip_address_t *ip_address);

char *
copy_wasm_string_to_native(wasm_exec_env_t exec_env,
                           const int32_t *wasm_str_ptr, int32_t wasm_str_len);

wasi_filesystem_error_code_t
errno_to_wasi_filesystem(int err);

const char *
wasi_filesystem_error_code_to_string(wasi_filesystem_error_code_t error_code);

wit_value_t
get_result_error_val(uint32_t error_code);

wit_value_t
get_result_datetime(uint64_t seconds, uint32_t nanoseconds);

wit_value_t
get_datetime(uint64_t seconds, uint32_t nanoseconds);

StringEncoding
wasm_get_string_encoding(WASMExecEnv *exec_env);

bool
close_host_resource_fd(HostResource *hr);

#ifdef __cplusplus
}
#endif

#endif /* end of _WASI_COMMON_H */

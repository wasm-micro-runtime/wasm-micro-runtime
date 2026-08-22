/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_ERROR_H
#define WASI_P2_ERROR_H

#include "wasi_p2_types.h"
#include <stdbool.h>
#include "component-model/wasm_component_host_resource.h"
#include "component-model/wasm_canonical_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WasiErrorResource {
    int32 error_code;  // unix errno
    IOStreamType type; // type of the stream that generated the error
} WasiErrorResource;

uint32_t
wasi_error_new(IOStreamType type, uint32_t code);

const char *
wasi_error_to_debug_string(HostResource *hr_err);

wit_value_t
get_stream_error_val(bool is_closed, uint32_t error_idx);

wit_value_t
get_hr_stream_error_val(HostResource *hr, wasi_stream_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_ERROR_H */

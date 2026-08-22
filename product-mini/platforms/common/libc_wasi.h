/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef LIBC_WASI_H
#define LIBC_WASI_H

#include <stdio.h>
#if WASM_ENABLE_COMPONENT_MODEL != 0
#include "wasm_component.h"
#include "wasm_component_runtime.h"
#endif
#include "bh_platform.h"
#include "wasm_export.h"

typedef struct libc_wasi_options_t {
    uint32 cli : 1;
    uint32 cli_exit_with_code : 1; // unsupported for now
    uint32 common : 1;
    uint32 nn : 1;       // unsupported for now
    uint32 threads : 1;  // unsupported for now
    uint32 http : 1;     // unsupported for now
    uint32 config : 1;   // unsupported for now
    uint32 keyvalue : 1; // unsupported for now
    uint32 listenfd : 1; // unsupported for now
    uint32 tls : 1;      // unsupported for now
    uint32 preview2 : 1; // unsupported for now
    uint32 inherit_network : 1;
    uint32 allow_ip_name_lookup : 1;
    uint32 tcp : 1;
    uint32 udp : 1;
    uint32 network_error_code : 1; // unsupported for now
    uint32 inherit_env : 1;
    uint32 p3 : 1;                          // unsupported for now
    uint32 http_outgoing_body_buffer_chunk; // unsupported for now
    uint32 http_outgoing_body_chunk_size;   // unsupported for now
    uint32 config_var;                      // unsupported for now
    uint32 keyvalue_in_memory_data;         // unsupported for now

} libc_wasi_options_t;

typedef struct libc_wasi_parse_context_t {
    const char *dir_list[8];
    uint32 dir_list_size;
    const char *map_dir_list[8];
    uint32 map_dir_list_size;
    const char *env_list[8];
    uint32 env_list_size;
    const char *addr_pool[8];
    uint32 addr_pool_size;
    const char *ns_lookup_pool[8];
    uint32 ns_lookup_pool_size;
#if WASM_ENABLE_COMPONENT_MODEL != 0
    libc_wasi_options_t wasi_options;
#endif
} libc_wasi_parse_context_t;

typedef enum {
    LIBC_WASI_PARSE_RESULT_OK = 0,
    LIBC_WASI_PARSE_RESULT_NEED_HELP,
    LIBC_WASI_PARSE_RESULT_BAD_PARAM
} libc_wasi_parse_result_t;

#ifdef __cplusplus
extern "C" {
#endif

libc_wasi_parse_result_t
libc_wasi_parse(char *arg, libc_wasi_parse_context_t *ctx);

void
libc_wasi_init(wasm_module_t wasm_module, int argc, char **argv,
               libc_wasi_parse_context_t *ctx);

#if WASM_ENABLE_COMPONENT_MODEL != 0
void
libc_component_wasi_init(WASMComponent *wasm_component, int argc, char **argv,
                         libc_wasi_parse_context_t *ctx);
#endif

void
libc_wasi_set_default_options(libc_wasi_parse_context_t *ctx);

bool
libc_wasi_check_option(const char *arg, libc_wasi_parse_context_t *ctx,
                       const char *option, int len,
                       libc_wasi_parse_result_t *res);

libc_wasi_parse_result_t
libc_wasi_parse_options(const char *arg, libc_wasi_parse_context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif // LIBC_WASI_H

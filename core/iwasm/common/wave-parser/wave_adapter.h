/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAVE_ADAPTER_H
#define WAVE_ADAPTER_H

#include <stdbool.h>
#include <stdint.h>
#include "wasm_canonical_abi.h"

// Structure to hold the parsed invocation result
typedef struct {
    char *func_name;
    wit_value_t args;
    uint32_t arg_count;
} wave_invocation_t;

// Main parsing function
bool
wave_parse_invocation_str(const char *input, wave_invocation_t *out);

// Cleanup function
void
wave_free_invocation_struct(wave_invocation_t *inv);

// Prescan the input string and get just the function name
bool
wave_pop_func_name(const char *input, wave_invocation_t *inv);

// verify and coerce loaded data types and values
bool
wave_coerce_invocation(const WASMComponentInstance *component_inst,
                       wave_invocation_t *inv,
                       WASMComponentParamListInstance *params);
#endif
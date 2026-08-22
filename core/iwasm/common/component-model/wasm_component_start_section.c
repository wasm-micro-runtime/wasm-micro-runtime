/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wasm_loader_common.h"
#include "wasm_runtime_common.h"
#include "wasm_export.h"
#include <stdio.h>

// Section 9: start section
bool
wasm_component_parse_start_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentStartSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len)
{
    if (consumed_len)
        *consumed_len = 0;
    if (!payload || !*payload || !out || payload_len == 0) {
        return false;
    }

    const uint8_t *p = *payload;
    const uint8_t *end = p + payload_len;

    // Initialize outputs
    out->func_idx = 0;
    out->value_args_count = 0;
    out->value_args = NULL;
    out->result = 0;

    uint64_t func_idx = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &func_idx, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size, "Failed to read func idx");
        return false;
    }
    out->func_idx = (uint32_t)func_idx;

    uint64_t args_count = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &args_count, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to read args count");
        return false;
    }
    out->value_args_count = (uint32_t)args_count;

    if (args_count > 0) {
        out->value_args = wasm_runtime_malloc(sizeof(uint32_t) * args_count);
        if (!out->value_args) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for value args");
            return false;
        }

        for (uint64_t i = 0; i < args_count; i++) {
            uint64_t value_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &value_idx, error_buf,
                          error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to read value idx");
                // cleanup
                wasm_runtime_free(out->value_args);
                out->value_args = NULL;
                out->value_args_count = 0;
                return false;
            }
            out->value_args[i] = (uint32_t)value_idx;
        }
    }

    uint64_t result_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &result_leb, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to read result count");
        if (out->value_args) {
            wasm_runtime_free(out->value_args);
            out->value_args = NULL;
            out->value_args_count = 0;
        }
        return false;
    }
    out->result = (uint32_t)result_leb;

    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);
    return true;
}

// Individual section free functions
void
wasm_component_free_start_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.start_section) {
        return;
    }

    WASMComponentStartSection *start_section = section->parsed.start_section;
    if (start_section->value_args) {
        wasm_runtime_free(start_section->value_args);
    }
    wasm_runtime_free(start_section);
    section->parsed.start_section = NULL;
}
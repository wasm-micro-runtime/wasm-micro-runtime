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

// Section 4: component section
bool
wasm_component_parse_component_section(const uint8_t **payload,
                                       uint32_t payload_len, WASMComponent *out,
                                       char *error_buf, uint32_t error_buf_size,
                                       LoadArgs *args, unsigned int depth,
                                       uint32_t *consumed_len)
{
    if (consumed_len)
        *consumed_len = 0;
    if (!payload || !*payload || payload_len == 0 || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    // Check depth limit BEFORE recursive call
    if (depth >= MAX_DEPTH_RECURSION) {
        set_error_buf_ex(
            error_buf, error_buf_size,
            "Max depth of recursion for parsing component reached: %d", depth);
        return false;
    }

    // Increment depth BEFORE recursive call
    unsigned int new_depth = depth + 1;

    // Parse the nested component with incremented depth
    bool status = wasm_component_parse_sections(*payload, payload_len, out,
                                                args, new_depth);
    if (!status) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Could not parse sub component with depth: %d",
                         new_depth);
        return false;
    }

    if (consumed_len)
        *consumed_len = payload_len;
    return true;
}

// Individual section free functions
void
wasm_component_free_component_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.component)
        return;

    // Recursively free nested components
    wasm_component_free(section->parsed.component);
    wasm_runtime_free(section->parsed.component);
    section->parsed.component = NULL;
}
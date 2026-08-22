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
#include "../interpreter/wasm_runtime.h"
#include "wasm_export.h"
#include <stdio.h>

// Section 0: custom section
bool
wasm_component_parse_core_custom_section(const uint8_t **payload,
                                         uint32_t payload_len,
                                         WASMComponentCoreCustomSection *out,
                                         char *error_buf,
                                         uint32_t error_buf_size,
                                         uint32_t *consumed_len)
{
    if (out) {
        // Zero-initialize the output struct
        memset(out, 0, sizeof(WASMComponentCoreCustomSection));
    }
    if (consumed_len)
        *consumed_len = 0;
    if (!payload || !*payload || payload_len == 0 || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;
    const uint8_t *end = *payload + payload_len;
    uint32_t name_len = 0;

    // Check bounds
    if (p >= end) {
        set_error_buf_ex(error_buf, error_buf_size, "unexpected end");
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    // Read name length, validate, and copy name
    uint64_t name_len_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &name_len_leb, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }
    name_len = (uint32_t)name_len_leb;

    // Validate name length bounds
    if (name_len == 0) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Custom section name cannot be empty");
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }
    if (p + name_len > end) {
        set_error_buf_ex(error_buf, error_buf_size, "unexpected end");
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    // UTF-8 validation (reuse the same logic as load_user_section)
    if (!wasm_check_utf8_str(p, name_len)) {
        set_error_buf_ex(error_buf, error_buf_size, "invalid UTF-8 encoding");
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    // Allocate and copy the section name
    out->name = (char *)wasm_runtime_malloc(name_len + 1);
    if (!out->name) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Memory allocation failed for custom section name");
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    memcpy(out->name, p, name_len);
    out->name[name_len] = '\0';
    p += name_len;

    // Set the data pointer and length
    out->data = p;
    out->data_len = (uint32_t)(end - p);

    // Calculate consumed length
    if (consumed_len) {
        *consumed_len = (uint32_t)(p - *payload) + out->data_len;
    }

    return true;
}

// Individual section free functions
void
wasm_component_free_core_custom_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.core_custom)
        return;

    if (section->parsed.core_custom->name) {
        wasm_runtime_free(section->parsed.core_custom->name);
        section->parsed.core_custom->name = NULL;
    }
    wasm_runtime_free(section->parsed.core_custom);
    section->parsed.core_custom = NULL;
}

/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include "wasm_loader_common.h"
#include "wasm_export.h"
#include <stdio.h>

// Helper function to parse a value
// Helper to check if f32 bytes represent canonical NaN
static bool
is_canonical_f32_nan(const uint8_t *bytes)
{
    return bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xC0
           && bytes[3] == 0x7F;
}

// Helper to check if f64 bytes represent canonical NaN
static bool
is_canonical_f64_nan(const uint8_t *bytes)
{
    return bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x00
           && bytes[3] == 0x00 && bytes[4] == 0x00 && bytes[5] == 0x00
           && bytes[6] == 0xF8 && bytes[7] == 0x7F;
}

// Helper to check if f32/f64 bytes represent any NaN (for rejection)
static bool
is_any_nan(const uint8_t *bytes, bool is_f64)
{
    if (is_f64) {
        // Check f64 NaN pattern (exponent all 1s, mantissa non-zero)
        uint64_t bits = 0;
        memcpy(&bits, bytes, 8);
        return ((bits >> 52) & 0x7FF) == 0x7FF && (bits & 0xFFFFFFFFFFFFF) != 0;
    }
    else {
        // Check f32 NaN pattern (exponent all 1s, mantissa non-zero)
        uint32_t bits = 0;
        memcpy(&bits, bytes, 4);
        return ((bits >> 23) & 0xFF) == 0xFF && (bits & 0x7FFFFF) != 0;
    }
}

// value ::= t:<valtype> len:<core:u32> v:<val(t)> => (value t v) (where len =
// ||v||)
static bool
parse_value(const uint8_t **payload, const uint8_t *end,
            WASMComponentValue *out, char *error_buf, uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Parse the value type
    WASMComponentValueType *val_type = NULL;
    val_type = wasm_runtime_malloc(sizeof(WASMComponentValueType));
    if (!val_type) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for value type");
        return false;
    }
    if (!parse_valtype(&p, end, val_type, error_buf, error_buf_size)) {
        wasm_runtime_free(val_type);
        return false;
    }

    out->val_type = val_type;

    uint64_t core_data_len_u32_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &core_data_len_u32_leb,
                  error_buf, error_buf_size)) {
        wasm_runtime_free(val_type);
        return false;
    }

    out->core_data_len = (uint32_t)core_data_len_u32_leb;

    // Now parse v:<val(t)> according to t. Advance `p` per case and ensure
    // the declared len matches the actual encoding size for handled cases.
    if ((uint32_t)(end - p) < out->core_data_len) {
        set_error_buf_ex(
            error_buf, error_buf_size,
            "Insufficient bytes for value payload: need %u, have %zu",
            out->core_data_len, (size_t)(end - p));
        wasm_runtime_free(val_type);
        return false;
    }

    const uint8_t *v_start = p;

    if (val_type->type == WASM_COMP_VAL_TYPE_PRIMVAL) {
        switch (val_type->type_specific.primval_type) {
            case WASM_COMP_PRIMVAL_BOOL:
            {
                if (out->core_data_len != 1) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid bool length: %u (expected 1)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint8_t b = *p;
                if (b != 0x00 && b != 0x01) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid bool value byte: 0x%02x", b);
                    wasm_runtime_free(val_type);
                    return false;
                }
                p += 1;
                break;
            }

            case WASM_COMP_PRIMVAL_U8:
            {
                if (out->core_data_len != 1) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid u8 length: %u (expected 1)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                p += 1;
                break;
            }

            case WASM_COMP_PRIMVAL_S8:
            {
                if (out->core_data_len != 1) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid s8 length: %u (expected 1)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                p += 1;
                break;
            }

            case WASM_COMP_PRIMVAL_S16:
            {
                // signed LEB128 (1..3 bytes for 16-bit)
                if (out->core_data_len < 1 || out->core_data_len > 3) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid s16 LEB length: %u (expected 1..3)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 16, true, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "s16 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_U16:
            {
                // unsigned LEB128 (1..3 bytes for 16-bit)
                if (out->core_data_len < 1 || out->core_data_len > 3) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid u16 LEB length: %u (expected 1..3)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 16, false, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "u16 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_S32:
            {
                // signed LEB128 (1..5 bytes)
                if (out->core_data_len < 1 || out->core_data_len > 5) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid s32 LEB length: %u (expected 1..5)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 32, true, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "s32 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_U32:
            {
                // unsigned LEB128 (1..5 bytes)
                if (out->core_data_len < 1 || out->core_data_len > 5) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid u32 LEB length: %u (expected 1..5)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 32, false, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "u32 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_S64:
            {
                // signed LEB128 (1..10 bytes)
                if (out->core_data_len < 1 || out->core_data_len > 10) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid s64 LEB length: %u (expected 1..10)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 64, true, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "s64 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_U64:
            {
                // unsigned LEB128 (1..10 bytes)
                if (out->core_data_len < 1 || out->core_data_len > 10) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid u64 LEB length: %u (expected 1..10)",
                        out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint64_t tmp = 0;
                uint8_t *q = (uint8_t *)p;
                const uint8_t *before = p;
                if (!read_leb(&q, end, 64, false, &tmp, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)(q - before) != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "u64 len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)(q - before));
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = (const uint8_t *)q;
                break;
            }

            case WASM_COMP_PRIMVAL_F32:
            {
                if (out->core_data_len != 4) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid f32 length: %u (expected 4)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                if (is_canonical_f32_nan(p)) {
                    p += 4;
                    break;
                }
                else if (is_any_nan(p, false)) {
                    // Reject non-canonical NaN
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Non-canonical NaN not allowed");
                    wasm_runtime_free(val_type);
                    return false;
                }
                p += 4;
                break;
            }

            case WASM_COMP_PRIMVAL_F64:
            {
                if (out->core_data_len != 8) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid f64 length: %u (expected 8)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                if (is_canonical_f64_nan(p)) {
                    p += 8;
                    break;
                }
                else if (is_any_nan(p, true)) {
                    // Reject non-canonical NaN
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Non-canonical NaN not allowed");
                    wasm_runtime_free(val_type);
                    return false;
                }

                p += 8;
                break;
            }

            case WASM_COMP_PRIMVAL_CHAR:
            {
                // val(char) ::= b*:<core:byte>* => c (where b* = core:utf8(c))
                // Expect 1..4 bytes and ensure exactly one UTF-8 scalar
                if (out->core_data_len < 1 || out->core_data_len > 4) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid char length: %u (expected 1..4)",
                                     out->core_data_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                if (!wasm_component_validate_single_utf8_scalar(
                        p, out->core_data_len)) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid UTF-8 scalar for char");
                    wasm_runtime_free(val_type);
                    return false;
                }
                p += out->core_data_len;
                break;
            }

            case WASM_COMP_PRIMVAL_STRING:
            {
                // val(string) ::= v:<core:name> => v
                // core:name = name_len_leb + name_bytes; require outer len ==
                // leb_len + name_len
                const uint8_t *before = p;
                uint8_t *q = (uint8_t *)p;
                uint64_t name_len = 0;
                if (!read_leb(&q, end, 32, false, &name_len, error_buf,
                              error_buf_size)) {
                    wasm_runtime_free(val_type);
                    return false;
                }
                uint32_t leb_len = (uint32_t)(q - before);
                if ((uint32_t)(end - q) < (uint32_t)name_len) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Insufficient bytes for string payload: "
                                     "need %llu, have %zu",
                                     (unsigned long long)name_len,
                                     (size_t)(end - q));
                    wasm_runtime_free(val_type);
                    return false;
                }
                if ((uint32_t)name_len + leb_len != out->core_data_len) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "string len mismatch: declared %u, decoded %u",
                        out->core_data_len, (uint32_t)name_len + leb_len);
                    wasm_runtime_free(val_type);
                    return false;
                }
                if (!wasm_component_validate_utf8(q, (uint32_t)name_len)) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Invalid UTF-8 in string");
                    wasm_runtime_free(val_type);
                    return false;
                }
                p = q + name_len;
                break;
            }
            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Unknown primitive value type 0x%02x",
                                 val_type->type_specific.primval_type);
                wasm_runtime_free(val_type);
                return false;
            }
        }
    }
    else {
        // valtype in the values section must be either primval or typeidx
        if (val_type->type != WASM_COMP_VAL_TYPE_IDX) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unsupported valtype tag %u in values section "
                             "(expected typeidx)",
                             (unsigned)val_type->type);
            wasm_runtime_free(val_type);
            return false;
        }

        // Minimal mode for type-indexed values:
        // - core_data will be set to v_start (the start of v:<val(t)>)
        // - just advance by the declared len and validate bounds
        if ((uint32_t)(end - p) < out->core_data_len) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Insufficient bytes for type-indexed value "
                             "payload: need %u, have %zu",
                             out->core_data_len, (size_t)(end - p));
            wasm_runtime_free(val_type);
            return false;
        }
        p += out->core_data_len;
    }

    // Keep a borrowed pointer to the raw value bytes window
    out->core_data = v_start;

    *payload = p;
    return true;
}

// Section 12: values section
bool
wasm_component_parse_values_section(const uint8_t **payload,
                                    uint32_t payload_len,
                                    WASMComponentValueSection *out,
                                    char *error_buf, uint32_t error_buf_size,
                                    uint32_t *consumed_len)
{
    if (!payload || !*payload || payload_len == 0 || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        if (consumed_len)
            *consumed_len = 0;
        return false;
    }

    const uint8_t *p = *payload;
    const uint8_t *end = *payload + payload_len;

    // values ::= count:<count> value:<value> => (values count value)
    // Read the count of values (LEB128-encoded)
    uint64_t value_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &value_count_leb, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    uint32_t value_count = (uint32_t)value_count_leb;
    out->count = value_count;

    if (value_count > 0) {
        out->values =
            wasm_runtime_malloc(sizeof(WASMComponentValue) * value_count);
        if (!out->values) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for values");
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }

        // Initialize all values to zero to avoid garbage data
        memset(out->values, 0, sizeof(WASMComponentValue) * value_count);

        for (uint32_t i = 0; i < value_count; ++i) {
            if (!parse_value(&p, end, &out->values[i], error_buf,
                             error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse value %u", i);
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }
        }
    }

    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);
    return true;
}

// Individual section free functions
void
wasm_component_free_values_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.value_section) {
        return;
    }

    WASMComponentValueSection *value_section = section->parsed.value_section;
    if (value_section->values) {
        for (uint32_t i = 0; i < value_section->count; i++) {
            // Free the allocated val_type structures
            if (value_section->values[i].val_type) {
                wasm_runtime_free(value_section->values[i].val_type);
                value_section->values[i].val_type = NULL;
            }
            // core_data points into the original payload; do not free
        }
        wasm_runtime_free(value_section->values);
        value_section->values = NULL;
    }
    wasm_runtime_free(value_section);
    section->parsed.value_section = NULL;
}
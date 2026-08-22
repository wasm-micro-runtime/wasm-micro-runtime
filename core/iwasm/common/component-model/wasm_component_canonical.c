/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_canonical.h"
#include "wasm_runtime.h"
#include "wasm_component_runtime.h"
#include "wasm_component_resource.h"
#include "math.h"
#include "wasm_ieee754.h"
#include "stdlib.h"
#include "bh_assert.h"
#include "wasm_component_task.h"

static const int DETERMINISTIC_PROFILE = 0;
static const uint32_t CANONICAL_FLOAT32_NAN = 0x7fc00000u;
static const uint64_t CANONICAL_FLOAT64_NAN = 0x7ff8000000000000ULL;
static const uint32_t UTF16_TAG = (1U << 31);
static const uint32_t MAX_STRING_BYTE_LENGTH = (1U << 31) - 1;

void
set_component_exception(LiftLowerContext *cx, const char *msg)
{
    if (cx && cx->inst && msg) {
        snprintf(cx->inst->cur_exception, sizeof(cx->inst->cur_exception),
                 "Exception: %s", msg);
    }
}

static uint32_t
get_alignment(const WASMComponentTypeInstance *type)
{
    return type->alignment;
}

static uint32_t
get_elem_size(const WASMComponentTypeInstance *type)
{
    return type->elem_size;
}

uint32_t
align_to(uint32_t ptr, uint32_t alignment)
{
    return ((ptr + alignment - 1) / alignment) * alignment;
}

static float
uint32_to_float(uint32_t input)
{
    union ieee754_float result;

    unsigned int sign = (input >> 31) & 0x1;
    unsigned int exp = (input >> 23) & 0xFF;
    unsigned int mant = input & 0x7FFFFF;

    if (is_little_endian()) {
        result.ieee.ieee_little_endian.negative = sign;
        result.ieee.ieee_little_endian.exponent = exp;
        result.ieee.ieee_little_endian.mantissa = mant;
    }
    else {
        result.ieee.ieee_big_endian.negative = sign;
        result.ieee.ieee_big_endian.exponent = exp;
        result.ieee.ieee_big_endian.mantissa = mant;
    }

    return result.f;
}

static double
uint64_to_double(uint64_t input)
{
    union ieee754_double result;

    unsigned int sign = (input >> 63) & 0x1;

    unsigned int exp = (input >> 52) & 0x7FF;

    unsigned int mant0 = (input >> 32) & 0xFFFFF;

    unsigned int mant1 = input & 0xFFFFFFFF;

    if (is_little_endian()) {
        result.ieee.ieee_little_endian.negative = sign;
        result.ieee.ieee_little_endian.exponent = exp;
        result.ieee.ieee_little_endian.mantissa0 = mant0;
        result.ieee.ieee_little_endian.mantissa1 = mant1;
    }
    else {
        result.ieee.ieee_big_endian.negative = sign;
        result.ieee.ieee_big_endian.exponent = exp;
        result.ieee.ieee_big_endian.mantissa0 = mant0;
        result.ieee.ieee_big_endian.mantissa1 = mant1;
    }

    return result.d;
}

float
core_f32_reinterpret_i32(uint32_t i)
{
    float f = uint32_to_float(i);
    return f;
}

double
core_f64_reinterpret_i64(uint64_t i)
{
    double d = uint64_to_double(i);
    return d;
}

float
canonicalize_nan32(float f)
{
    if (isnan(f)) {
        f = core_f32_reinterpret_i32(CANONICAL_FLOAT32_NAN);
        bh_assert(isnan(f));
    }
    return f;
}

double
canonicalize_nan64(double d)
{
    if (isnan(d)) {
        d = core_f64_reinterpret_i64(CANONICAL_FLOAT64_NAN);
        bh_assert(isnan(d));
    }
    return d;
}

static float
decode_i32_as_float(uint32_t i)
{
    return canonicalize_nan32(core_f32_reinterpret_i32(i));
}

static double
decode_i64_as_float(uint64_t i)
{
    return canonicalize_nan64(core_f64_reinterpret_i64(i));
}

bool
convert_i32_to_char(LiftLowerContext *cx, uint32_t i, uint32_t *out)
{
    trap_if(i >= 0x110000);
    trap_if((0xD800 <= i) && (i <= 0xDFFF));
    *out = i;
    return true;
}

static bool
load_int(LiftLowerContext *cx, uint32_t ptr, uint32_t nbytes, bool is_signed,
         uint64_t *out)
{
    const WASMMemoryInstance *mem = get_mem_from_cx(cx);
    bh_assert(nbytes >= 1 && nbytes <= 8);
    trap_if((uint64)(ptr + nbytes) > mem->memory_data_size);

    const uint8_t *src = mem->memory_data + ptr;

    // Always load as unsigned first
    uint64_t val = 0;

    // Read bytes in little endian order
    for (uint32_t i = 0; i < nbytes; i++) {
        val |= ((uint64_t)src[i]) << (i * 8);
    }

    // Sign-extend if needed
    if (is_signed && nbytes < 8) {
        uint32_t shift = 64 - (nbytes * 8);
        val = (uint64_t)((int64_t)(val << shift) >> shift);
    }

    *out = val;
    return true;
}

static bool
decode_utf8(LiftLowerContext *cx, const uint8_t *src, uint32_t len,
            char **out_str, uint32_t *out_len)
{
    // Validate UTF-8
    if (!wasm_check_utf8_str(src, (uint32)len)) {
        set_component_exception(cx, "invalid UTF-8");
        return false;
    }

    char *str = (char *)wasm_runtime_malloc(len + 1);
    if (!str) {
        set_component_exception(cx, "out of memory");
        return false;
    }

    memcpy(str, src, len);
    str[len] = '\0';

    *out_str = str;
    *out_len = len;
    return true;
}

static bool
decode_utf16_le(LiftLowerContext *cx, const uint8_t *src, uint32_t len,
                char **out_str, uint32_t *out_len)
{
    if (len % 2 != 0) {
        set_component_exception(cx, "invalid UTF-16 length");
        return false;
    }

    uint32_t max_utf8_size = (len / 2) * 3; // worst-case UTF-8 size
    char *str = (char *)wasm_runtime_malloc(max_utf8_size + 1);
    if (!str) {
        set_component_exception(cx, "out of memory");
        return false;
    }

    uint32_t utf8_pos = 0;
    uint32_t i = 0;

    while (i < len) {
        uint16_t code_unit = src[i] | (src[i + 1] << 8);
        i += 2;
        uint32_t codepoint = 0;

        if (code_unit >= 0xD800 && code_unit <= 0xDBFF) { // high surrogate
            if (i >= len) {
                wasm_runtime_free(str);
                set_component_exception(cx, "unpaired high surrogate");
                return false;
            }
            uint16_t low = src[i] | (src[i + 1] << 8);
            i += 2;
            if (low < 0xDC00 || low > 0xDFFF) {
                wasm_runtime_free(str);
                set_component_exception(cx, "invalid low surrogate");
                return false;
            }
            codepoint = 0x10000 + ((code_unit - 0xD800) << 10) + (low - 0xDC00);
        }
        else if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
            wasm_runtime_free(str);
            set_component_exception(cx, "unpaired low surrogate");
            return false;
        }
        else {
            codepoint = code_unit;
        }

        // Encode to UTF-8
        if (codepoint < 0x80) {
            str[utf8_pos++] = (char)codepoint;
        }
        else if (codepoint < 0x800) {
            str[utf8_pos++] = (char)(0xC0 | (codepoint >> 6));
            str[utf8_pos++] = (char)(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000) {
            str[utf8_pos++] = (char)(0xE0 | (codepoint >> 12));
            str[utf8_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            str[utf8_pos++] = (char)(0x80 | (codepoint & 0x3F));
        }
        else {
            str[utf8_pos++] = (char)(0xF0 | (codepoint >> 18));
            str[utf8_pos++] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
            str[utf8_pos++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            str[utf8_pos++] = (char)(0x80 | (codepoint & 0x3F));
        }
    }

    str[utf8_pos] = '\0';
    *out_str = str;
    *out_len = utf8_pos;
    return true;
}

static bool
decode_latin1(LiftLowerContext *cx, const uint8_t *src, uint32_t len,
              char **out_str, uint32_t *out_len)
{
    uint32_t utf8_size = 0;
    for (uint32_t i = 0; i < len; i++)
        utf8_size += (src[i] < 0x80) ? 1 : 2;

    char *str = (char *)wasm_runtime_malloc(utf8_size + 1);
    if (!str) {
        set_component_exception(cx, "out of memory");
        return false;
    }

    uint32_t pos = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t byte = src[i];
        if (byte < 0x80) {
            str[pos++] = (char)byte;
        }
        else {
            str[pos++] = (char)(0xC0 | (byte >> 6));
            str[pos++] = (char)(0x80 | (byte & 0x3F));
        }
    }

    str[pos] = '\0';
    *out_str = str;
    *out_len = pos;
    return true;
}

static bool
decode_string(LiftLowerContext *cx, const uint8_t *src, uint32_t len,
              StringDecoding decoding, char **out_str, uint32_t *out_len)
{
    switch (decoding) {
        case DECODING_UTF_8:
            return decode_utf8(cx, src, len, out_str, out_len);
        case DECODING_UTF_16_LE:
            return decode_utf16_le(cx, src, len, out_str, out_len);
        case DECODING_LATIN_1:
            return decode_latin1(cx, src, len, out_str, out_len);
        default:
            set_component_exception(cx, "unknown decoding");
            return false;
    }
}

bool
load_string_from_range(LiftLowerContext *cx, uint32_t begin,
                       uint32_t tagged_code_units, wit_value_t *out)
{
    StringEncoding encoding = get_string_encoding(cx);
    WASMMemoryInstance *mem = get_mem_from_cx(cx);

    uint32_t alignment = 0;
    uint32_t byte_length = 0;
    StringDecoding decoding = DECODING_UTF_8;

    switch (encoding) {
        case ENCODING_UTF_8:
        {
            alignment = 1;
            byte_length = tagged_code_units;
            decoding = DECODING_UTF_8;
            break;
        }

        case ENCODING_UTF_16:
        {
            alignment = 2;
            byte_length = 2 * tagged_code_units;
            decoding = DECODING_UTF_16_LE;
            break;
        }

        case ENCODING_LATIN_1_UTF_16:
        {
            alignment = 2;
            if (tagged_code_units & UTF16_TAG) {
                byte_length = 2 * (tagged_code_units ^ UTF16_TAG);
                decoding = DECODING_UTF_16_LE;
            }
            else {
                byte_length = tagged_code_units;
                decoding = DECODING_LATIN_1;
            }
            break;
        }

        default:
        {
            set_component_exception(cx, "unknown string encoding");
            return false;
        }
    }

    trap_if(begin != align_to(begin, alignment));
    trap_if((uint64_t)(begin + byte_length) > mem->memory_data_size);

    const uint8_t *src = mem->memory_data + begin;
    char *decoded_str = NULL;
    uint32_t decoded_len = 0;

    if (!decode_string(cx, src, byte_length, decoding, &decoded_str,
                       &decoded_len)) {
        return false;
    }

    *out =
        wit_string_ctor(decoded_str, decoded_len, tagged_code_units, encoding);
    wasm_runtime_free(decoded_str);

    return true;
}

static bool
load_string(LiftLowerContext *cx, uint32_t ptr, wit_value_t *out)
{
    uint64_t begin64 = 0, tagged_code_units64 = 0;

    if (!load_int(cx, ptr, 4, false, &begin64)) {
        return false;
    }

    if (!load_int(cx, ptr + 4, 4, false, &tagged_code_units64)) {
        return false;
    }

    uint32_t begin = (uint32_t)begin64;
    uint32_t tagged_code_units = (uint32_t)tagged_code_units64;

    return load_string_from_range(cx, begin, tagged_code_units, out);
}

bool
lift_error_context(LiftLowerContext *cx, uint32_t errctx_val, wit_value_t *out)
{
    uint32_t errctx = (uint32_t)(uintptr_t)wasm_component_table_get(
        cx->inst->table, errctx_val, WASM_TABLE_ELEM_ERROR_CONTEXT);

    // For now, just store the i32 opaque handle
    *out = wit_errctx_ctor(errctx);

    return true;
}

static bool
load_primitive_value(LiftLowerContext *cx, uint32_t ptr,
                     WASMComponentPrimValType primval, wit_value_t *out)
{
    uint64_t loaded_val = 0;

    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
        {
            if (!load_int(cx, ptr, 1, false, &loaded_val))
                return false;
            uint8_t val = (uint8_t)loaded_val;
            *out = wit_bool_ctor(val != 0); // 0 = false, non-zero = true
            return true;
        }

        case WASM_COMP_PRIMVAL_U8:
        {
            if (!load_int(cx, ptr, 1, false, &loaded_val))
                return false;
            *out = wit_u8_ctor((uint8_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_U16:
        {
            if (!load_int(cx, ptr, 2, false, &loaded_val))
                return false;
            *out = wit_u16_ctor((uint16_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_U32:
        {
            if (!load_int(cx, ptr, 4, false, &loaded_val))
                return false;
            *out = wit_u32_ctor((uint32_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_U64:
        {
            if (!load_int(cx, ptr, 8, false, &loaded_val))
                return false;
            *out = wit_u64_ctor(loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_S8:
        {
            if (!load_int(cx, ptr, 1, true, &loaded_val))
                return false;
            *out = wit_s8_ctor((int8_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_S16:
        {
            if (!load_int(cx, ptr, 2, true, &loaded_val))
                return false;
            *out = wit_s16_ctor((int16_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_S32:
        {
            if (!load_int(cx, ptr, 4, true, &loaded_val))
                return false;
            *out = wit_s32_ctor((int32_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_S64:
        {
            if (!load_int(cx, ptr, 8, true, &loaded_val))
                return false;
            *out = wit_s64_ctor((int64_t)loaded_val);
            return true;
        }

        case WASM_COMP_PRIMVAL_F32:
        {
            if (!load_int(cx, ptr, 4, false, &loaded_val))
                return false;
            *out = wit_f32_ctor(decode_i32_as_float((uint32_t)loaded_val));
            return true;
        }

        case WASM_COMP_PRIMVAL_F64:
        {
            if (!load_int(cx, ptr, 8, false, &loaded_val))
                return false;
            *out = wit_f64_ctor(decode_i64_as_float(loaded_val));
            return true;
        }

        case WASM_COMP_PRIMVAL_CHAR:
        {
            if (!load_int(cx, ptr, 4, false, &loaded_val))
                return false;
            uint32_t codepoint = 0;
            if (!convert_i32_to_char(cx, (uint32_t)loaded_val, &codepoint))
                return false;
            *out = wit_char_ctor(codepoint);
            return true;
        }

        case WASM_COMP_PRIMVAL_STRING:
        {
            return load_string(cx, ptr, out);
        }

        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
        {
            if (!load_int(cx, ptr, 4, false, &loaded_val))
                return false;

            return lift_error_context(cx, loaded_val, out);
        }

        default:
        {
            return false;
        }
    }
}

static bool
load_list_from_valid_range(LiftLowerContext *cx, uint32_t ptr, uint32_t length,
                           WASMComponentTypeInstance *type, wit_value_t *out)
{
    // Allocate array for elements
    wit_value_t *elements =
        (wit_value_t *)wasm_runtime_malloc(length * sizeof(wit_value_t));
    if (!elements && length > 0) {
        set_component_exception(cx, "out of memory for list elements");
        return false;
    }

    uint32_t elem_sz = get_elem_size(type);

    // Load each element
    for (uint32_t i = 0; i < length; i++) {
        if (!load(cx, ptr + (i * elem_sz), type, &elements[i])) {
            wasm_runtime_free((void *)elements);
            return false;
        }
    }

    *out = wit_list_ctor(elements, length);
    return true;
}

bool
load_list_from_range(LiftLowerContext *cx, uint32_t ptr, uint32_t length,
                     WASMComponentTypeInstance *type, wit_value_t *out)
{
    uint32_t elem_alignment = get_alignment(type);
    uint32_t elem_sz = get_elem_size(type);
    const WASMMemoryInstance *mem = get_mem_from_cx(cx);
    trap_if(ptr != align_to(ptr, elem_alignment));
    trap_if((uint64_t)(ptr + (length * elem_sz)) > mem->memory_data_size);

    return load_list_from_valid_range(cx, ptr, length, type, out);
}

static bool
load_list(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
          uint32_t maybe_length, bool is_fixed_size, wit_value_t *out)
{
    if (is_fixed_size) {
        return load_list_from_valid_range(cx, ptr, maybe_length, type, out);
    }

    uint64_t begin64 = 0, length64 = 0;

    if (!load_int(cx, ptr, 4, false, &begin64)) {
        return false;
    }

    if (!load_int(cx, ptr + 4, 4, false, &length64)) {
        return false;
    }

    uint32_t begin = (uint32_t)begin64;
    uint32_t length = (uint32_t)length64;

    return load_list_from_range(cx, begin, length, type, out);
}

static bool
load_record(LiftLowerContext *cx, uint32_t ptr,
            WASMComponentRecordInstance *type, wit_value_t *out)
{
    // Allocate array for elements
    ComponentWITRecordField *elements =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            type->count * sizeof(ComponentWITRecordField));
    if (!elements) {
        set_component_exception(cx, "out of memory for record elements");
        return false;
    }

    wit_value_t temp = NULL;
    for (uint32_t field = 0; field < type->count; field++) {
        temp = NULL;

        ptr = align_to(ptr, get_alignment(type->fields[field].type));
        if (!load(cx, ptr, type->fields[field].type, &temp)) {
            free_wit_value(temp);
            for (uint32_t index = 0; index < field; index++) {
                free_wit_value(elements[index].value);
                wasm_runtime_free(elements[index].key);
            }
            wasm_runtime_free(elements);
            return false;
        }

        init_record_field(&elements[field], type->fields[field].label->name,
                          type->fields[field].label->name_len, temp);

        ptr += get_elem_size(type->fields[field].type);
    }

    *out = wit_record_ctor(elements, type->count);

    return true;
}

static bool
load_variant(LiftLowerContext *cx, uint32_t ptr,
             WASMComponentVariantInstance *type, wit_value_t *out)
{
    uint32_t disc_size = compute_discriminant_alignment(type->count);
    uint64_t case_index64 = 0;

    if (!load_int(cx, ptr, disc_size, false, &case_index64)) {
        return false;
    }

    uint32_t case_index = (uint32_t)case_index64;

    ptr += disc_size;
    trap_if(case_index >= type->count);

    WASMComponentCaseValInstance c = type->cases[case_index];

    ptr = align_to(ptr, compute_max_case_alignment(type));
    if (c.value_type == NULL) {
        *out = wit_variant_ctor(c.label->name, c.label->name_len, NULL);
        return true;
    }

    wit_value_t temp = NULL;
    if (!load(cx, ptr, c.value_type, &temp)) {
        return false;
    }

    *out = wit_variant_ctor(c.label->name, c.label->name_len, temp);
    return true;
}

bool
unpack_flags_from_int(LiftLowerContext *cx, uint64_t i,
                      WASMComponentFlagType *labels, wit_value_t *out)
{
    // Allocate array for elements
    ComponentWITRecordField *elements =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            labels->count * sizeof(ComponentWITRecordField));
    if (!elements) {
        set_component_exception(cx, "out of memory for flags elements");
        return false;
    }

    for (uint32_t index = 0; index < labels->count; index++) {
        init_record_field(&elements[index], labels->flags[index].name,
                          labels->flags[index].name_len, wit_bool_ctor(i & 1));
        i >>= 1;
    }

    *out = wit_flag_ctor(elements, labels->count);

    return true;
}

static bool
load_flags(LiftLowerContext *cx, uint32_t ptr, WASMComponentFlagType *type,
           uint32_t flag_size, wit_value_t *out)
{
    uint64_t i = 0;

    if (!load_int(cx, ptr, flag_size, false, &i)) {
        return false;
    }

    return unpack_flags_from_int(cx, i, type, out);
}

static bool
load_tuple(LiftLowerContext *cx, uint32_t ptr, WASMComponentTupleInstance *type,
           wit_value_t *out)
{
    wit_value_t *elems =
        (wit_value_t *)wasm_runtime_malloc(type->count * sizeof(wit_value_t));
    if (!elems) {
        set_component_exception(cx, "out of memory for tuple elements");
        return false;
    }

    for (uint32_t field = 0; field < type->count; field++) {
        elems[field] = NULL;

        ptr = align_to(ptr, get_alignment(type->element_types[field]));
        if (!load(cx, ptr, type->element_types[field], &elems[field])) {
            free_wit_value(elems[field]);
            for (uint32_t index = 0; index < field; index++) {
                free_wit_value(elems[index]);
            }
            wasm_runtime_free((void *)elems);
            return false;
        }

        ptr += get_elem_size(type->element_types[field]);
    }

    *out = wit_tuple_ctor(elems, type->count);
    return true;
}

static bool
load_enum(LiftLowerContext *cx, uint32_t ptr, const WASMComponentEnumType *type,
          wit_value_t *out)
{
    uint32_t disc_size = compute_discriminant_alignment(type->count);
    uint64_t case_index64 = 0;

    if (!load_int(cx, ptr, disc_size, false, &case_index64)) {
        return false;
    }

    uint32_t case_index = (uint32_t)case_index64;
    trap_if(case_index >= type->count);

    *out = wit_enum_ctor(case_index);
    return true;
}

static bool
load_option(LiftLowerContext *cx, uint32_t ptr,
            WASMComponentOptionInstance *type, wit_value_t *out)
{
    uint32_t disc_size = compute_discriminant_alignment(2); // 2 cases
    uint64_t case_index64 = 0;

    if (!load_int(cx, ptr, disc_size, false, &case_index64)) {
        return false;
    }

    uint32_t case_index = (uint32_t)case_index64;
    trap_if(case_index >= 2);

    ptr += disc_size;

    // Compute max_case_alignment
    // Case 0 ("none"): no payload, alignment = 1
    // Case 1 ("some"): has payload of type->element_type
    uint32_t max_case_alignment = 1;
    if (type->element_type) {
        max_case_alignment = get_alignment(type->element_type);
    }
    ptr = align_to(ptr, max_case_alignment);

    if (case_index == 0) {
        // Case "none" - no payload
        *out = wit_option_ctor(NULL);
        return true;
    }
    else {
        // Case "some" - has payload
        if (type->element_type == NULL) {
            *out = wit_option_ctor(NULL);
            return true;
        }

        wit_value_t payload = NULL;
        if (!load(cx, ptr, type->element_type, &payload)) {
            return false;
        }

        *out = wit_option_ctor(payload);
        return true;
    }
}

static bool
load_result(LiftLowerContext *cx, uint32_t ptr,
            WASMComponentResultInstance *type, wit_value_t *out)
{
    uint32_t disc_size =
        compute_discriminant_alignment(2); // 2 cases: ok, error
    uint64_t case_index64 = 0;

    if (!load_int(cx, ptr, disc_size, false, &case_index64)) {
        return false;
    }

    uint32_t case_index = (uint32_t)case_index64;
    trap_if(case_index >= 2);
    ptr += disc_size;

    // Compute max_case_alignment
    // Case 0 ("ok"): payload type->result_type
    // Case 1 ("error"): payload type->error_type
    uint32_t max_case_alignment = 1;
    if (type->result_type) {
        max_case_alignment = get_alignment(type->result_type);
    }
    if (type->error_type) {
        uint32_t error_align = get_alignment(type->error_type);
        if (error_align > max_case_alignment) {
            max_case_alignment = error_align;
        }
    }
    ptr = align_to(ptr, max_case_alignment);

    if (case_index == 0) {
        // Case "ok"
        if (type->result_type == NULL) {
            *out = wit_result_ctor(false, NULL);
            return true;
        }

        wit_value_t payload = NULL;
        if (!load(cx, ptr, type->result_type, &payload)) {
            return false;
        }

        *out = wit_result_ctor(false, payload);
        return true;
    }
    else {
        // Case "error"
        if (type->error_type == NULL) {
            *out = wit_result_ctor(true, NULL);
            return true;
        }

        wit_value_t payload = NULL;
        if (!load(cx, ptr, type->error_type, &payload)) {
            return false;
        }

        *out = wit_result_ctor(true, payload);
        return true;
    }
}

bool
lift_own(LiftLowerContext *cx, uint32_t index,
         WASMComponentResourceHandleInstance *type, wit_value_t *out)
{
    // 1. Look up handle in table
    const WASMResourceHandle *handle =
        (WASMResourceHandle *)wasm_component_table_get(
            cx->inst->table, index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    trap_if(handle == NULL);

    // 2. Validate it's an own handle
    trap_if(!handle->own);

    // 3. Get the rep
    uint32_t rep = handle->rep;

    // 4. Remove from table (ownership transfers out)
    trap_if(!wasm_component_table_remove(cx->inst->table, index));

    // 5. Return rep
    *out = wit_resource_ctor(rep);
    return true;
}

bool
lift_borrow(LiftLowerContext *cx, uint32_t index,
            const WASMComponentResourceHandleInstance *type, wit_value_t *out)
{
    bh_assert(cx->borrow_scope_type == BORROW_SCOPE_SUBTASK);

    // 1. Look up handle in table
    WASMResourceHandle *handle = (WASMResourceHandle *)wasm_component_table_get(
        cx->inst->table, index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    trap_if(handle == NULL);
    trap_if(handle->rt != type->resource);
    // 2. For sync-only: no need to validate own vs borrow. Both are valid - we
    // just extract the rep

    // 3. If borrowing from an own handle, track the lend
    if (!subtask_add_lender(cx->borrow_scope.subtask, handle))
        return false;

    // 4. Save rep
    *out = wit_resource_ctor(handle->rep);
    return true;
}

bool
load(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
     wit_value_t *out)
{
    if (!type)
        return false;
    bh_assert(ptr == align_to(ptr, type->alignment));
    bh_assert((uint64_t)(ptr + type->elem_size)
              <= get_mem_from_cx(cx)->memory_data_size);

    switch (type->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
        {
            return load_primitive_value(cx, ptr, type->type_specific.primval,
                                        out);
        }

        case COMPONENT_VAL_TYPE_LIST:
        {
            return load_list(cx, ptr, type->type_specific.list->element_type, 0,
                             false, out);
        }

        case COMPONENT_VAL_TYPE_FIXED_SIZE_LIST:
        {
            return load_list(cx, ptr,
                             type->type_specific.list_len->element_type,
                             type->type_specific.list_len->len, true, out);
        }

        case COMPONENT_VAL_TYPE_RECORD:
        {
            return load_record(cx, ptr, type->type_specific.record, out);
        }

        case COMPONENT_VAL_TYPE_VARIANT:
        {
            return load_variant(cx, ptr, type->type_specific.variant, out);
        }

        case COMPONENT_VAL_TYPE_FLAGS:
        {
            return load_flags(cx, ptr, type->type_specific.flag,
                              type->elem_size, out);
        }

        case COMPONENT_VAL_TYPE_TUPLE:
        {
            return load_tuple(cx, ptr, type->type_specific.tuple, out);
        }

        case COMPONENT_VAL_TYPE_ENUM:
        {
            return load_enum(cx, ptr, type->type_specific.enum_type, out);
        }

        case COMPONENT_VAL_TYPE_OPTION:
        {
            return load_option(cx, ptr, type->type_specific.option, out);
        }

        case COMPONENT_VAL_TYPE_RESULT:
        {
            return load_result(cx, ptr, type->type_specific.result, out);
        }

        case COMPONENT_VAL_TYPE_OWN:
        {
            uint64_t index64 = 0;
            if (!load_int(cx, ptr, 4, false, &index64))
                return false;
            return lift_own(cx, (uint32_t)index64,
                            type->type_specific.resource_handle, out);
        }

        case COMPONENT_VAL_TYPE_BORROW:
        {
            uint64_t index64 = 0;
            if (!load_int(cx, ptr, 4, false, &index64))
                return false;
            return lift_borrow(cx, (uint32_t)index64,
                               type->type_specific.resource_handle, out);
        }

        default:
        {
            set_component_exception(cx, "invalid type to load");
            return false;
        }
    }

    return false;
}

// Store
static bool
store_int(LiftLowerContext *cx, uint32_t ptr, uint32_t nbytes, uint64_t val)
{
    WASMMemoryInstance *mem = get_mem_from_cx(cx);
    bh_assert(nbytes >= 1 && nbytes <= 8);
    uint8_t *dst = mem->memory_data + ptr;
    trap_if((uint64)(ptr + nbytes) > mem->memory_data_size);

    for (uint32_t i = 0; i < nbytes; i++) {
        dst[i] = (uint8_t)(val >> (i * 8));
    }

    return true;
}

static uint32_t
random_nan_bits_32(uint32_t total_bits, uint32_t exponent_bits)
{
    uint32_t fraction_bits = total_bits - exponent_bits - 1;
    uint32_t bits = 0;

    for (int i = 0; i < 4; i++) {
        bits |= ((uint32_t)rand() & 0xFF) << (i * 8);
    }

    uint32_t exponent_mask = ((1u << exponent_bits) - 1) << fraction_bits;
    bits |= exponent_mask;

    int random_bit_pos = (int)(rand() % (fraction_bits - 1));
    bits |= (1u << random_bit_pos);

    return bits;
}

static uint64_t
random_nan_bits_64(uint32_t total_bits, uint32_t exponent_bits)
{
    uint32_t fraction_bits = total_bits - exponent_bits - 1;
    uint64_t bits = 0;

    for (int i = 0; i < 8; i++) {
        bits |= ((uint64_t)rand() & 0xFF) << (i * 8);
    }

    uint64_t exponent_mask = ((1ull << exponent_bits) - 1) << fraction_bits;
    bits |= exponent_mask;

    int random_bit_pos = (int)(rand() % (fraction_bits - 1));
    bits |= (1ull << random_bit_pos);

    return bits;
}

float
maybe_scramble_nan32(float f)
{
    if (isnan(f)) {
        if (DETERMINISTIC_PROFILE) {
            f = core_f32_reinterpret_i32(CANONICAL_FLOAT32_NAN);
        }
        else {
            f = core_f32_reinterpret_i32(random_nan_bits_32(32, 8));
        }

        bh_assert(isnan(f));
    }

    return f;
}

double
maybe_scramble_nan64(double d)
{
    if (isnan(d)) {
        if (DETERMINISTIC_PROFILE) {
            d = core_f64_reinterpret_i64(CANONICAL_FLOAT64_NAN);
        }
        else {
            d = core_f64_reinterpret_i64(random_nan_bits_64(64, 11));
        }

        bh_assert(isnan(d));
    }

    return d;
}

static uint32_t
core_i32_reinterpret_f32(float f)
{
    union ieee754_float u;
    u.f = f;
    uint32_t sign = 0, exp = 0, mant = 0;

    if (is_little_endian()) {
        sign = u.ieee.ieee_little_endian.negative;
        exp = u.ieee.ieee_little_endian.exponent;
        mant = u.ieee.ieee_little_endian.mantissa;
    }
    else {
        sign = u.ieee.ieee_big_endian.negative;
        exp = u.ieee.ieee_big_endian.exponent;
        mant = u.ieee.ieee_big_endian.mantissa;
    }

    return (sign << 31) | (exp << 23) | mant;
}

static uint64_t
core_i64_reinterpret_f64(double d)
{
    union ieee754_double u;
    u.d = d;
    uint32_t sign = 0, exp = 0, mant0 = 0, mant1 = 0;

    if (is_little_endian()) {
        sign = u.ieee.ieee_little_endian.negative;
        exp = u.ieee.ieee_little_endian.exponent;
        mant0 = u.ieee.ieee_little_endian.mantissa0;
        mant1 = u.ieee.ieee_little_endian.mantissa1;
    }
    else {
        sign = u.ieee.ieee_big_endian.negative;
        exp = u.ieee.ieee_big_endian.exponent;
        mant0 = u.ieee.ieee_big_endian.mantissa0;
        mant1 = u.ieee.ieee_big_endian.mantissa1;
    }

    uint64_t result = 0;

    result |= ((uint64_t)sign << 63);
    result |= ((uint64_t)exp << 52);
    result |= ((uint64_t)mant0 << 32);
    result |= mant1;

    return result;
}

uint32_t
encode_float_as_i32(float f)
{
    return core_i32_reinterpret_f32(maybe_scramble_nan32(f));
}

uint64_t
encode_float_as_i64(double d)
{
    return core_i64_reinterpret_f64(maybe_scramble_nan64(d));
}

bool
char_to_i32(LiftLowerContext *cx, uint32_t c, uint32_t *out)
{
    trap_if(c >= 0x110000);
    trap_if((c >= 0xD800) && (c <= 0xDFFF));
    *out = c;
    return true;
}

static bool
write_bytes_to_memory(LiftLowerContext *cx, uint32_t ptr, const uint8_t *bytes,
                      uint32_t length)
{
    WASMMemoryInstance *mem = get_mem_from_cx(cx);

    if (!mem || !mem->memory_data) {
        set_component_exception(cx, "invalid memory instance");
        return false;
    }

    if ((uint64_t)ptr + (uint64_t)length > mem->memory_data_size) {
        set_component_exception(cx, "memory access out of bounds");
        return false;
    }

    uint8_t *dst = mem->memory_data + ptr;
    for (uint32_t i = 0; i < length; i++) {
        dst[i] = bytes[i];
    }

    return true;
}

// Encode UTF-8 string to UTF-16LE bytes
static bool
encode_utf8_to_utf16le(const char *utf8_str, uint32_t utf8_len,
                       uint8_t **out_bytes, uint32_t *out_byte_len)
{
    uint32_t max_utf16_units = utf8_len;
    uint8_t *buffer = (uint8_t *)wasm_runtime_malloc(max_utf16_units * 2);
    if (!buffer)
        return false;

    uint32_t utf16_pos = 0;
    uint32_t i = 0;

    while (i < utf8_len) {
        uint32_t codepoint = 0;
        uint8_t byte = (uint8_t)utf8_str[i];

        // Decode UTF-8 codepoint
        if (byte < 0x80) {
            // 1-byte sequence (ASCII)
            codepoint = byte;
            i += 1;
        }
        else if ((byte & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x1F) << 6) | (utf8_str[i + 1] & 0x3F);
            i += 2;
        }
        else if ((byte & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x0F) << 12) | ((utf8_str[i + 1] & 0x3F) << 6)
                        | (utf8_str[i + 2] & 0x3F);
            i += 3;
        }
        else if ((byte & 0xF8) == 0xF0) {
            // 4-byte sequence -> needs surrogate pair in UTF-16
            if (i + 3 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x07) << 18) | ((utf8_str[i + 1] & 0x3F) << 12)
                        | ((utf8_str[i + 2] & 0x3F) << 6)
                        | (utf8_str[i + 3] & 0x3F);
            i += 4;
        }
        else {
            goto error;
        }

        // Encode to UTF-16LE
        if (codepoint < 0x10000) {
            // BMP: single 16-bit code unit (little-endian)
            buffer[utf16_pos++] = codepoint & 0xFF;        // low byte
            buffer[utf16_pos++] = (codepoint >> 8) & 0xFF; // high byte
        }
        else {
            // Supplementary: surrogate pair
            codepoint -= 0x10000;
            uint16_t high = 0xD800 + (codepoint >> 10);
            uint16_t low = 0xDC00 + (codepoint & 0x3FF);

            // Write high surrogate (little-endian)
            buffer[utf16_pos++] = high & 0xFF;
            buffer[utf16_pos++] = (high >> 8) & 0xFF;

            // Write low surrogate (little-endian)
            buffer[utf16_pos++] = low & 0xFF;
            buffer[utf16_pos++] = (low >> 8) & 0xFF;
        }
    }

    *out_bytes = buffer;
    *out_byte_len = utf16_pos;
    return true;

error:
    wasm_runtime_free(buffer);
    return false;
}

// Encode UTF-8 string to Latin-1 bytes
static bool
encode_utf8_to_latin1(const char *utf8_str, uint32_t utf8_len,
                      uint8_t **out_bytes, uint32_t *out_byte_len)
{
    uint8_t *buffer = (uint8_t *)wasm_runtime_malloc(utf8_len); // Max size
    if (!buffer)
        return false;

    uint32_t latin1_pos = 0;
    uint32_t i = 0;

    while (i < utf8_len) {
        uint32_t codepoint = 0;
        uint8_t byte = (uint8_t)utf8_str[i];

        // Decode UTF-8 codepoint
        if (byte < 0x80) {
            codepoint = byte;
            i += 1;
        }
        else if ((byte & 0xE0) == 0xC0) {
            if (i + 1 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x1F) << 6) | (utf8_str[i + 1] & 0x3F);
            i += 2;
        }
        else if ((byte & 0xF0) == 0xE0) {
            if (i + 2 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x0F) << 12) | ((utf8_str[i + 1] & 0x3F) << 6)
                        | (utf8_str[i + 2] & 0x3F);
            i += 3;
        }
        else if ((byte & 0xF8) == 0xF0) {
            if (i + 3 >= utf8_len)
                goto error;
            codepoint = ((byte & 0x07) << 18) | ((utf8_str[i + 1] & 0x3F) << 12)
                        | ((utf8_str[i + 2] & 0x3F) << 6)
                        | (utf8_str[i + 3] & 0x3F);
            i += 4;
        }
        else {
            goto error;
        }

        // Check if codepoint fits in Latin-1 (0-255)
        if (codepoint > 0xFF) {
            goto error; // Can't encode in Latin-1
        }

        buffer[latin1_pos++] = (uint8_t)codepoint;
    }

    *out_bytes = buffer;
    *out_byte_len = latin1_pos;
    return true;

error:
    wasm_runtime_free(buffer);
    return false;
}

bool
encode_string(LiftLowerContext *cx, const char *utf8_str, uint32_t utf8_len,
              StringEncoding encoding, uint8_t **out_bytes,
              uint32_t *out_byte_len, uint32_t *out_code_units)
{
    switch (encoding) {
        case ENCODING_UTF_8:
            // UTF-8 -> UTF-8: no conversion needed
            *out_bytes = (uint8_t *)utf8_str;
            *out_byte_len = utf8_len;
            *out_code_units = utf8_len;
            return true;

        case ENCODING_UTF_16:
            // UTF-8 -> UTF-16LE
            if (!encode_utf8_to_utf16le(utf8_str, utf8_len, out_bytes,
                                        out_byte_len)) {
                set_component_exception(cx, "UTF-8 to UTF-16 encoding failed");
                return false;
            }
            *out_code_units = *out_byte_len / 2;
            return true;

        case ENCODING_LATIN_1_UTF_16:
            // UTF-8 -> Latin-1 (or UTF-16 with tag if Latin-1 fails)
            // Try Latin-1 first
            if (encode_utf8_to_latin1(utf8_str, utf8_len, out_bytes,
                                      out_byte_len)) {
                // Success: all characters fit in Latin-1
                *out_code_units = *out_byte_len; // NO UTF16_TAG
                return true;
            }
            else {
                // Failed: some characters > U+00FF, use UTF-16 with tag
                if (!encode_utf8_to_utf16le(utf8_str, utf8_len, out_bytes,
                                            out_byte_len)) {
                    set_component_exception(cx,
                                            "UTF-8 to UTF-16 encoding failed");
                    return false;
                }
                *out_code_units =
                    (*out_byte_len / 2) | UTF16_TAG; // SET UTF16_TAG
                return true;
            }

        default:
            set_component_exception(cx, "unknown encoding");
            return false;
    }
}

static bool
store_string_copy(LiftLowerContext *cx, const char *src, uint32_t src_byte_len,
                  StringEncoding dst_encoding, uint32_t *out_ptr,
                  uint32_t *out_code_units)
{
    const WASMMemoryInstance *mem = get_mem_from_cx(cx);
    uint8_t *encoded_bytes = NULL;
    uint32_t encoded_byte_len = 0;
    uint32_t code_units = 0;

    // Encode UTF-8 to destination encoding (mirrors decode_string)
    if (!encode_string(cx, src, src_byte_len, dst_encoding, &encoded_bytes,
                       &encoded_byte_len, &code_units)) {
        return false;
    }

    // Determine if we need to free the encoded bytes
    // (UTF-8 case reuses the source pointer, others allocate new memory)
    bool need_free = (encoded_bytes != (uint8_t *)src);

    // Determine alignment based on encoding
    uint32_t alignment = 0;
    switch (dst_encoding) {
        case ENCODING_UTF_8:
            alignment = 1;
            break;
        case ENCODING_UTF_16:
        case ENCODING_LATIN_1_UTF_16:
            alignment = 2;
            break;
        default:
            set_component_exception(cx, "unknown encoding");
            if (need_free)
                wasm_runtime_free(encoded_bytes);
            return false;
    }

    if (encoded_byte_len > (uint32_t)MAX_STRING_BYTE_LENGTH) {
        set_component_exception(cx,
                                "encoded string byte length exceeds maximum");
        if (need_free)
            wasm_runtime_free(encoded_bytes);
        return false;
    }

    // Call realloc
    uint32 ptr = (uint32)wasm_runtime_call_realloc(cx, 0, 0, (int32_t)alignment,
                                                   (int32_t)encoded_byte_len);
    if (ptr == 0) {
        set_component_exception(cx, "Could not call cabi_realloc");
        if (need_free)
            wasm_runtime_free(encoded_bytes);
        return false;
    }

    if (ptr != align_to(ptr, alignment)
        || ((uint64_t)ptr + (uint64_t)encoded_byte_len)
               > mem->memory_data_size) {
        set_component_exception(
            cx, "store string pointer out of bounds or misaligned");
        if (need_free)
            wasm_runtime_free(encoded_bytes);
        return false;
    }

    // Write encoded bytes to memory
    if (!write_bytes_to_memory(cx, ptr, encoded_bytes, encoded_byte_len)) {
        if (need_free)
            wasm_runtime_free(encoded_bytes);
        return false;
    }

    // Clean up temporary buffer if allocated
    if (need_free) {
        wasm_runtime_free(encoded_bytes);
    }

    *out_ptr = ptr;
    *out_code_units = code_units;
    return true;
}

bool
store_string_into_range(LiftLowerContext *cx, wit_value_t str,
                        uint32_t *out_ptr, uint32_t *out_code_units)
{
    const char *src = str->value.string_value.chars;            // Byes
    uint32_t src_byte_len = str->value.string_value.size_bytes; // Size in bytes
    StringEncoding dst_encoding = get_string_encoding(cx);

    return store_string_copy(cx, src, src_byte_len, dst_encoding, out_ptr,
                             out_code_units);
}

static bool
store_string(LiftLowerContext *cx, uint32_t ptr, wit_value_t str)
{
    uint32_t begin = 0;
    uint32_t tagged_code_units = 0;

    if (!store_string_into_range(cx, str, &begin, &tagged_code_units)) {
        return false;
    }

    if (!store_int(cx, ptr, 4, begin)) {
        return false;
    }

    if (!store_int(cx, ptr + 4, 4, tagged_code_units)) {
        return false;
    }

    return true;
}

bool
lower_error_context(LiftLowerContext *cx, wit_value_t value,
                    uint32_t *out_index)
{
    uint32_t out = 0;
    void *errctx_ptr = (void *)(uintptr_t)value->value.resource_value
                           .value; // NOLINT(performance-no-int-to-ptr)
    if (!wasm_component_table_add(cx->inst->table, errctx_ptr,
                                  WASM_TABLE_ELEM_ERROR_CONTEXT, &out)) {
        return false;
    }

    *out_index = out;
    return true;
}

static bool
store_primitive_value(LiftLowerContext *cx, uint32_t ptr,
                      WASMComponentPrimValType primval, wit_value_t value)
{
    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
        {
            uint8_t bool_as_int = value->value.bool_value ? 1 : 0;
            return store_int(cx, ptr, 1, bool_as_int);
        }

        case WASM_COMP_PRIMVAL_U8:
        {
            return store_int(cx, ptr, 1, (uint64_t)value->value.u8_value);
        }

        case WASM_COMP_PRIMVAL_U16:
        {
            return store_int(cx, ptr, 2, (uint64_t)value->value.u16_value);
        }

        case WASM_COMP_PRIMVAL_U32:
        {
            return store_int(cx, ptr, 4, (uint64_t)value->value.u32_value);
        }

        case WASM_COMP_PRIMVAL_U64:
        {
            return store_int(cx, ptr, 8, value->value.u64_value);
        }

        case WASM_COMP_PRIMVAL_S8:
        {
            return store_int(cx, ptr, 1,
                             (uint64_t)(uint8_t)value->value.s8_value);
        }

        case WASM_COMP_PRIMVAL_S16:
        {
            return store_int(cx, ptr, 2,
                             (uint64_t)(uint16_t)value->value.s16_value);
        }

        case WASM_COMP_PRIMVAL_S32:
        {
            return store_int(cx, ptr, 4,
                             (uint64_t)(uint32_t)value->value.s32_value);
        }

        case WASM_COMP_PRIMVAL_S64:
        {
            return store_int(cx, ptr, 8, (uint64_t)value->value.s64_value);
        }

        case WASM_COMP_PRIMVAL_F32:
        {
            return store_int(cx, ptr, 4,
                             encode_float_as_i32(value->value.f32_value));
        }

        case WASM_COMP_PRIMVAL_F64:
        {
            return store_int(cx, ptr, 8,
                             encode_float_as_i64(value->value.f64_value));
        }

        case WASM_COMP_PRIMVAL_CHAR:
        {
            uint32_t validated = 0;
            if (!char_to_i32(cx, value->value.char_value, &validated))
                return false;
            return store_int(cx, ptr, 4, (uint64_t)validated);
        }

        case WASM_COMP_PRIMVAL_STRING:
        {
            return store_string(cx, ptr, value);
        }

        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
        {
            uint32_t out = 0;
            if (!lower_error_context(cx, value, &out)) {
                return false;
            }

            return store_int(cx, ptr, 4, (uint64_t)out);
        }

        default:
        {
            return false;
        }
    }

    return false;
}

static bool
store_list_into_valid_range(LiftLowerContext *cx, uint32_t ptr, uint32_t length,
                            WASMComponentTypeInstance *type, wit_value_t value)
{
    uint32_t i = 0;
    for (i = 0; i < length; i++) {
        if (!store(cx, ptr + (i * type->elem_size), type,
                   value->value.list_value.elems[i])) {
            set_component_exception(cx, "Could not store list");
            return false;
        }
    }

    return true;
}

bool
store_list_into_range(LiftLowerContext *cx, uint32_t length,
                      WASMComponentTypeInstance *type, uint32_t *begin_out,
                      uint32_t *length_out, wit_value_t value)
{
    uint64_t byte_length =
        (uint64_t)value->value.list_value.size * type->elem_size;
    trap_if(byte_length >= (1ULL << 32));
    const WASMMemoryInstance *mem = get_mem_from_cx(cx);

    // Call realloc
    uint32 ptr = (uint32)wasm_runtime_call_realloc(
        cx, 0, 0, (int32_t)type->alignment, (int32_t)byte_length);
    if (ptr == 0) {
        set_component_exception(cx, "Could not call cabi_realloc");
        return false;
    }

    trap_if(ptr != align_to(ptr, type->alignment));
    trap_if((uint64_t)(ptr + byte_length) > mem->memory_data_size);
    if (!store_list_into_valid_range(cx, ptr, length, type, value)) {
        return false;
    }

    *begin_out = ptr;
    *length_out = length;
    return true;
}

static bool
store_list(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
           uint32_t maybe_length, bool is_fixed_size, wit_value_t value)
{
    if (is_fixed_size) {
        bh_assert(maybe_length == value->value.list_value.size);
        return store_list_into_valid_range(cx, ptr, maybe_length, type, value);
    }

    uint32_t begin = 0, length = 0;

    if (!store_list_into_range(cx, value->value.list_value.size, type, &begin,
                               &length, value)) {
        return false;
    }

    if (!store_int(cx, ptr, 4, (uint64_t)begin)) {
        return false;
    }

    if (!store_int(cx, ptr + 4, 4, (uint64_t)length)) {
        return false;
    }

    return true;
}

static bool
store_record(LiftLowerContext *cx, uint32_t ptr,
             WASMComponentRecordInstance *type, wit_value_t value)
{
    for (uint32_t field = 0; field < type->count; field++) {
        ptr = align_to(ptr, get_alignment(type->fields[field].type));
        if (!store(cx, ptr, type->fields[field].type,
                   value->value.record_value.fields[field].value)) {
            set_component_exception(cx, "Could not store record value");
            return false;
        }

        ptr += get_elem_size(type->fields[field].type);
    }

    return true;
}

bool
match_case(LiftLowerContext *cx, const WASMComponentVariantInstance *type,
           wit_value_t value, uint32_t *case_index, wit_value_t *case_value)
{
    const char *label = value->value.variant_value.discriminator;
    uint32_t label_len = value->value.variant_value.discriminator_size;
    for (uint32_t i = 0; i < type->count; i++) {
        if (type->cases[i].label->name_len == label_len
            && strncmp(label, type->cases[i].label->name, label_len) == 0) {
            *case_value = value->value.variant_value.value;
            *case_index = i;
            return true;
        }
    }

    set_component_exception(cx, "variant case label not found");
    *case_index = 0;
    *case_value = NULL;
    return false;
}

static bool
store_variant(LiftLowerContext *cx, uint32_t ptr,
              WASMComponentVariantInstance *type, wit_value_t value)
{
    uint32_t case_index = 0;
    wit_value_t case_value = NULL;
    if (!match_case(cx, type, value, &case_index, &case_value)) {
        return false;
    }

    uint32_t disc_size = compute_discriminant_alignment(type->count);

    if (!store_int(cx, ptr, disc_size, (uint64_t)case_index)) {
        return false;
    }

    ptr += disc_size;
    ptr = align_to(ptr, compute_max_case_alignment(type));

    WASMComponentCaseValInstance c = type->cases[case_index];

    if (c.value_type != NULL) {
        return store(cx, ptr, c.value_type, case_value);
    }

    return true;
}

bool
pack_flags_into_int(LiftLowerContext *cx, const WASMComponentFlagType *labels,
                    uint64_t *out, wit_value_t value)
{
    uint64_t i = 0;
    for (uint64_t index = 0; index < labels->count; index++) {
        bool flag_val =
            value->value.flag_value.fields[index].value->value.bool_value;

        if (flag_val) {
            i |= (1ULL << index);
        }
    }

    *out = i;
    return true;
}

static bool
store_flags(LiftLowerContext *cx, uint32_t ptr,
            const WASMComponentFlagType *type, uint32_t flag_size,
            wit_value_t value)
{
    uint64_t i = 0;
    if (!pack_flags_into_int(cx, type, &i, value)) {
        return false;
    }

    return store_int(cx, ptr, flag_size, i);
}

bool
lower_own(LiftLowerContext *cx, WASMComponentResourceHandleInstance *type,
          wit_value_t value, uint32_t *out_index)
{
    // 1. Get rep from wit_value
    uint32_t rep = value->value.resource_value.value;

    // 2. Create a new own handle
    WASMResourceHandle *handle =
        wasm_create_resource_handle(type->resource, // resource type
                                    rep,            // representation
                                    true            // own = true
        );
    trap_if(handle == NULL);

    // 3. Add to table
    uint32_t index = 0;
    if (!wasm_component_table_add(cx->inst->table, handle,
                                  WASM_TABLE_ELEM_RESOURCE_HANDLE, &index)) {
        wasm_destroy_resource_handle(handle);
        set_component_exception(cx, "failed to add resource handle to table");
        return false;
    }

    *out_index = index;
    return true;
}

bool
lower_borrow(LiftLowerContext *cx, WASMComponentResourceHandleInstance *type,
             wit_value_t value, uint32_t *out_index)
{
    bh_assert(cx->borrow_scope_type == BORROW_SCOPE_TASK);

    // Check if lowering a borrow to the same component that owns the resource,
    // just pass the rep directly.
    // 1. Get rep from wit_value
    uint32_t rep = value->value.resource_value.value;

    if (cx->inst == type->resource->impl) {
        *out_index = rep;
        return true;
    }

    // 2. Create a new borrow handle
    WASMResourceHandle *handle =
        wasm_create_resource_handle(type->resource, // resource type
                                    rep,            // representation
                                    false           // own = false (borrow)
        );
    trap_if(handle == NULL);

    // 3. Add to table
    uint32_t index = 0;
    if (!wasm_component_table_add(cx->inst->table, handle,
                                  WASM_TABLE_ELEM_RESOURCE_HANDLE, &index)) {
        wasm_destroy_resource_handle(handle);
        set_component_exception(cx, "failed to add resource handle to table");
        return false;
    }

    handle->borrow_scope = cx->borrow_scope.task;
    cx->borrow_scope.task->num_borrows++;

    *out_index = index;
    return true;
}

static bool
store_tuple(LiftLowerContext *cx, uint32_t ptr,
            WASMComponentTupleInstance *type, wit_value_t value)
{
    for (uint32_t field = 0; field < type->count; field++) {
        ptr = align_to(ptr, get_alignment(type->element_types[field]));
        if (!store(cx, ptr, type->element_types[field],
                   value->value.tuple_value.elems[field])) {
            set_component_exception(cx, "Could not store tuple value");
            return false;
        }

        ptr += get_elem_size(type->element_types[field]);
    }

    return true;
}

static bool
store_enum(LiftLowerContext *cx, uint32_t ptr,
           const WASMComponentEnumType *type, wit_value_t value)
{
    uint32_t case_index = value->value.enum_value.value;
    trap_if(case_index >= type->count);

    uint32_t disc_size = compute_discriminant_alignment(type->count);

    if (!store_int(cx, ptr, disc_size, (uint64_t)case_index)) {
        return false;
    }

    return true;
}

static bool
store_option(LiftLowerContext *cx, uint32_t ptr,
             WASMComponentOptionInstance *type, wit_value_t value)
{
    // Check if optional_elem is NULL (none) or not (some)
    bool is_some = (value->value.option_value.optional_elem != NULL);
    uint32_t case_index = is_some ? 1 : 0;
    wit_value_t case_value =
        is_some ? value->value.option_value.optional_elem : NULL;

    uint32_t disc_size = compute_discriminant_alignment(2);
    if (!store_int(cx, ptr, disc_size, (uint64_t)case_index)) {
        return false;
    }

    ptr += disc_size;

    // Compute max_case_alignment
    uint32_t max_case_alignment = 1;
    if (type->element_type) {
        max_case_alignment = get_alignment(type->element_type);
    }
    ptr = align_to(ptr, max_case_alignment);

    // Store payload if present
    if (is_some && type->element_type != NULL) {
        return store(cx, ptr, type->element_type, case_value);
    }

    return true;
}

static bool
store_result(LiftLowerContext *cx, uint32_t ptr,
             WASMComponentResultInstance *type, wit_value_t value)
{
    // Check is_err to determine case
    bool is_err = value->value.result_value.is_err;
    uint32_t case_index = is_err ? 1 : 0;
    WASMComponentTypeInstance *payload_type =
        is_err ? type->error_type : type->result_type;
    wit_value_t case_value = is_err ? value->value.result_value.result.err
                                    : value->value.result_value.result.ok;

    // Write discriminant
    uint32_t disc_size = compute_discriminant_alignment(2);
    if (!store_int(cx, ptr, disc_size, (uint64_t)case_index)) {
        return false;
    }

    ptr += disc_size;

    // Compute max_case_alignment
    uint32_t max_case_alignment = 1;
    if (type->result_type) {
        max_case_alignment = get_alignment(type->result_type);
    }
    if (type->error_type) {
        uint32_t error_align = get_alignment(type->error_type);
        if (error_align > max_case_alignment) {
            max_case_alignment = error_align;
        }
    }
    ptr = align_to(ptr, max_case_alignment);

    // Store payload if present
    if (payload_type != NULL && case_value != NULL) {
        return store(cx, ptr, payload_type, case_value);
    }

    return true;
}

bool
store(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
      wit_value_t value)
{
    if (!type)
        return false;
    bh_assert(ptr == align_to(ptr, type->alignment));
    bh_assert((uint64_t)(ptr + type->elem_size)
              <= get_mem_from_cx(cx)->memory_data_size);

    switch (type->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
        {
            return store_primitive_value(cx, ptr, type->type_specific.primval,
                                         value);
        }

        case COMPONENT_VAL_TYPE_LIST:
        {
            return store_list(cx, ptr, type->type_specific.list->element_type,
                              0, false, value);
        }

        case COMPONENT_VAL_TYPE_FIXED_SIZE_LIST:
        {
            return store_list(cx, ptr,
                              type->type_specific.list_len->element_type,
                              type->type_specific.list_len->len, true, value);
        }

        case COMPONENT_VAL_TYPE_RECORD:
        {
            return store_record(cx, ptr, type->type_specific.record, value);
        }

        case COMPONENT_VAL_TYPE_VARIANT:
        {
            return store_variant(cx, ptr, type->type_specific.variant, value);
        }

        case COMPONENT_VAL_TYPE_FLAGS:
        {
            return store_flags(cx, ptr, type->type_specific.flag,
                               type->elem_size, value);
        }

        case COMPONENT_VAL_TYPE_OWN:
        {
            uint32_t index = 0;
            if (!lower_own(cx, type->type_specific.resource_handle, value,
                           &index)) {
                return false;
            }
            return store_int(cx, ptr, 4, (uint64_t)index);
        }

        case COMPONENT_VAL_TYPE_BORROW:
        {
            uint32_t index = 0;
            if (!lower_borrow(cx, type->type_specific.resource_handle, value,
                              &index)) {
                return false;
            }
            return store_int(cx, ptr, 4, (uint64_t)index);
        }

        case COMPONENT_VAL_TYPE_TUPLE:
        {
            return store_tuple(cx, ptr, type->type_specific.tuple, value);
        }

        case COMPONENT_VAL_TYPE_ENUM:
        {
            return store_enum(cx, ptr, type->type_specific.enum_type, value);
        }

        case COMPONENT_VAL_TYPE_OPTION:
        {
            return store_option(cx, ptr, type->type_specific.option, value);
        }

        case COMPONENT_VAL_TYPE_RESULT:
        {
            return store_result(cx, ptr, type->type_specific.result, value);
        }

        default:
        {
            set_component_exception(cx, "invalid type to store");
            return false;
        }
    }

    return false;
}

CanonicalOptions *
convert_canon_opts_to_runtime(WASMComponentCanonOptsInstance *parsed_opts,
                              char *error_buf, uint32 error_buf_size)
{
    if (!parsed_opts) {
        return NULL;
    }

    CanonicalOptions *opts = wasm_runtime_malloc(sizeof(CanonicalOptions));
    if (!opts) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "failed to allocate CanonicalOptions");
        return NULL;
    }

    // Initialize nested structures
    opts->lift_lower_opts = wasm_runtime_malloc(sizeof(LiftLowerOptions));
    if (!opts->lift_lower_opts) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "failed to allocate LiftLowerOptions");
        wasm_runtime_free(opts);
        return NULL;
    }

    opts->lift_lower_opts->lift_opts = wasm_runtime_malloc(sizeof(LiftOptions));
    if (!opts->lift_lower_opts->lift_opts) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "failed to allocate LiftOptions");
        wasm_runtime_free(opts->lift_lower_opts);
        wasm_runtime_free(opts);
        return NULL;
    }

    // Set defaults
    opts->lift_lower_opts->lift_opts->string_encoding = ENCODING_UTF_8;
    opts->lift_lower_opts->lift_opts->memory = NULL;
    opts->lift_lower_opts->realloc_func = NULL;
    opts->post_return_func = NULL;
    opts->async = false; // default to sync
    opts->callback_func = NULL;

    // Iterate through options and populate runtime structure
    for (uint32_t i = 0; i < parsed_opts->canon_opts_count; i++) {
        WASMComponentCanonOptInstance *opt = &parsed_opts->canon_opts[i];

        switch (opt->tag) {
            case WASM_COMP_CANON_OPT_MEMORY:
                opts->lift_lower_opts->lift_opts->memory = opt->payload.memory;
                break;
            case WASM_COMP_CANON_OPT_REALLOC:
                opts->lift_lower_opts->realloc_func = opt->payload.realloc_func;
                break;
            case WASM_COMP_CANON_OPT_POST_RETURN:
                opts->post_return_func = opt->payload.post_return_func;
                break;
            case WASM_COMP_CANON_OPT_ASYNC:
                opts->async = true;
                break;
            case WASM_COMP_CANON_OPT_CALLBACK:
                opts->callback_func = opt->payload.callback_func;
                opts->async = true;
                break;
            case WASM_COMP_CANON_OPT_STRING_UTF8:
                opts->lift_lower_opts->lift_opts->string_encoding =
                    ENCODING_UTF_8;
                break;
            case WASM_COMP_CANON_OPT_STRING_UTF16:
                opts->lift_lower_opts->lift_opts->string_encoding =
                    ENCODING_UTF_16;
                break;
            case WASM_COMP_CANON_OPT_STRING_LATIN1_UTF16:
                opts->lift_lower_opts->lift_opts->string_encoding =
                    ENCODING_LATIN_1_UTF_16;
                break;

            default:
                continue;
        }
    }

    return opts;
}

void
free_canonical_options(CanonicalOptions *opts)
{
    if (!opts) {
        return;
    }

    if (opts->lift_lower_opts) {
        if (opts->lift_lower_opts->lift_opts) {
            wasm_runtime_free(opts->lift_lower_opts->lift_opts);
        }
        wasm_runtime_free(opts->lift_lower_opts);
    }

    wasm_runtime_free(opts);
}

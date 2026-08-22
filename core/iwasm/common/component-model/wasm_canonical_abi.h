/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_CANONICAL_ABI_H
#define WASM_CANONICAL_ABI_H

#include <stdint.h>
#include <stdbool.h>
#include "wasm_component.h"
#include "wasm_component_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ComponentWITValue *wit_value_t;

typedef enum StringEncoding {
    ENCODING_UTF_8 = 0,
    ENCODING_UTF_16 = 1,
    ENCODING_LATIN_1_UTF_16 = 2
} StringEncoding;

typedef enum StringDecoding {
    DECODING_UTF_8 = 0,
    DECODING_UTF_16_LE = 1,
    DECODING_LATIN_1 = 2
} StringDecoding;

typedef struct ComponentWITString {
    char *chars;
    StringEncoding hint_encoding;
    uint32_t size_bytes;
    uint32_t tagged_code_units;
} ComponentWITString;

typedef struct ComponentWITList {
    wit_value_t *elems;
    uint32_t size;
} ComponentWITList;

typedef struct ComponentWITOption {
    wit_value_t optional_elem;
} ComponentWITOption;

typedef struct ComponentWITResult {
    bool is_err;
    union {
        wit_value_t ok;
        wit_value_t err;
    } result;
} ComponentWITResult;

typedef struct ComponentWITRecordField {
    char *key;
    uint32_t key_size;
    wit_value_t value;
} ComponentWITRecordField;

typedef struct ComponentWITRecord {
    ComponentWITRecordField *fields;
    uint32_t size;
} ComponentWITRecord;

typedef struct ComponentWITTuple {
    wit_value_t *elems;
    uint32_t size;
} ComponentWITTuple;

typedef struct ComponentWITVariant {
    char *discriminator;
    uint32_t discriminator_size;
    wit_value_t value;
} ComponentWITVariant;

typedef struct ComponentWITEnum {
    uint32_t value;
} ComponentWITEnum;

typedef struct ComponentWITFlag {
    ComponentWITRecordField *fields;
    uint32_t size;
} ComponentWITFlag;

typedef struct ComponentWITResource {
    uint32_t value;
} ComponentWITResource;

typedef union ComponentWITUVal {
    bool bool_value;
    int8_t s8_value;
    int16_t s16_value;
    int32_t s32_value;
    int64_t s64_value;
    uint8_t u8_value;
    uint16_t u16_value;
    uint32_t u32_value;
    uint64_t u64_value;
    float f32_value;
    double f64_value;
    uint32_t char_value;

    ComponentWITString string_value;
    ComponentWITList list_value;
    ComponentWITOption option_value;
    ComponentWITResult result_value;
    ComponentWITRecordField record_field_value;
    ComponentWITRecord record_value;
    ComponentWITTuple tuple_value;
    ComponentWITVariant variant_value;
    ComponentWITEnum enum_value;
    ComponentWITFlag flag_value;
    ComponentWITResource resource_value;
} ComponentWITUVal;

struct ComponentWITValue {
    WASMComponentValType type;
    WASMComponentPrimValType prim_type;
    ComponentWITUVal value;
};

/**
 * @brief Allocates raw memory for a new WIT value structure.
 * @return wit_value_t Pointer to the allocated memory, or NULL if allocation
 * fails.
 */
wit_value_t
wit_value_alloc(void);

/**
 * @brief Recursively deallocates a WIT value and all its nested children.
 * @param value The WIT value to be freed.
 * @return bool True if the value was successfully freed, false otherwise.
 */
bool
free_wit_value(wit_value_t value);

/**
 * @brief Constructs a boolean WIT value.
 * @param val The boolean value to wrap.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_bool_ctor(bool val);

/**
 * @brief Constructs a signed 8-bit integer WIT value.
 * @param val The s8 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_s8_ctor(int8_t val);

/**
 * @brief Constructs a signed 16-bit integer WIT value.
 * @param val The s16 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_s16_ctor(int16_t val);

/**
 * @brief Constructs a signed 32-bit integer WIT value.
 * @param val The s32 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_s32_ctor(int32_t val);

/**
 * @brief Constructs a signed 64-bit integer WIT value.
 * @param val The s64 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_s64_ctor(int64_t val);

/**
 * @brief Constructs an unsigned 8-bit integer WIT value.
 * @param val The u8 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_u8_ctor(uint8_t val);

/**
 * @brief Constructs an unsigned 16-bit integer WIT value.
 * @param val The u16 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_u16_ctor(uint16_t val);

/**
 * @brief Constructs an unsigned 32-bit integer WIT value.
 * @param val The u32 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_u32_ctor(uint32_t val);

/**
 * @brief Constructs an unsigned 64-bit integer WIT value.
 * @param val The u64 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_u64_ctor(uint64_t val);

/**
 * @brief Constructs a 32-bit float WIT value.
 * @param val The f32 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_f32_ctor(float val);

/**
 * @brief Constructs a 64-bit float WIT value.
 * @param val The f64 value.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_f64_ctor(double val);

/**
 * @brief Constructs a Unicode character WIT value.
 * @param val The 32-bit character code.
 * @return wit_value_t The constructed WIT value or NULL on failure.
 */
wit_value_t
wit_char_ctor(uint32_t val);

/**
 * @brief Constructs a WIT list, validating that all elements are of the same
 * type.
 * @param elems Array of pointers to WIT values.
 * @param size Number of elements in the list.
 * @return wit_value_t The constructed list or NULL if validation fails.
 */
wit_value_t
wit_list_ctor(wit_value_t *elems, uint32_t size);

/**
 * @brief Constructs a WIT string by performing a deep copy of the input buffer.
 * @param string The source string buffer.
 * @param size_bytes Length of the string in bytes.
 * @param tagged_code_units ABI unit count.
 * @param hint_encoding The suggested string encoding.
 * @return wit_value_t The constructed string or NULL on failure.
 */
wit_value_t
wit_string_ctor(char *string, uint32_t size_bytes, uint32_t tagged_code_units,
                StringEncoding hint_encoding);

/**
 * @brief Wraps a WIT value in an Option (representing 'some' or 'none').
 * @param val The value to wrap (can be NULL for 'none').
 * @return wit_value_t The constructed option.
 */
wit_value_t
wit_option_ctor(wit_value_t val);

/**
 * @brief Constructs a WIT result value (representing 'ok' or 'error').
 * @param is_err True if this is an error case, false for ok.
 * @param value The payload value for the result.
 * @return wit_value_t The constructed result.
 */
wit_value_t
wit_result_ctor(bool is_err, wit_value_t value);

/**
 * @brief Initializes a single field for a WIT record.
 * @param field Pointer to the field structure to initialize.
 * @param key The field label name.
 * @param key_size Length of the key name.
 * @param value The WIT value associated with the key.
 */
void
init_record_field(ComponentWITRecordField *field, char *key, uint32_t key_size,
                  wit_value_t value);

/**
 * @brief Constructs a WIT record from an array of initialized fields.
 * @param fields Array of initialized record fields.
 * @param size Number of fields in the record.
 * @return wit_value_t The constructed record.
 */
wit_value_t
wit_record_ctor(ComponentWITRecordField *fields, uint32_t size);

/**
 * @brief Constructs a WIT tuple.
 * @param elems Array of pointers to WIT values.
 * @param size Number of elements in the tuple.
 * @return wit_value_t The constructed tuple.
 */
wit_value_t
wit_tuple_ctor(wit_value_t *elems, uint32_t size);

/**
 * @brief Constructs a WIT variant value.
 * @param discriminator The case label string.
 * @param discriminator_size Length of the label string.
 * @param value The payload value for the specific variant case.
 * @return wit_value_t The constructed variant.
 */
wit_value_t
wit_variant_ctor(char *discriminator, uint32_t discriminator_size,
                 wit_value_t value);

/**
 * @brief Constructs a WIT enum value.
 * @param value The numeric index of the enum case.
 * @return wit_value_t The constructed enum.
 */
wit_value_t
wit_enum_ctor(uint32_t value);

/**
 * @brief Constructs WIT flags from an array of boolean fields.
 * @param fields Array of boolean fields representing individual flags.
 * @param size Number of flags.
 * @return wit_value_t The constructed flags.
 */
wit_value_t
wit_flag_ctor(ComponentWITRecordField *fields, uint32_t size);

/**
 * @brief Constructs a WIT resource handle.
 * @param value The numeric representation of the resource.
 * @return wit_value_t The constructed resource handle.
 */
wit_value_t
wit_resource_ctor(uint32_t value);

/**
 * @brief Constructs an error context value.
 * @param value The numeric representation of the error context.
 * @return wit_value_t The constructed error context.
 */
wit_value_t
wit_errctx_ctor(uint32_t value);

/**
 * @brief Constructs a WIT list WITHOUT type homogeneity validation.
 *
 * Unlike wit_list_ctor(), this function does NOT validate that all elements
 * have the same type. This is needed for parameter/result lists which can
 * contain heterogeneous types (e.g., func(a: u32, b: u64, c: string)).
 *
 * Use wit_list_ctor() for actual WIT list types (which are homogeneous by
 * spec). Use this function for internal parameter/result value passing.
 *
 * @param elems Array of pointers to WIT values (can be heterogeneous).
 * @param size Number of elements in the list.
 * @return wit_value_t The constructed list or NULL on allocation failure.
 */
wit_value_t
wit_values_list_ctor(wit_value_t *elems, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif

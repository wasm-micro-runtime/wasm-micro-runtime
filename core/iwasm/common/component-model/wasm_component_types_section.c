/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include "wasm_component_runtime.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wasm_loader_common.h"
#include "wasm_runtime_common.h"
#include "wasm_export.h"
#include <stdio.h>

// Static primitive type instances
static WASMComponentTypeInstance primitive_type_bool = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 1,
    .elem_size = 1,
    .type_specific.primval = WASM_COMP_PRIMVAL_BOOL
};

static WASMComponentTypeInstance primitive_type_s8 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 1,
    .elem_size = 1,
    .type_specific.primval = WASM_COMP_PRIMVAL_S8
};

static WASMComponentTypeInstance primitive_type_u8 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 1,
    .elem_size = 1,
    .type_specific.primval = WASM_COMP_PRIMVAL_U8
};

static WASMComponentTypeInstance primitive_type_s16 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 2,
    .elem_size = 2,
    .type_specific.primval = WASM_COMP_PRIMVAL_S16
};

static WASMComponentTypeInstance primitive_type_u16 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 2,
    .elem_size = 2,
    .type_specific.primval = WASM_COMP_PRIMVAL_U16
};

static WASMComponentTypeInstance primitive_type_s32 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 4,
    .type_specific.primval = WASM_COMP_PRIMVAL_S32
};

static WASMComponentTypeInstance primitive_type_u32 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 4,
    .type_specific.primval = WASM_COMP_PRIMVAL_U32
};

static WASMComponentTypeInstance primitive_type_s64 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 8,
    .elem_size = 8,
    .type_specific.primval = WASM_COMP_PRIMVAL_S64
};

static WASMComponentTypeInstance primitive_type_u64 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 8,
    .elem_size = 8,
    .type_specific.primval = WASM_COMP_PRIMVAL_U64
};

static WASMComponentTypeInstance primitive_type_f32 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 4,
    .type_specific.primval = WASM_COMP_PRIMVAL_F32
};

static WASMComponentTypeInstance primitive_type_f64 = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 8,
    .elem_size = 8,
    .type_specific.primval = WASM_COMP_PRIMVAL_F64
};

static WASMComponentTypeInstance primitive_type_char = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 4,
    .type_specific.primval = WASM_COMP_PRIMVAL_CHAR
};

static WASMComponentTypeInstance primitive_type_string = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 8,
    .type_specific.primval = WASM_COMP_PRIMVAL_STRING
};

static WASMComponentTypeInstance primitive_type_error_context = {
    .type = COMPONENT_VAL_TYPE_PRIMVAL,
    .alignment = 4,
    .elem_size = 4,
    .type_specific.primval = WASM_COMP_PRIMVAL_ERROR_CONTEXT
};

static void
free_component_instance_decl(WASMComponentInstDecl *decl);

// Free helpers for nested component/instance/resource types
static void
free_component_types_entry(WASMComponentTypes *type);

// Helper function to get static primitive type instance
static inline WASMComponentTypeInstance *
get_primitive_type_instance(WASMComponentPrimValType primval)
{
    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
            return &primitive_type_bool;
        case WASM_COMP_PRIMVAL_S8:
            return &primitive_type_s8;
        case WASM_COMP_PRIMVAL_U8:
            return &primitive_type_u8;
        case WASM_COMP_PRIMVAL_S16:
            return &primitive_type_s16;
        case WASM_COMP_PRIMVAL_U16:
            return &primitive_type_u16;
        case WASM_COMP_PRIMVAL_S32:
            return &primitive_type_s32;
        case WASM_COMP_PRIMVAL_U32:
            return &primitive_type_u32;
        case WASM_COMP_PRIMVAL_S64:
            return &primitive_type_s64;
        case WASM_COMP_PRIMVAL_U64:
            return &primitive_type_u64;
        case WASM_COMP_PRIMVAL_F32:
            return &primitive_type_f32;
        case WASM_COMP_PRIMVAL_F64:
            return &primitive_type_f64;
        case WASM_COMP_PRIMVAL_CHAR:
            return &primitive_type_char;
        case WASM_COMP_PRIMVAL_STRING:
            return &primitive_type_string;
        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
            return &primitive_type_error_context;
        default:
            return NULL;
    }
}

// Helper function to assign type instance pointer based on value definition
static inline void
assign_type_instance(WASMComponentTypeInstance **val_instance,
                     WASMComponentValueType *val_definition,
                     WASMComponentTypeInstance **types)
{
    if (!val_definition) {
        *val_instance = NULL;
    }
    else if (val_definition->type == WASM_COMP_VAL_TYPE_PRIMVAL) {
        *val_instance = get_primitive_type_instance(
            val_definition->type_specific.primval_type);
    }
    else {
        *val_instance = types[val_definition->type_specific.type_idx];
    }
}

// Helper function to parse record types
static bool
parse_record_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Parse vec<labelvaltype>
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse variant type");
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0) {
        // Allocate the record structure
        (*out)->def_val.record =
            wasm_runtime_malloc(sizeof(WASMComponentRecordType));
        if (!(*out)->def_val.record) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for record type");
            return false;
        }

        // Allocate the fields array
        (*out)->def_val.record->fields =
            wasm_runtime_malloc(sizeof(WASMComponentLabelValType) * count);
        if (!(*out)->def_val.record->fields) {
            wasm_runtime_free((*out)->def_val.record);
            (*out)->def_val.record = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for record fields");
            return false;
        }

        (*out)->def_val.record->count = count;

        // Initialize all fields to zero
        memset((*out)->def_val.record->fields, 0,
               sizeof(WASMComponentLabelValType) * count);

        // Parse each field
        for (uint32_t i = 0; i < count; i++) {
            if (!parse_labelvaltype(&p, end, &(*out)->def_val.record->fields[i],
                                    error_buf, error_buf_size)) {
                // Clean up already parsed fields
                for (uint32_t j = 0; j < i; j++) {
                    free_labelvaltype(&(*out)->def_val.record->fields[j]);
                }
                wasm_runtime_free((*out)->def_val.record->fields);
                wasm_runtime_free((*out)->def_val.record);
                (*out)->def_val.record = NULL;
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse record type");
                return false;
            }
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "record type must have at least one field");
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse variant types
static bool
parse_variant_type(const uint8_t **payload, const uint8_t *end,
                   WASMComponentDefValType **out, char *error_buf,
                   uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Parse vec<casevaltype>
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0) {
        // Allocate the variant structure
        (*out)->def_val.variant =
            wasm_runtime_malloc(sizeof(WASMComponentVariantType));
        if (!(*out)->def_val.variant) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for variant type");
            return false;
        }

        // Allocate the cases array
        (*out)->def_val.variant->cases =
            wasm_runtime_malloc(sizeof(WASMComponentCaseValType) * count);
        if (!(*out)->def_val.variant->cases) {
            wasm_runtime_free((*out)->def_val.variant);
            (*out)->def_val.variant = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for variant cases");
            return false;
        }

        (*out)->def_val.variant->count = count;

        // Initialize all cases to zero
        memset((*out)->def_val.variant->cases, 0,
               sizeof(WASMComponentCaseValType) * count);

        // Parse each case
        for (uint32_t i = 0; i < count; i++) {
            if (!parse_case(&p, end, &(*out)->def_val.variant->cases[i],
                            error_buf, error_buf_size)) {
                // Clean up already parsed cases
                for (uint32_t j = 0; j < i; j++) {
                    free_case(&(*out)->def_val.variant->cases[j]);
                }
                wasm_runtime_free((*out)->def_val.variant->cases);
                wasm_runtime_free((*out)->def_val.variant);
                (*out)->def_val.variant = NULL;
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse variant type");
                return false;
            }
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "variant type must have at least one field");
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse tuple types
static bool
parse_tuple_type(const uint8_t **payload, const uint8_t *end,
                 WASMComponentDefValType **out, char *error_buf,
                 uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Parse vec<valtype>
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0) {
        // Allocate the tuple structure
        (*out)->def_val.tuple =
            wasm_runtime_malloc(sizeof(WASMComponentTupleType));
        if (!(*out)->def_val.tuple) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for tuple type");
            return false;
        }

        // Allocate the element types array
        (*out)->def_val.tuple->element_types =
            wasm_runtime_malloc(sizeof(WASMComponentValueType) * count);
        if (!(*out)->def_val.tuple->element_types) {
            wasm_runtime_free((*out)->def_val.tuple);
            (*out)->def_val.tuple = NULL;
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Failed to allocate memory for tuple element types");
            return false;
        }

        (*out)->def_val.tuple->count = count;

        // Initialize all element types to zero
        memset((*out)->def_val.tuple->element_types, 0,
               sizeof(WASMComponentValueType) * count);

        // Parse each element type
        for (uint32_t i = 0; i < count; i++) {
            if (!parse_valtype(&p, end,
                               &(*out)->def_val.tuple->element_types[i],
                               error_buf, error_buf_size)) {
                wasm_runtime_free((*out)->def_val.tuple->element_types);
                wasm_runtime_free((*out)->def_val.tuple);
                (*out)->def_val.tuple = NULL;
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse tuple element type %u", i);
                return false;
            }
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "tuple type must have at least one field");
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse vec<label'> for flags and enum types
static bool
parse_flags_type(const uint8_t **payload, const uint8_t *end,
                 WASMComponentDefValType **out, char *error_buf,
                 uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Parse vec<label'>
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0 && count <= 32) {
        // Allocate the flag structure
        (*out)->def_val.flag =
            wasm_runtime_malloc(sizeof(WASMComponentFlagType));
        if (!(*out)->def_val.flag) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for flag type");
            return false;
        }

        // Allocate the labels array
        (*out)->def_val.flag->flags =
            wasm_runtime_malloc(sizeof(WASMComponentCoreName) * count);
        if (!(*out)->def_val.flag->flags) {
            wasm_runtime_free((*out)->def_val.flag);
            (*out)->def_val.flag = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for flag labels");
            return false;
        }

        (*out)->def_val.flag->count = count;

        // Initialize all labels to zero
        memset((*out)->def_val.flag->flags, 0,
               sizeof(WASMComponentCoreName) * count);

        // Parse each label
        for (uint32_t i = 0; i < count; i++) {
            WASMComponentCoreName *temp_ptr = &(*out)->def_val.flag->flags[i];
            if (!parse_label_prime(&p, end, &temp_ptr, error_buf,
                                   error_buf_size)) {
                // Clean up on error
                for (uint32_t j = 0; j < i; j++) {
                    free_label_prime(&(*out)->def_val.flag->flags[j]);
                }
                wasm_runtime_free((*out)->def_val.flag->flags);
                wasm_runtime_free((*out)->def_val.flag);
                (*out)->def_val.flag = NULL;
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse flag label %u", i);
                return false;
            }
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Flags type must have 0 < count <= 32, got %u", count);
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse vec<label'> for enum types
static bool
parse_enum_type(const uint8_t **payload, const uint8_t *end,
                WASMComponentDefValType **out, char *error_buf,
                uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Parse vec<label'>
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0) {
        // Allocate the enum structure
        (*out)->def_val.enum_type =
            wasm_runtime_malloc(sizeof(WASMComponentEnumType));
        if (!(*out)->def_val.enum_type) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for enum type");
            return false;
        }

        // Allocate the labels array
        (*out)->def_val.enum_type->labels =
            wasm_runtime_malloc(sizeof(WASMComponentCoreName) * count);
        if (!(*out)->def_val.enum_type->labels) {
            wasm_runtime_free((*out)->def_val.enum_type);
            (*out)->def_val.enum_type = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for enum labels");
            return false;
        }

        (*out)->def_val.enum_type->count = count;

        // Initialize all labels to zero
        memset((*out)->def_val.enum_type->labels, 0,
               sizeof(WASMComponentCoreName) * count);

        // Parse each label
        for (uint32_t i = 0; i < count; i++) {
            WASMComponentCoreName *temp_ptr =
                &(*out)->def_val.enum_type->labels[i];
            if (!parse_label_prime(&p, end, &temp_ptr, error_buf,
                                   error_buf_size)) {
                // Clean up on error
                for (uint32_t j = 0; j < i; j++) {
                    free_label_prime(&(*out)->def_val.enum_type->labels[j]);
                }
                wasm_runtime_free((*out)->def_val.enum_type->labels);
                wasm_runtime_free((*out)->def_val.enum_type);
                (*out)->def_val.enum_type = NULL;
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse enum label %u", i);
                return false;
            }
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "enum type must have at least one field");
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse option types
static bool
parse_option_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate the option structure
    (*out)->def_val.option =
        wasm_runtime_malloc(sizeof(WASMComponentOptionType));
    if (!(*out)->def_val.option) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for option type");
        return false;
    }

    // Allocate the element type
    (*out)->def_val.option->element_type =
        wasm_runtime_malloc(sizeof(WASMComponentValueType));
    if (!(*out)->def_val.option->element_type) {
        wasm_runtime_free((*out)->def_val.option);
        (*out)->def_val.option = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for option element type");
        return false;
    }

    // Initialize element type to zero
    memset((*out)->def_val.option->element_type, 0,
           sizeof(WASMComponentValueType));

    // Parse option type
    if (!parse_valtype(&p, end, (*out)->def_val.option->element_type, error_buf,
                       error_buf_size)) {
        wasm_runtime_free((*out)->def_val.option->element_type);
        wasm_runtime_free((*out)->def_val.option);
        (*out)->def_val.option = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse option type");
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse result types
static bool
parse_result_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate the result structure
    (*out)->def_val.result =
        wasm_runtime_malloc(sizeof(WASMComponentResultType));
    if (!(*out)->def_val.result) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for result type");
        return false;
    }

    // Initialize to NULL
    (*out)->def_val.result->result_type = NULL;
    (*out)->def_val.result->error_type = NULL;

    // Parse in Binary.md order: result t? then error u?
    // Optional result type (t?)
    uint8_t has_result_type = *p++;
    if (has_result_type == WASM_COMP_OPTIONAL_TRUE) {
        (*out)->def_val.result->result_type =
            wasm_runtime_malloc(sizeof(WASMComponentValueType));
        if (!(*out)->def_val.result->result_type) {
            wasm_runtime_free((*out)->def_val.result);
            (*out)->def_val.result = NULL;
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Failed to allocate memory for result result type");
            return false;
        }
        memset((*out)->def_val.result->result_type, 0,
               sizeof(WASMComponentValueType));
        if (!parse_valtype(&p, end, (*out)->def_val.result->result_type,
                           error_buf, error_buf_size)) {
            wasm_runtime_free((*out)->def_val.result->result_type);
            wasm_runtime_free((*out)->def_val.result);
            (*out)->def_val.result = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse result result type");
            return false;
        }
    }
    else if (has_result_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_result_type);
        return false;
    }

    // Optional error type (u?)
    uint8_t has_error_type = *p++;
    if (has_error_type == WASM_COMP_OPTIONAL_TRUE) {
        (*out)->def_val.result->error_type =
            wasm_runtime_malloc(sizeof(WASMComponentValueType));
        if (!(*out)->def_val.result->error_type) {
            if ((*out)->def_val.result->result_type) {
                wasm_runtime_free((*out)->def_val.result->result_type);
            }
            wasm_runtime_free((*out)->def_val.result);
            (*out)->def_val.result = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for result error type");
            return false;
        }
        memset((*out)->def_val.result->error_type, 0,
               sizeof(WASMComponentValueType));
        if (!parse_valtype(&p, end, (*out)->def_val.result->error_type,
                           error_buf, error_buf_size)) {
            wasm_runtime_free((*out)->def_val.result->error_type);
            if ((*out)->def_val.result->result_type) {
                wasm_runtime_free((*out)->def_val.result->result_type);
            }
            wasm_runtime_free((*out)->def_val.result);
            (*out)->def_val.result = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse result error type");
            return false;
        }
    }
    else if (has_error_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_error_type);
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse own types
static bool
parse_own_type(const uint8_t **payload, const uint8_t *end,
               WASMComponentDefValType **out, char *error_buf,
               uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    (*out)->def_val.owned = wasm_runtime_malloc(sizeof(WASMComponentOwnType));
    if (!(*out)->def_val.owned) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for own type");
        return false;
    }

    // Parse own type
    uint64_t type_idx_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx_leb, error_buf,
                  error_buf_size)) {
        wasm_runtime_free((*out)->def_val.owned);
        (*out)->def_val.owned = NULL;
        set_error_buf_ex(error_buf, error_buf_size, "Failed to parse own type");
        return false;
    }
    (*out)->def_val.owned->type_idx = (uint32_t)type_idx_leb;

    *payload = p;
    return true;
}

// Helper function to parse borrow types
// 0x68 i:<typeidx> => (borrow i)
static bool
parse_borrow_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    (*out)->def_val.borrow =
        wasm_runtime_malloc(sizeof(WASMComponentBorrowType));
    if (!(*out)->def_val.borrow) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for borrow type");
        return false;
    }

    // Parse borrow type
    uint64_t type_idx_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx_leb, error_buf,
                  error_buf_size)) {
        wasm_runtime_free((*out)->def_val.borrow);
        (*out)->def_val.borrow = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse borrow type");
        return false;
    }
    (*out)->def_val.borrow->type_idx = (uint32_t)type_idx_leb;

    *payload = p;
    return true;
}

// Helper function to parse stream types
static bool
parse_stream_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate the stream structure
    (*out)->def_val.stream =
        wasm_runtime_malloc(sizeof(WASMComponentStreamType));
    if (!(*out)->def_val.stream) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for stream type");
        return false;
    }

    // Initialize to NULL
    (*out)->def_val.stream->element_type = NULL;

    // Parse optional element type
    uint8_t has_element_type = *p++;
    if (has_element_type == WASM_COMP_OPTIONAL_TRUE) {
        // Allocate the element type
        (*out)->def_val.stream->element_type =
            wasm_runtime_malloc(sizeof(WASMComponentValueType));
        if (!(*out)->def_val.stream->element_type) {
            wasm_runtime_free((*out)->def_val.stream);
            (*out)->def_val.stream = NULL;
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Failed to allocate memory for stream element type");
            return false;
        }

        // Initialize element type to zero
        memset((*out)->def_val.stream->element_type, 0,
               sizeof(WASMComponentValueType));

        if (!parse_valtype(&p, end, (*out)->def_val.stream->element_type,
                           error_buf, error_buf_size)) {
            wasm_runtime_free((*out)->def_val.stream->element_type);
            wasm_runtime_free((*out)->def_val.stream);
            (*out)->def_val.stream = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse stream element type");
            return false;
        }
    }
    else if (has_element_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_element_type);
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to parse future types
static bool
parse_future_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentDefValType **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate the future structure
    (*out)->def_val.future =
        wasm_runtime_malloc(sizeof(WASMComponentFutureType));
    if (!(*out)->def_val.future) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for future type");
        return false;
    }

    // Initialize to NULL
    (*out)->def_val.future->element_type = NULL;

    // Parse optional element type
    uint8_t has_element_type = *p++;
    if (has_element_type == WASM_COMP_OPTIONAL_TRUE) {
        // Allocate the element type
        (*out)->def_val.future->element_type =
            wasm_runtime_malloc(sizeof(WASMComponentValueType));
        if (!(*out)->def_val.future->element_type) {
            wasm_runtime_free((*out)->def_val.future);
            (*out)->def_val.future = NULL;
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Failed to allocate memory for future element type");
            return false;
        }

        // Initialize element type to zero
        memset((*out)->def_val.future->element_type, 0,
               sizeof(WASMComponentValueType));

        if (!parse_valtype(&p, end, (*out)->def_val.future->element_type,
                           error_buf, error_buf_size)) {
            wasm_runtime_free((*out)->def_val.future->element_type);
            wasm_runtime_free((*out)->def_val.future);
            (*out)->def_val.future = NULL;
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse future element type");
            return false;
        }
    }
    else if (has_element_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_element_type);
        return false;
    }

    *payload = p;
    return true;
}

// Validation wrapper for parse_list_type with len
static bool
parse_list_type_with_len(const uint8_t **payload, const uint8_t *end,
                         WASMComponentDefValType **out, char *error_buf,
                         uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate the list_len structure
    (*out)->def_val.list_len =
        wasm_runtime_malloc(sizeof(WASMComponentListLenType));
    if (!(*out)->def_val.list_len) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for list_len type");
        return false;
    }

    // Allocate the element_type
    (*out)->def_val.list_len->element_type =
        wasm_runtime_malloc(sizeof(WASMComponentValueType));
    if (!(*out)->def_val.list_len->element_type) {
        wasm_runtime_free((*out)->def_val.list_len);
        (*out)->def_val.list_len = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for list_len element type");
        return false;
    }

    // Parse the length
    uint64_t len_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &len_leb, error_buf,
                  error_buf_size)) {
        wasm_runtime_free((*out)->def_val.list_len->element_type);
        wasm_runtime_free((*out)->def_val.list_len);
        (*out)->def_val.list_len = NULL;
        return false;
    }

    // Binary.md: "if len > 0" - fixed-size list length must be greater than 0
    if (len_leb == 0) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Fixed-size list length must be greater than 0");
        wasm_runtime_free((*out)->def_val.list_len->element_type);
        wasm_runtime_free((*out)->def_val.list_len);
        (*out)->def_val.list_len = NULL;
        return false;
    }

    if (!parse_valtype(&p, end, (*out)->def_val.list_len->element_type,
                       error_buf, error_buf_size)) {
        wasm_runtime_free((*out)->def_val.list_len->element_type);
        wasm_runtime_free((*out)->def_val.list_len);
        (*out)->def_val.list_len = NULL;
        return false;
    }

    (*out)->def_val.list_len->len = (uint32_t)len_leb;

    *payload = p;
    return true;
}

// Validation wrapper for parse_list_type without len
static bool
parse_list_type(const uint8_t **payload, const uint8_t *end,
                WASMComponentDefValType **out, char *error_buf,
                uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;
    // Allocate the list structure
    (*out)->def_val.list = wasm_runtime_malloc(sizeof(WASMComponentListType));
    if (!(*out)->def_val.list) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for list type");
        return false;
    }

    // Allocate the element_type
    (*out)->def_val.list->element_type =
        wasm_runtime_malloc(sizeof(WASMComponentValueType));
    if (!(*out)->def_val.list->element_type) {
        wasm_runtime_free((*out)->def_val.list);
        (*out)->def_val.list = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for list element type");
        return false;
    }

    if (!parse_valtype(&p, end, (*out)->def_val.list->element_type, error_buf,
                       error_buf_size)) {
        wasm_runtime_free((*out)->def_val.list->element_type);
        wasm_runtime_free((*out)->def_val.list);
        (*out)->def_val.list = NULL;
        return false;
    }

    *payload = p;
    return true;
}

// Helper function to free a defvaltype structure
static void
free_defvaltype(WASMComponentDefValType *def_val_type)
{
    if (!def_val_type) {
        return;
    }

    switch (def_val_type->tag) {
        case WASM_COMP_DEF_VAL_RECORD:
        {
            if (def_val_type->def_val.record) {
                if (def_val_type->def_val.record->fields) {
                    for (uint32_t i = 0;
                         i < def_val_type->def_val.record->count; i++) {
                        free_labelvaltype(
                            &def_val_type->def_val.record->fields[i]);
                    }
                    wasm_runtime_free(def_val_type->def_val.record->fields);
                }
                wasm_runtime_free(def_val_type->def_val.record);
                def_val_type->def_val.record = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_VARIANT:
        {
            if (def_val_type->def_val.variant) {
                if (def_val_type->def_val.variant->cases) {
                    for (uint32_t i = 0;
                         i < def_val_type->def_val.variant->count; i++) {
                        free_case(&def_val_type->def_val.variant->cases[i]);
                    }
                    wasm_runtime_free(def_val_type->def_val.variant->cases);
                }
                wasm_runtime_free(def_val_type->def_val.variant);
                def_val_type->def_val.variant = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_LIST:
        {
            if (def_val_type->def_val.list) {
                if (def_val_type->def_val.list->element_type) {
                    wasm_runtime_free(def_val_type->def_val.list->element_type);
                    def_val_type->def_val.list->element_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.list);
                def_val_type->def_val.list = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_LIST_LEN:
        {
            if (def_val_type->def_val.list_len) {
                if (def_val_type->def_val.list_len->element_type) {
                    wasm_runtime_free(
                        def_val_type->def_val.list_len->element_type);
                    def_val_type->def_val.list_len->element_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.list_len);
                def_val_type->def_val.list_len = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_TUPLE:
        {
            if (def_val_type->def_val.tuple) {
                if (def_val_type->def_val.tuple->element_types) {
                    wasm_runtime_free(
                        def_val_type->def_val.tuple->element_types);
                    def_val_type->def_val.tuple->element_types = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.tuple);
                def_val_type->def_val.tuple = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_FLAGS:
        {
            if (def_val_type->def_val.flag) {
                if (def_val_type->def_val.flag->flags) {
                    for (uint32_t i = 0; i < def_val_type->def_val.flag->count;
                         i++) {
                        free_label_prime(&def_val_type->def_val.flag->flags[i]);
                    }
                    wasm_runtime_free(def_val_type->def_val.flag->flags);
                }
                wasm_runtime_free(def_val_type->def_val.flag);
                def_val_type->def_val.flag = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_ENUM:
        {
            if (def_val_type->def_val.enum_type) {
                if (def_val_type->def_val.enum_type->labels) {
                    for (uint32_t i = 0;
                         i < def_val_type->def_val.enum_type->count; i++) {
                        free_label_prime(
                            &def_val_type->def_val.enum_type->labels[i]);
                    }
                    wasm_runtime_free(def_val_type->def_val.enum_type->labels);
                }
                wasm_runtime_free(def_val_type->def_val.enum_type);
                def_val_type->def_val.enum_type = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_OPTION:
        {
            if (def_val_type->def_val.option) {
                if (def_val_type->def_val.option->element_type) {
                    wasm_runtime_free(
                        def_val_type->def_val.option->element_type);
                    def_val_type->def_val.option->element_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.option);
                def_val_type->def_val.option = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_RESULT:
        {
            if (def_val_type->def_val.result) {
                if (def_val_type->def_val.result->result_type) {
                    wasm_runtime_free(
                        def_val_type->def_val.result->result_type);
                    def_val_type->def_val.result->result_type = NULL;
                }
                if (def_val_type->def_val.result->error_type) {
                    wasm_runtime_free(def_val_type->def_val.result->error_type);
                    def_val_type->def_val.result->error_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.result);
                def_val_type->def_val.result = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_OWN:
        {
            if (def_val_type->def_val.owned) {
                wasm_runtime_free(def_val_type->def_val.owned);
                def_val_type->def_val.owned = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_BORROW:
        {
            if (def_val_type->def_val.borrow) {
                wasm_runtime_free(def_val_type->def_val.borrow);
                def_val_type->def_val.borrow = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_STREAM:
        {
            if (def_val_type->def_val.stream) {
                if (def_val_type->def_val.stream->element_type) {
                    wasm_runtime_free(
                        def_val_type->def_val.stream->element_type);
                    def_val_type->def_val.stream->element_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.stream);
                def_val_type->def_val.stream = NULL;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_FUTURE:
        {
            if (def_val_type->def_val.future) {
                if (def_val_type->def_val.future->element_type) {
                    wasm_runtime_free(
                        def_val_type->def_val.future->element_type);
                    def_val_type->def_val.future->element_type = NULL;
                }
                wasm_runtime_free(def_val_type->def_val.future);
                def_val_type->def_val.future = NULL;
            }
            break;
        }
        default:
            // For primitive types, no cleanup needed
            break;
    }
}

// Validation wrapper for parse_defvaltype
static bool
parse_defvaltype(const uint8_t **payload, const uint8_t *end,
                 WASMComponentDefValType **out, char *error_buf,
                 uint32_t error_buf_size)
{
    if (!payload || !*payload || !out) {
        return false;
    }

    const uint8_t *p = *payload;
    uint8_t tag = *p;

    // Allocate memory for the def_val_type
    *out = wasm_runtime_malloc(sizeof(WASMComponentDefValType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for def_val_type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentDefValType));

    // Check if it's a primitive type
    if (is_primitive_type(tag)) {
        (*out)->tag = WASM_COMP_DEF_VAL_PRIMVAL;
        (*out)->def_val.primval = tag;
        p++;
        *payload = p;
        return true;
    }

    (*out)->tag = tag;
    p++;
    switch (tag) {
        case WASM_COMP_DEF_VAL_RECORD:
            if (!parse_record_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;

        case WASM_COMP_DEF_VAL_VARIANT:
            if (!parse_variant_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;

        case WASM_COMP_DEF_VAL_LIST:
            if (!parse_list_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;

        case WASM_COMP_DEF_VAL_LIST_LEN:
            if (!parse_list_type_with_len(&p, end, out, error_buf,
                                          error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;

        case WASM_COMP_DEF_VAL_TUPLE:
        {
            if (!parse_tuple_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }
        case WASM_COMP_DEF_VAL_FLAGS:
        {
            if (!parse_flags_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_ENUM:
        {
            if (!parse_enum_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_OPTION:
        {
            if (!parse_option_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_RESULT:
        {
            if (!parse_result_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_OWN:
        {
            if (!parse_own_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_BORROW:
        {
            if (!parse_borrow_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_STREAM:
        {
            if (!parse_stream_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        case WASM_COMP_DEF_VAL_FUTURE:
        {
            if (!parse_future_type(&p, end, out, error_buf, error_buf_size)) {
                wasm_runtime_free(*out);
                *out = NULL;
                return false;
            }
            break;
        }

        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid defvaltype tag: 0x%02x", tag);
            wasm_runtime_free(*out);
            *out = NULL;
            return false;
        }
    }

    *payload = p;
    return true;
}

static bool
parse_param_list(const uint8_t **payload, const uint8_t *end,
                 WASMComponentParamList **out, char *error_buf,
                 uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the param list structure
    *out = wasm_runtime_malloc(sizeof(WASMComponentParamList));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for param list");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentParamList));

    // Parse the param list count
    uint64_t param_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &param_count_leb, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse param count");
        return false;
    }

    uint32_t param_count = (uint32_t)param_count_leb;
    (*out)->count = param_count;

    // Allocate memory for the param list
    if (param_count > 0) {
        (*out)->params = wasm_runtime_malloc(sizeof(WASMComponentLabelValType)
                                             * param_count);
        if (!(*out)->params) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for param list");
            return false;
        }

        // Parse the param list
        for (uint32_t i = 0; i < param_count; i++) {
            if (!parse_labelvaltype(&p, end, &(*out)->params[i], error_buf,
                                    error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse param %d", i);
                return false;
            }
        }
    }
    else {
        (*out)->params = NULL;
    }

    *payload = p;
    return true;
}

bool
parse_func_type(const uint8_t **payload, const uint8_t *end,
                WASMComponentFuncType **out, char *error_buf,
                uint32_t error_buf_size)
{
    if (!payload || !*payload || !out) {
        return false;
    }

    const uint8_t *p = *payload;

    // Allocate memory for the func_type
    *out = wasm_runtime_malloc(sizeof(WASMComponentFuncType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for func_type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentFuncType));

    // Parse the param list
    if (!parse_param_list(&p, end, &(*out)->params, error_buf,
                          error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse param list");
        return false;
    }

    // Parse the result list
    if (!parse_result_list(&p, end, &(*out)->results, error_buf,
                           error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse result list");
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_component_decl_import_decl(const uint8_t **payload, const uint8_t *end,
                                 WASMComponentImportDecl **out, char *error_buf,
                                 uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the import decl
    *out = wasm_runtime_malloc(sizeof(WASMComponentImportDecl));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for import decl");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentImportDecl));

    // Parse the import name
    WASMComponentImportName *import_name =
        wasm_runtime_malloc(sizeof(WASMComponentImportName));
    if (!import_name) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for import_name");
        return false;
    }
    // Initialize the struct to zero to avoid garbage data
    memset(import_name, 0, sizeof(WASMComponentImportName));

    bool status = parse_component_import_name(&p, end, import_name, error_buf,
                                              error_buf_size);
    if (!status) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse component name for import");
        wasm_runtime_free(import_name);
        return false;
    }

    (*out)->import_name = import_name;

    WASMComponentExternDesc *extern_desc =
        wasm_runtime_malloc(sizeof(WASMComponentExternDesc));
    if (!extern_desc) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for extern desc");
        wasm_runtime_free(import_name);
        return false;
    }
    memset(extern_desc, 0, sizeof(WASMComponentExternDesc));

    // Parse the extern desc
    if (!parse_extern_desc(&p, end, extern_desc, error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse extern desc");
        wasm_runtime_free(extern_desc);
        wasm_runtime_free(import_name);
        return false;
    }

    (*out)->extern_desc = extern_desc;

    *payload = p;
    return true;
}

bool
parse_component_decl_core_type(const uint8_t **payload, const uint8_t *end,
                               WASMComponentCoreType **out, char *error_buf,
                               uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the core type
    *out = wasm_runtime_malloc(sizeof(WASMComponentCoreType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for core type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentCoreType));

    // Allocate memory fore the core def type
    WASMComponentCoreDefType *deftype =
        wasm_runtime_malloc(sizeof(WASMComponentCoreDefType));
    if (!deftype) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for core def type");
        return false;
    }
    memset(deftype, 0, sizeof(WASMComponentCoreDefType));

    // Support both encodings:
    // 1) Inline core:deftype per Binary.md (preferred)
    // 2) Producer variation: core type index (u32) at this position
    //    This handles cases where instancedecl 0x00 is followed by a u32 index
    //    instead of inline core:deftype, which wasmparser handles gracefully
    if (p < end) {
        uint8_t b0 = *p;
        bool looks_like_inline =
            (b0 == 0x50)    /* moduletype */
            || (b0 == 0x4E) /* rec group (GC, unsupported here) */
            || (b0 == 0x4F) /* final subtype (GC, unsupported here) */
            || (b0 == 0x5E) || (b0 == 0x5F)
            || (b0 == 0x60) /* comptype prefixes (GC) */
            || (b0 == 0x00 && (p + 1) < end
                && (*(p + 1) == 0x50 || *(p + 1) == 0x4E || *(p + 1) == 0x4F
                    || *(p + 1) == 0x5E || *(p + 1) == 0x5F
                    || *(p + 1) == 0x60));

        if (!looks_like_inline) {
            // Treat as core type index (producer variation)
            uint64_t idx = 0;
            const uint8_t *p_before = p;
            if (!read_leb((uint8_t **)&p, end, 32, false, &idx, error_buf,
                          error_buf_size)) {
                wasm_runtime_free(deftype);
                wasm_runtime_free(*out);
                *out = NULL;
                *payload = p_before;
                return false;
            }

            // Store as unresolved reference by leaving deftype NULL for now.
            // The index %llu will need to be resolved later against the core
            // type section
            (*out)->deftype = NULL;

            // IMPORTANT: After parsing a core type index, we need to continue
            // parsing the rest of the instancedecl. The core type index is just
            // the first part. We'll let the caller continue parsing from this
            // point.
            *payload = p;
            return true;
        }
    }

    // Parse the core type inline (Binary.md: instancedecl 0x00 t:<core:type>
    // where core:type ::= dt:<core:deftype>)
    if (!parse_single_core_type(&p, end, deftype, error_buf, error_buf_size)) {
        // cleanup on failure
        wasm_runtime_free(deftype);
        wasm_runtime_free(*out);
        *out = NULL;
        *payload = p; // propagate consumption
        return false;
    }

    (*out)->deftype = deftype;
    *payload = p;
    return true;
}

bool
parse_component_decl_type(const uint8_t **payload, const uint8_t *end,
                          WASMComponentTypes **out, char *error_buf,
                          uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the types
    *out = wasm_runtime_malloc(sizeof(WASMComponentTypes));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for types");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentTypes));

    // Parse the type
    if (!parse_single_type(&p, end, *out, error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size, "Failed to parse type");
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_component_decl_alias(const uint8_t **payload, const uint8_t *end,
                           WASMComponentAliasDefinition **out, char *error_buf,
                           uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the alias definition
    *out = wasm_runtime_malloc(sizeof(WASMComponentAliasDefinition));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for alias definition");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentAliasDefinition));

    // Parse the alias definition
    if (!parse_single_alias(&p, end, *out, error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size, "Failed to parse alias");
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_component_decl_export(const uint8_t **payload, const uint8_t *end,
                            WASMComponentComponentDeclExport **out,
                            char *error_buf, uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the types
    *out = wasm_runtime_malloc(sizeof(WASMComponentComponentDeclExport));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for types");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentComponentDeclExport));

    // Parse the import name
    WASMComponentExportName *export_name =
        wasm_runtime_malloc(sizeof(WASMComponentExportName));
    if (!export_name) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for export_name");
        return false;
    }
    // Initialize the struct to zero to avoid garbage data
    memset(export_name, 0, sizeof(WASMComponentExportName));

    bool status = parse_component_export_name(&p, end, export_name, error_buf,
                                              error_buf_size);
    if (!status) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse component name for export");
        wasm_runtime_free(export_name);
        return false;
    }

    (*out)->export_name = export_name;

    WASMComponentExternDesc *extern_desc =
        wasm_runtime_malloc(sizeof(WASMComponentExternDesc));
    if (!extern_desc) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for extern desc");
        wasm_runtime_free(export_name);
        return false;
    }
    memset(extern_desc, 0, sizeof(WASMComponentExternDesc));

    // Parse the extern desc
    if (!parse_extern_desc(&p, end, extern_desc, error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse extern desc");
        wasm_runtime_free(extern_desc);
        wasm_runtime_free(export_name);
        return false;
    }

    (*out)->extern_desc = extern_desc;

    *payload = p;
    return true;
}

bool
parse_component_instance_decl(const uint8_t **payload, const uint8_t *end,
                              WASMComponentInstDecl **out, char *error_buf,
                              uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the instance decl
    *out = wasm_runtime_malloc(sizeof(WASMComponentInstDecl));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for instance decl");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentInstDecl));

    uint8_t tag = *p;
    (*out)->tag = tag;
    p++;
    switch (tag) {
        case WASM_COMP_COMPONENT_DECL_CORE_TYPE:
        {
            if (!parse_component_decl_core_type(&p, end,
                                                &(*out)->decl.core_type,
                                                error_buf, error_buf_size)) {
                return false;
            }
            // After parsing a core type, the instancedecl is complete
            // The core type (either inline or index) is the entire content
            break;
        }
        case WASM_COMP_COMPONENT_DECL_TYPE:
        {
            if (!parse_component_decl_type(&p, end, &(*out)->decl.type,
                                           error_buf, error_buf_size)) {
                return false;
            }
            break;
        }
        case WASM_COMP_COMPONENT_DECL_ALIAS:
        {
            if (!parse_component_decl_alias(&p, end, &(*out)->decl.alias,
                                            error_buf, error_buf_size)) {
                return false;
            }
            break;
        }
        case WASM_COMP_COMPONENT_DECL_EXPORT:
        {
            if (!parse_component_decl_export(&p, end, &(*out)->decl.export_decl,
                                             error_buf, error_buf_size)) {
                return false;
            }
            break;
        }
        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid instance decl tag: 0x%02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

bool
parse_component_decl(const uint8_t **payload, const uint8_t *end,
                     WASMComponentComponentDecl **out, char *error_buf,
                     uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    uint8_t tag = *p;
    (*out)->tag = tag;
    if (tag == WASM_COMP_COMPONENT_DECL_IMPORT) {
        p++;
        if (!parse_component_decl_import_decl(&p, end,
                                              &(*out)->decl.import_decl,
                                              error_buf, error_buf_size)) {
            return false;
        }
    }
    else {
        if (!parse_component_instance_decl(&p, end, &(*out)->decl.instance_decl,
                                           error_buf, error_buf_size)) {
            return false;
        }
    }

    *payload = p;
    return true;
}

bool
parse_component_type(const uint8_t **payload, const uint8_t *end,
                     WASMComponentComponentType **out, char *error_buf,
                     uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the component type
    *out = wasm_runtime_malloc(sizeof(WASMComponentComponentType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for component type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentComponentType));

    // Read the component type count
    uint64_t component_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &component_count_leb,
                  error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse component count");
        return false;
    }

    uint32_t component_count = (uint32_t)component_count_leb;
    (*out)->count = component_count;

    // Allocate memory for the component list
    if (component_count > 0) {
        (*out)->component_decls = wasm_runtime_malloc(
            sizeof(WASMComponentComponentDecl) * component_count);
        if (!(*out)->component_decls) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for component list");
            return false;
        }

        // Parse the component list
        for (uint32_t i = 0; i < component_count; i++) {
            WASMComponentComponentDecl *component_decl =
                wasm_runtime_malloc(sizeof(WASMComponentComponentDecl));
            if (!component_decl) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Failed to allocate memory for component decl");
                return false;
            }
            memset(component_decl, 0, sizeof(WASMComponentComponentDecl));

            if (!parse_component_decl(&p, end, &component_decl, error_buf,
                                      error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse component %d", i);
                wasm_runtime_free(component_decl);
                return false;
            }

            (*out)->component_decls[i] = *component_decl;
            wasm_runtime_free(component_decl); // free shell
        }
    }
    else {
        (*out)->component_decls = NULL;
    }
    *payload = p;
    return true;
}

bool
parse_component_instance_type(const uint8_t **payload, const uint8_t *end,
                              WASMComponentInstType **out, char *error_buf,
                              uint32_t error_buf_size)
{
    if (!payload || !*payload || !out) {
        return false;
    }

    const uint8_t *p = *payload;
    WASMComponentInstDecl *instance_decls = NULL;
    WASMComponentInstDecl *instance_decl = NULL;
    uint64_t instance_count_leb = 0;
    uint32_t instance_count = 0;
    uint32_t populated = 0;
    bool ret = true;

    *out = wasm_runtime_malloc(sizeof(WASMComponentInstType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for instance type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentInstType));

    // Read the instance type count
    if (!read_leb((uint8_t **)&p, end, 32, false, &instance_count_leb,
                  error_buf, error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse instance count");
        goto fail;
    }

    instance_count = (uint32_t)instance_count_leb;
    (*out)->count = instance_count;

    // Allocate memory for the instance list
    if (instance_count > 0) {
        instance_decls =
            wasm_runtime_malloc(sizeof(WASMComponentInstDecl) * instance_count);
        if (!instance_decls) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for instance list");
            goto fail;
        }

        // Parse the instance list
        for (uint32_t i = 0; i < instance_count; i++) {
            if (p >= end) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Unexpected end of buffer at instancedecl %d",
                                 i);
                goto fail;
            }

            instance_decl = NULL;
            if (!parse_component_instance_decl(&p, end, &instance_decl,
                                               error_buf, error_buf_size)) {
                goto fail;
            }

            instance_decls[i] = *instance_decl;
            wasm_runtime_free(instance_decl); // free shell
            instance_decl = NULL;
            populated++;
        }

        (*out)->instance_decls = instance_decls;
    }
    *payload = p;
    goto done;

fail:
    ret = false;
    free_component_instance_decl(instance_decl);
    wasm_runtime_free(instance_decl);

    for (uint32_t j = 0; j < populated; j++) {
        free_component_instance_decl(&instance_decls[j]);
    }

    wasm_runtime_free(instance_decls);
    wasm_runtime_free(*out);
    *out = NULL;
done:
    return ret;
}

bool
parse_resource_type_sync(const uint8_t **payload, const uint8_t *end,
                         WASMComponentResourceTypeSync **out, char *error_buf,
                         uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the resource type sync
    *out = wasm_runtime_malloc(sizeof(WASMComponentResourceTypeSync));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for resource type sync");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentResourceTypeSync));

    uint8_t has_result_type = *p++;
    // Read optional f?:<funcidx>?
    if (has_result_type == WASM_COMP_OPTIONAL_TRUE) {
        // Read the dtor funcidx
        uint64_t dtor_func_idx_leb = 0;
        if (!read_leb((uint8_t **)&p, end, 32, false, &dtor_func_idx_leb,
                      error_buf, error_buf_size)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse resource type sync");
            return false;
        }
        (*out)->has_dtor = true;
        (*out)->dtor_func_idx = (uint32_t)dtor_func_idx_leb;
    }
    else if (has_result_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_result_type);
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_resource_type_async(const uint8_t **payload, const uint8_t *end,
                          WASMComponentResourceTypeAsync **out, char *error_buf,
                          uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the resource type async

    *out = wasm_runtime_malloc(sizeof(WASMComponentResourceTypeAsync));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for resource type async");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentResourceTypeAsync));

    // Read the dtor funcidx
    uint64_t dtor_func_idx_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &dtor_func_idx_leb, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse resource type async");
        return false;
    }
    (*out)->dtor_func_idx = (uint32_t)dtor_func_idx_leb;

    // Read optional cb?:<funcidx>?
    uint8_t has_result_type = *p++;
    if (has_result_type == WASM_COMP_OPTIONAL_TRUE) {
        // Read the cb funcidx
        uint64_t callback_func_idx_leb = 0;
        if (!read_leb((uint8_t **)&p, end, 32, false, &callback_func_idx_leb,
                      error_buf, error_buf_size)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to parse resource type async");
            return false;
        }
        (*out)->callback_func_idx = (uint32_t)callback_func_idx_leb;
    }
    else if (has_result_type != WASM_COMP_OPTIONAL_FALSE) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         has_result_type);
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_resource_type(const uint8_t **payload, const uint8_t *end,
                    WASMComponentResourceType **out, char *error_buf,
                    uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the resource type
    *out = wasm_runtime_malloc(sizeof(WASMComponentResourceType));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for resource type");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentResourceType));

    // Read the resource type tag
    uint8_t tag = *p++;
    (*out)->tag = tag;

    // Read the resource type rep
    if (*p++ != WASM_COMP_RESOURCE_REP_I32) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid resource type rep: 0x%02x", *p);
        return false;
    }

    switch (tag) {
        case WASM_COMP_RESOURCE_TYPE_SYNC:
        {
            if (!parse_resource_type_sync(&p, end, &(*out)->resource.sync,
                                          error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_COMP_RESOURCE_TYPE_ASYNC:
        {
            if (!parse_resource_type_async(&p, end, &(*out)->resource.async,
                                           error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid resource type tag: 0x%02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

// Helper function to parse a type individually
bool
parse_single_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentTypes *out, char *error_buf,
                  uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p;
    WASMComponentTypesTag type_tag = get_type_tag(tag);
    out->tag = type_tag;

    // Only advance past the tag for non-defval types. For defvaltype, the tag
    // is part of the inner grammar and must be consumed by parse_defvaltype.
    if (type_tag != WASM_COMP_DEF_TYPE) {
        p++;
    }

    switch (type_tag) {
        case WASM_COMP_DEF_TYPE:
        {
            if (!parse_defvaltype(&p, end, &out->type.def_val_type, error_buf,
                                  error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_COMP_FUNC_TYPE:
        {
            if (!parse_func_type(&p, end, &out->type.func_type, error_buf,
                                 error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_COMP_COMPONENT_TYPE:
        {
            // componenttype ::= 0x41 cd*:vec(<componentdecl>)
            if (!parse_component_type(&p, end, &out->type.component_type,
                                      error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_COMP_INSTANCE_TYPE:
        {
            // instancetype ::= 0x42 id*:vec(<instancedecl>)
            if (!parse_component_instance_type(&p, end,
                                               &out->type.instance_type,
                                               error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_COMP_RESOURCE_TYPE_SYNC:
        case WASM_COMP_RESOURCE_TYPE_ASYNC:
        {
            // resourcetype ::= 0x3f 0x7f f?:<funcidx>? => (resource (rep i32)
            // (dtor f)?)
            //               |  0x3e 0x7f f:<funcidx> cb?:<funcidx>? =>
            //               (resource (rep i32) (dtor async f (callback cb)?))
            // Note: We already consumed the leading tag byte above. Rewind so
            // the resource parser can read the tag and rep as specified by
            // Binary.md.
            p--;
            if (!parse_resource_type(&p, end, &out->type.resource_type,
                                     error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid type tag: 0x%02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

// Section 7: types section
bool
wasm_component_parse_types_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentTypeSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len)
{
    if (!payload || !*payload || !out || payload_len == 0) {
        if (consumed_len)
            *consumed_len = 0;
        return false;
    }

    const uint8_t *p = *payload;
    const uint8_t *end = *payload + payload_len;
    uint32_t type_count = 0;

    // Read type count
    uint64_t type_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &type_count_leb, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    type_count = (uint32_t)type_count_leb;
    out->count = type_count;

    if (type_count > 0) {
        // Allocate the types array
        out->types =
            wasm_runtime_malloc(sizeof(WASMComponentTypes) * type_count);
        if (!out->types) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for types array");
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }

        // Initialize all types to zero
        memset(out->types, 0, sizeof(WASMComponentTypes) * type_count);

        for (uint32_t i = 0; i < type_count; ++i) {
            if (!parse_single_type(&p, end, &out->types[i], error_buf,
                                   error_buf_size)) {
                wasm_runtime_free(out->types);
                out->types = NULL;
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }
        }
    }
    else {
        out->types = NULL;
    }
    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);
    return true;
}

static void
free_component_instance_decl(WASMComponentInstDecl *decl)
{
    if (!decl)
        return;
    switch (decl->tag) {
        case WASM_COMP_COMPONENT_DECL_INSTANCE_CORE_TYPE:
            if (decl->decl.core_type) {
                if (decl->decl.core_type->deftype) {
                    free_core_deftype(decl->decl.core_type->deftype);
                    wasm_runtime_free(decl->decl.core_type->deftype);
                }
                wasm_runtime_free(decl->decl.core_type);
                decl->decl.core_type = NULL;
            }
            break;
        case WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE:
            if (decl->decl.type) {
                free_component_types_entry(decl->decl.type);
                wasm_runtime_free(decl->decl.type);
                decl->decl.type = NULL;
            }
            break;
        case WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS:
            if (decl->decl.alias) {
                // Free sort
                if (decl->decl.alias->sort) {
                    wasm_runtime_free(decl->decl.alias->sort);
                    decl->decl.alias->sort = NULL;
                }
                // Free target-specific fields
                switch (decl->decl.alias->alias_target_type) {
                    case WASM_COMP_ALIAS_TARGET_EXPORT:
                        if (decl->decl.alias->target.exported.name) {
                            free_core_name(
                                decl->decl.alias->target.exported.name);
                            wasm_runtime_free(
                                decl->decl.alias->target.exported.name);
                            decl->decl.alias->target.exported.name = NULL;
                        }
                        break;
                    case WASM_COMP_ALIAS_TARGET_CORE_EXPORT:
                        if (decl->decl.alias->target.core_exported.name) {
                            free_core_name(
                                decl->decl.alias->target.core_exported.name);
                            wasm_runtime_free(
                                decl->decl.alias->target.core_exported.name);
                            decl->decl.alias->target.core_exported.name = NULL;
                        }
                        break;
                    case WASM_COMP_ALIAS_TARGET_OUTER:
                        break;
                }
                wasm_runtime_free(decl->decl.alias);
                decl->decl.alias = NULL;
            }
            break;
        case WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL:
            if (decl->decl.export_decl) {
                if (decl->decl.export_decl->export_name) {
                    free_component_export_name(
                        decl->decl.export_decl->export_name);
                    wasm_runtime_free(decl->decl.export_decl->export_name);
                    decl->decl.export_decl->export_name = NULL;
                }
                if (decl->decl.export_decl->extern_desc) {
                    free_extern_desc(decl->decl.export_decl->extern_desc);
                    wasm_runtime_free(decl->decl.export_decl->extern_desc);
                    decl->decl.export_decl->extern_desc = NULL;
                }
                wasm_runtime_free(decl->decl.export_decl);
                decl->decl.export_decl = NULL;
            }
            break;
    }
}

static void
free_component_decl(WASMComponentComponentDecl *decl)
{
    if (!decl)
        return;
    switch (decl->tag) {
        case WASM_COMP_COMPONENT_DECL_IMPORT:
            if (decl->decl.import_decl) {
                if (decl->decl.import_decl->import_name) {
                    free_component_import_name(
                        decl->decl.import_decl->import_name);
                    wasm_runtime_free(decl->decl.import_decl->import_name);
                    decl->decl.import_decl->import_name = NULL;
                }
                if (decl->decl.import_decl->extern_desc) {
                    free_extern_desc(decl->decl.import_decl->extern_desc);
                    wasm_runtime_free(decl->decl.import_decl->extern_desc);
                    decl->decl.import_decl->extern_desc = NULL;
                }
                wasm_runtime_free(decl->decl.import_decl);
                decl->decl.import_decl = NULL;
            }
            break;
        default:
            // Instance decl branch handles tags 0x00/0x01/0x02/0x04 inside
            // nested decl
            if (decl->decl.instance_decl) {
                free_component_instance_decl(decl->decl.instance_decl);
                wasm_runtime_free(decl->decl.instance_decl);
                decl->decl.instance_decl = NULL;
            }
            break;
    }
}

static void
free_resource_type(WASMComponentResourceType *rt)
{
    if (!rt)
        return;
    switch (rt->tag) {
        case WASM_COMP_RESOURCE_TYPE_SYNC:
            if (rt->resource.sync) {
                wasm_runtime_free(rt->resource.sync);
                rt->resource.sync = NULL;
            }
            break;
        case WASM_COMP_RESOURCE_TYPE_ASYNC:
            if (rt->resource.async) {
                wasm_runtime_free(rt->resource.async);
                rt->resource.async = NULL;
            }
            break;
        default:
            break;
    }
}

static void
free_component_types_entry(WASMComponentTypes *type)
{
    if (!type)
        return;
    switch (type->tag) {
        case WASM_COMP_DEF_TYPE:
            if (type->type.def_val_type) {
                free_defvaltype(type->type.def_val_type);
                wasm_runtime_free(type->type.def_val_type);
                type->type.def_val_type = NULL;
            }
            break;
        case WASM_COMP_FUNC_TYPE:
            if (type->type.func_type) {
                if (type->type.func_type->params) {
                    if (type->type.func_type->params->params) {
                        for (uint32_t j = 0;
                             j < type->type.func_type->params->count; j++) {
                            if (type->type.func_type->params->params[j].label) {
                                free_core_name(
                                    type->type.func_type->params->params[j]
                                        .label);
                                wasm_runtime_free(
                                    type->type.func_type->params->params[j]
                                        .label);
                            }
                            if (type->type.func_type->params->params[j]
                                    .value_type) {
                                wasm_runtime_free(
                                    type->type.func_type->params->params[j]
                                        .value_type);
                            }
                        }
                        wasm_runtime_free(type->type.func_type->params->params);
                    }
                    wasm_runtime_free(type->type.func_type->params);
                    type->type.func_type->params = NULL;
                }
                if (type->type.func_type->results) {
                    if (type->type.func_type->results->results) {
                        wasm_runtime_free(
                            type->type.func_type->results->results);
                    }
                    wasm_runtime_free(type->type.func_type->results);
                    type->type.func_type->results = NULL;
                }
                wasm_runtime_free(type->type.func_type);
                type->type.func_type = NULL;
            }
            break;
        case WASM_COMP_COMPONENT_TYPE:
            if (type->type.component_type) {
                if (type->type.component_type->component_decls) {
                    for (uint32_t k = 0; k < type->type.component_type->count;
                         k++) {
                        free_component_decl(
                            &type->type.component_type->component_decls[k]);
                    }
                    wasm_runtime_free(
                        type->type.component_type->component_decls);
                    type->type.component_type->component_decls = NULL;
                }
                wasm_runtime_free(type->type.component_type);
                type->type.component_type = NULL;
            }
            break;
        case WASM_COMP_INSTANCE_TYPE:
            if (type->type.instance_type) {
                if (type->type.instance_type->instance_decls) {
                    for (uint32_t k = 0; k < type->type.instance_type->count;
                         k++) {
                        free_component_instance_decl(
                            &type->type.instance_type->instance_decls[k]);
                    }
                    wasm_runtime_free(type->type.instance_type->instance_decls);
                    type->type.instance_type->instance_decls = NULL;
                }
                wasm_runtime_free(type->type.instance_type);
                type->type.instance_type = NULL;
            }
            break;
        case WASM_COMP_RESOURCE_TYPE_SYNC:
        case WASM_COMP_RESOURCE_TYPE_ASYNC:
            if (type->type.resource_type) {
                free_resource_type(type->type.resource_type);
                wasm_runtime_free(type->type.resource_type);
                type->type.resource_type = NULL;
            }
            break;
        default:
            break;
    }
}

// Individual section free functions
void
wasm_component_free_types_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.type_section) {
        return;
    }

    WASMComponentTypeSection *type_section = section->parsed.type_section;

    if (type_section->types) {
        for (uint32_t i = 0; i < type_section->count; i++) {
            WASMComponentTypes *type = &type_section->types[i];

            free_component_types_entry(type);
        }
        wasm_runtime_free(type_section->types);
        type_section->types = NULL;
    }

    // Free the type section structure itself
    wasm_runtime_free(type_section);
    section->parsed.type_section = NULL;
}

uint32
fill_resource_type_instance(WASMComponentTypeInstance **types,
                            uint32 *curr_types_count, void *defined_types,
                            uint32 defined_types_size,
                            WASMComponentTypes *type_definition,
                            WASMComponentInstance *comp_instance,
                            char *error_buf, uint32 error_buf_size)
{
    uint32 size = 0;
    uint32 types_count = *curr_types_count;
    if (!types) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: invalid types index space\n");
        return 0;
    }
    WASMComponentTypeInstance *curr_type =
        (WASMComponentTypeInstance *)defined_types;

    LOG_DEBUG("Fill resource type instance");

    size = sizeof(WASMComponentTypeInstance)
           + sizeof(WASMComponentResourceInstance)
           + 3 * sizeof(WASMFunctionInstance);
    WASMComponentResourceInstance *resource =
        (WASMComponentResourceInstance *)((uint8_t *)defined_types
                                          + sizeof(WASMComponentTypeInstance));
    WASMFunctionInstance *drop_method =
        (WASMFunctionInstance *)((uint8_t *)resource
                                 + sizeof(WASMComponentResourceInstance));
    WASMFunctionInstance *new_method =
        (WASMFunctionInstance *)((uint8_t *)drop_method
                                 + sizeof(WASMFunctionInstance));
    WASMFunctionInstance *rep_method =
        (WASMFunctionInstance *)((uint8_t *)new_method
                                 + sizeof(WASMFunctionInstance));

    memset(resource, 0, sizeof(WASMComponentResourceInstance));
    memset(drop_method, 0, sizeof(WASMFunctionInstance));
    memset(new_method, 0, sizeof(WASMFunctionInstance));
    memset(rep_method, 0, sizeof(WASMFunctionInstance));

    drop_method->resource = resource;
    drop_method->canon_type = WASM_COMP_CANON_RESOURCE_DROP;
    drop_method->is_canon_func = true;
    drop_method->param_cell_num = 1;
    drop_method->ret_cell_num = 0;

    new_method->resource = resource;
    new_method->canon_type = WASM_COMP_CANON_RESOURCE_NEW;
    new_method->is_canon_func = true;
    new_method->param_cell_num = 1;
    new_method->ret_cell_num = 1;

    rep_method->resource = resource;
    rep_method->canon_type = WASM_COMP_CANON_RESOURCE_REP;
    rep_method->is_canon_func = true;
    rep_method->param_cell_num = 1;
    rep_method->ret_cell_num = 1;

    if (size > defined_types_size) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Defined types alocated memory exceeded\n");
        return 0;
    }

    WASMComponentResourceType *resource_definition =
        type_definition->type.resource_type;
    if (resource_definition->tag == WASM_COMP_RESOURCE_TYPE_ASYNC) {
        LOG_WARNING("Async type not yet supported");
        return 0;
    }
    if (resource_definition->resource.sync->has_dtor) {
        if (resource_definition->resource.sync->dtor_func_idx
            >= comp_instance->core_functions_count) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid index %d for dtor method\n",
                             resource_definition->resource.sync->dtor_func_idx);
            return 0;
        }
        WASMFunctionInstance *dtor_func =
            comp_instance->core_functions[resource_definition->resource.sync
                                              ->dtor_func_idx];
        // destructor must have type (param i32) -> ()
        if (!dtor_func->is_canon_func) {
            const WASMFuncType *dtor_ft =
                dtor_func->is_import_func ? dtor_func->u.func_import->func_type
                                          : dtor_func->u.func->func_type;
            if (dtor_func->param_count != 1
                || dtor_func->param_types[0] != VALUE_TYPE_I32
                || dtor_ft->result_count != 0) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "resource destructor must have type (param i32) -> ()");
                return 0;
            }
        }
        resource->dtor_method = dtor_func;
    }
    resource->is_wasi = false;
    resource->impl = comp_instance;
    resource->drop_method = drop_method;
    resource->new_method = new_method;
    resource->rep_method = rep_method;
    curr_type->type = COMPONENT_VAL_TYPE_RESOURCE_SYNC;
    curr_type->type_specific.resource = resource;

    types[types_count] = curr_type;
    (*curr_types_count)++;

    return size;
}

/// @brief Adds a new default value type to defined types memory section +
/// refference to it in the types index space
/// @param types Types index space entry ==> holds tag + pointer to type
/// specific implementation (from defined_types)
/// @param types_count current entry in types index space
/// @param defined_types Pointer to the last free memory area in the defined
/// types section ==> will hold the actual type defintion that the index space
/// will point to
/// @param defined_types_size Remaining free space for defined types allocation
/// ==> used for checking for out of bounds allocation
/// @param type_definition Type definition from Binary section, without index
/// references resolved
/// @return Returns size of the newly defined type ==> will be used to increment
/// defined_types pointer to the next available memory area
uint32
fill_def_type_instance(WASMComponentTypeInstance **types,
                       uint32 *curr_types_count, void *defined_types,
                       uint32 defined_types_size,
                       WASMComponentTypes *type_definition, char *error_buf,
                       uint32 error_buf_size)
{
    uint32 size = 0, val_idx = 0;
    uint32 types_count = *curr_types_count;
    if (!types) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: invalid types index space\n");
        return 0;
    }
    WASMComponentTypeInstance *curr_type =
        (WASMComponentTypeInstance *)defined_types;

    switch (type_definition->type.def_val_type->tag) {
        case WASM_COMP_DEF_VAL_PRIMVAL: // pvt:<primvaltype>
            LOG_DEBUG("Fill primval type instance");
            size = sizeof(WASMComponentTypeInstance);
            if (size > defined_types_size)
                goto fail;
            curr_type->type = COMPONENT_VAL_TYPE_PRIMVAL;
            curr_type->type_specific.primval =
                type_definition->type.def_val_type->def_val.primval;

            break;
        // Record type (labeled fields)
        case WASM_COMP_DEF_VAL_RECORD: // 0x72 lt*:vec(<labelvaltype>)
            LOG_DEBUG("Fill record type instance");
            WASMComponentRecordType *record_definition =
                type_definition->type.def_val_type->def_val.record;
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentRecordInstance)
                   + (record_definition->count)
                         * sizeof(WASMComponentLabelValTypeInstance);
            if (size > defined_types_size)
                goto fail;
            WASMComponentRecordInstance *record =
                (WASMComponentRecordInstance *)((uint8_t *)defined_types
                                                + sizeof(
                                                    WASMComponentTypeInstance));
            record->count = record_definition->count;
            for (val_idx = 0; val_idx < record->count; val_idx++) {
                record->fields[val_idx].label =
                    record_definition->fields[val_idx].label;
                assign_type_instance(
                    &record->fields[val_idx].type,
                    record_definition->fields[val_idx].value_type, types);
            }
            curr_type->type = COMPONENT_VAL_TYPE_RECORD;
            curr_type->type_specific.record = record;

            break;
        // Variant type (labeled cases)
        case WASM_COMP_DEF_VAL_VARIANT: // 0x71 case*:vec(<case>)
            LOG_DEBUG("Fill variant type instance");
            WASMComponentVariantType *variant_definition =
                type_definition->type.def_val_type->def_val.variant;
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentVariantInstance)
                   + variant_definition->count
                         * sizeof(WASMComponentCaseValInstance);
            if (size > defined_types_size)
                goto fail;
            WASMComponentVariantInstance *variant =
                (WASMComponentVariantInstance
                     *)((uint8_t *)defined_types
                        + sizeof(WASMComponentTypeInstance));

            variant->count = variant_definition->count;
            for (val_idx = 0; val_idx < variant->count; val_idx++) {
                variant->cases[val_idx].label =
                    variant_definition->cases[val_idx].label;
                assign_type_instance(
                    &variant->cases[val_idx].value_type,
                    variant_definition->cases[val_idx].value_type, types);
            }
            curr_type->type = COMPONENT_VAL_TYPE_VARIANT;
            curr_type->type_specific.variant = variant;
            break;
        // List types
        case WASM_COMP_DEF_VAL_LIST: // 0x70 t:<valtype>
            LOG_DEBUG("Fill list type instance");
            WASMComponentListType *list_definition =
                type_definition->type.def_val_type->def_val.list;
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentListInstance);
            if (size > defined_types_size)
                goto fail;
            WASMComponentListInstance *list =
                (WASMComponentListInstance *)((uint8_t *)defined_types
                                              + sizeof(
                                                  WASMComponentTypeInstance));
            assign_type_instance(&list->element_type,
                                 list_definition->element_type, types);
            curr_type->type = COMPONENT_VAL_TYPE_LIST;
            curr_type->type_specific.list = list;
            break;
        case WASM_COMP_DEF_VAL_LIST_LEN: // 0x67 t:<valtype> len:<u32>
            LOG_DEBUG("Fill fixed size list type instance");
            WASMComponentListLenType *list_len_definition =
                type_definition->type.def_val_type->def_val.list_len;
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentListLenInstance);
            if (size > defined_types_size)
                goto fail;
            WASMComponentListLenInstance *list_len =
                (WASMComponentListLenInstance
                     *)((uint8_t *)defined_types
                        + sizeof(WASMComponentTypeInstance));
            list_len->len = list_len_definition->len;
            assign_type_instance(&list_len->element_type,
                                 list_len_definition->element_type, types);
            curr_type->type = COMPONENT_VAL_TYPE_FIXED_SIZE_LIST;
            curr_type->type_specific.list_len = list_len;
            break;
        // Tuple type
        case WASM_COMP_DEF_VAL_TUPLE: // 0x6f t*:vec(<valtype>)
            LOG_DEBUG("Fill tuple type instance");
            WASMComponentTupleType *tuple_definition =
                type_definition->type.def_val_type->def_val.tuple;
            size =
                sizeof(WASMComponentTypeInstance)
                + sizeof(WASMComponentTupleInstance)
                + tuple_definition->count * sizeof(WASMComponentTypeInstance *);
            if (size > defined_types_size)
                goto fail;
            WASMComponentTupleInstance *tuple =
                (WASMComponentTupleInstance *)((uint8_t *)defined_types
                                               + sizeof(
                                                   WASMComponentTypeInstance));

            tuple->count = tuple_definition->count;
            tuple->element_types =
                (WASMComponentTypeInstance **)((char *)tuple
                                               + sizeof(
                                                   WASMComponentTupleInstance));
            for (val_idx = 0; val_idx < tuple->count; val_idx++) {
                LOG_DEBUG("Fill Tuple element %d, type %d", val_idx,
                          tuple_definition->element_types[val_idx].type);
                assign_type_instance(&tuple->element_types[val_idx],
                                     &tuple_definition->element_types[val_idx],
                                     types);
            }
            curr_type->type = COMPONENT_VAL_TYPE_TUPLE;
            curr_type->type_specific.tuple = tuple;
            break;
        // Flags type
        case WASM_COMP_DEF_VAL_FLAGS: // 0x6e l*:vec(<label'>)
            LOG_DEBUG("Fill flags type instance");
            size = sizeof(WASMComponentTypeInstance);
            if (size > defined_types_size)
                goto fail;
            WASMComponentFlagType *flag =
                type_definition->type.def_val_type->def_val.flag;
            curr_type->type = COMPONENT_VAL_TYPE_FLAGS;
            curr_type->type_specific.flag = flag;
            break;
        // Enum type
        case WASM_COMP_DEF_VAL_ENUM: // 0x6d l*:vec(<label'>)
            LOG_DEBUG("Fill enum type instance");
            size = sizeof(WASMComponentTypeInstance);
            WASMComponentEnumType *enum_definition =
                type_definition->type.def_val_type->def_val.enum_type;
            if (size > defined_types_size)
                goto fail;
            curr_type->type = COMPONENT_VAL_TYPE_ENUM;
            curr_type->type_specific.enum_type = enum_definition;
            break;
        // Option type
        case WASM_COMP_DEF_VAL_OPTION: // 0x6b t:<valtype>
            LOG_DEBUG("Fill option type instance");
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentOptionInstance);
            WASMComponentOptionInstance *option =
                (WASMComponentOptionInstance *)((uint8_t *)defined_types
                                                + sizeof(
                                                    WASMComponentTypeInstance));
            if (size > defined_types_size)
                goto fail;
            WASMComponentOptionType *option_definition =
                type_definition->type.def_val_type->def_val.option;
            assign_type_instance(&option->element_type,
                                 option_definition->element_type, types);
            curr_type->type = COMPONENT_VAL_TYPE_OPTION;
            curr_type->type_specific.option = option;
            break;
        // Result type
        case WASM_COMP_DEF_VAL_RESULT: // 0x6a t?:<valtype>? u?:<valtype>?
            LOG_DEBUG("Fill result type instance");
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentResultInstance);
            WASMComponentResultInstance *result =
                (WASMComponentResultInstance *)((uint8_t *)defined_types
                                                + sizeof(
                                                    WASMComponentTypeInstance));
            if (size > defined_types_size)
                goto fail;
            WASMComponentResultType *result_definition =
                type_definition->type.def_val_type->def_val.result;
            assign_type_instance(&result->result_type,
                                 result_definition->result_type, types);
            assign_type_instance(&result->error_type,
                                 result_definition->error_type, types);
            curr_type->type = COMPONENT_VAL_TYPE_RESULT;
            curr_type->type_specific.result = result;
            break;
        // Handle types
        case WASM_COMP_DEF_VAL_OWN: // 0x69 i:<typeidx>
            LOG_DEBUG("Fill resource own type instance");
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentResourceHandleInstance);
            WASMComponentResourceHandleInstance *resource_own =
                (WASMComponentResourceHandleInstance
                     *)((uint8_t *)defined_types
                        + sizeof(WASMComponentTypeInstance));
            if (size > defined_types_size)
                goto fail;
            WASMComponentOwnType *own_definition =
                type_definition->type.def_val_type->def_val.owned;
            if (own_definition->type_idx >= *curr_types_count
                || types[own_definition->type_idx]->type
                       != COMPONENT_VAL_TYPE_RESOURCE_SYNC) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Type %d is not a resource\n",
                                 own_definition->type_idx);
                return 0;
            }
            resource_own->resource =
                types[own_definition->type_idx]->type_specific.resource;
            resource_own->is_borrow = false;
            curr_type->type = COMPONENT_VAL_TYPE_OWN;
            curr_type->type_specific.resource_handle = resource_own;
            break;
        case WASM_COMP_DEF_VAL_BORROW: // 0x68 i:<typeidx>
            LOG_DEBUG("Fill resource borrow type instance");
            size = sizeof(WASMComponentTypeInstance)
                   + sizeof(WASMComponentResourceHandleInstance);
            WASMComponentResourceHandleInstance *resource_borrow =
                (WASMComponentResourceHandleInstance
                     *)((uint8_t *)defined_types
                        + sizeof(WASMComponentTypeInstance));
            if (size > defined_types_size)
                goto fail;
            WASMComponentBorrowType *borrow_definition =
                type_definition->type.def_val_type->def_val.borrow;
            if (borrow_definition->type_idx >= *curr_types_count
                || (types[borrow_definition->type_idx]->type
                        != COMPONENT_VAL_TYPE_RESOURCE_SYNC
                    && types[borrow_definition->type_idx]->type
                           != COMPONENT_VAL_TYPE_RESOURCE_ASYNC)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Type %d is not a resource\n",
                                 borrow_definition->type_idx);
                return 0;
            }
            resource_borrow->resource =
                types[borrow_definition->type_idx]->type_specific.resource;
            resource_borrow->is_borrow = true;
            curr_type->type = COMPONENT_VAL_TYPE_BORROW;
            curr_type->type_specific.resource_handle = resource_borrow;
            break;
            // TODO
        // Async types
        case WASM_COMP_DEF_VAL_STREAM: // 0x66 t?:<valtype>?
                                       // TODO
        case WASM_COMP_DEF_VAL_FUTURE: // 0x65 t?:<valtype>?
            // TODO
            return 0;
    }
    curr_type->alignment = compute_alignment(curr_type);
    curr_type->elem_size = compute_elem_size(curr_type);
    LOG_DEBUG("Alignment: %d | Elem_size: %d\n", curr_type->alignment,
              curr_type->elem_size);
    types[types_count] = curr_type;
    (*curr_types_count)++;

    return size; // Returns the size of the newly added defined type, so that
                 // defined_types pointer can be incremented by this ammount,
                 // pointing to the next free location in defined types memory
                 // area
fail:
    set_error_buf_ex(error_buf, error_buf_size,
                     "Defined types alocated memory exceeded\n");
    return 0;
}

/// @brief Adds a new function signature type to defined types memory section +
/// refference to it in the types index space
/// @param types Types index space entry ==> holds tag + pointer to type
/// specific implementation (from defined_types)
/// @param types_count current entry in types index space
/// @param defined_types Pointer to the last free memory area in the defined
/// types section ==> will hold the actual type defintion that the index space
/// will point to
/// @param defined_types_size Remaining free space for defined types allocation
/// ==> used for checking for out of bounds allocation
/// @param type_definition Type definition from Binary section, without index
/// references resolved
/// @return Returns size of the newly defined type ==> will be used to increment
/// defined_types pointer to the next available memory area
uint32
fill_func_type_instance(WASMComponentTypeInstance **types, uint32 *types_count,
                        void *defined_types, uint32 defined_types_size,
                        WASMComponentTypes *type_definition, char *error_buf,
                        uint32 error_buf_size)
{
    uint32 size = 0, val_idx = 0;
    size = wasm_get_func_type_size(type_definition->type.func_type);
    if (size > defined_types_size) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Defined types alocated memory exceeded");
        return 0;
    }

    // Allocation in memory:
    /*
    +-----------------------------------+
    |  WASMComponentTypeInstance        |
    +-----------------------------------+ ->type_specific.function
    |  WASMComponentFuncTypeInstance    |
    +-----------------------------------+ ->params
    |  WASMComponentParamListInstance   |
    +-----------------------------------+ ->results
    |  WASMComponentResultListInstance  |
    +-----------------------------------+
    */

    WASMComponentFuncType *defined_func = type_definition->type.func_type;

    WASMComponentTypeInstance *curr_type =
        (WASMComponentTypeInstance *)defined_types;
    WASMComponentFuncTypeInstance *func_instance_type =
        (WASMComponentFuncTypeInstance *)((uint8_t *)curr_type
                                          + sizeof(WASMComponentTypeInstance));
    WASMComponentParamListInstance *parameters =
        (WASMComponentParamListInstance *)((uint8_t *)func_instance_type
                                           + sizeof(
                                               WASMComponentFuncTypeInstance));

    // Parameters will be allocated first
    parameters->count = defined_func->params->count;
    for (val_idx = 0; val_idx < parameters->count; val_idx++) {
        parameters->params[val_idx].label =
            defined_func->params->params[val_idx].label;
        assign_type_instance(&parameters->params[val_idx].type,
                             defined_func->params->params[val_idx].value_type,
                             types);
    }
    // Result is allocated second
    WASMComponentResultListInstance *results =
        (WASMComponentResultListInstance
             *)((uint8_t *)parameters
                + (parameters->count
                   * sizeof(WASMComponentLabelValTypeInstance))
                + sizeof(WASMComponentParamListInstance));

    results->tag = defined_func->results->tag;
    results->count = (results->tag == WASM_COMP_RESULT_LIST_WITH_TYPE) ? 1 : 0;
    assign_type_instance(&results->result, defined_func->results->results,
                         types);

    func_instance_type->params = parameters;
    func_instance_type->results = results;
    curr_type->type = COMPONENT_VAL_TYPE_FUNCTION;
    curr_type->type_specific.function = func_instance_type;

    types[*types_count] = curr_type;
    (*types_count)++;
    return size;
}

uint32
fill_instance_type_instance(WASMComponentTypeInstance **types,
                            uint32 *types_count, void *defined_types,
                            uint32 defined_types_size,
                            WASMComponentTypes *type_definition,
                            WASMComponentInstance *parent, char *error_buf,
                            uint32 error_buf_size)
{
    WASMComponentInstanceDeclTypeSize instance_decl_size = { 0 };
    uint32 size = 0, val_idx = 0, curr_type_size = 0;
    size += wasm_get_inst_decl_size(type_definition->type.instance_type,
                                    &instance_decl_size);
    if (size > defined_types_size) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Defined types alocated memory exceeded");
        return 0;
    }
    LOG_DEBUG("Fill instance type instance: %d definitions, total size %d",
              type_definition->type.instance_type->count, size);
    WASMComponentInstDecl *instance_decl = NULL;

    WASMComponentTypeInstance *curr_type =
        (WASMComponentTypeInstance *)defined_types;
    WASMComponentInstTypeInstance *inst_type_instance =
        (WASMComponentInstTypeInstance *)((uint8_t *)defined_types
                                          + sizeof(WASMComponentTypeInstance));
    WASMComponentTypeInstance **inst_type_instance_types =
        (WASMComponentTypeInstance **)((uint8_t *)inst_type_instance
                                       + sizeof(WASMComponentInstTypeInstance));
    WASMComponentFuncTypeInstance **inst_type_instance_funcs =
        (WASMComponentFuncTypeInstance *
             *)((uint8_t *)inst_type_instance_types
                + (instance_decl_size.types_count
                   * sizeof(WASMComponentTypeInstance *)));
    WASMComponentExportInstance *inst_type_instance_exports =
        (WASMComponentExportInstance
             *)((uint8_t *)inst_type_instance_funcs
                + (instance_decl_size.func_count
                   * sizeof(WASMComponentFuncTypeInstance *)));
    WASMFunctionInstance *inst_type_instance_core_funcs =
        (WASMFunctionInstance *)((uint8_t *)inst_type_instance_exports
                                 + (instance_decl_size.exports_count
                                    * sizeof(WASMComponentExportInstance)));
    WASMFunctionImport *inst_type_instance_core_func_imports =
        (WASMFunctionImport *)((uint8_t *)inst_type_instance_core_funcs
                               + ((instance_decl_size.func_count
                                   + instance_decl_size.resource_count)
                                  * sizeof(WASMFunctionInstance)));
    void *inst_type_instance_defined_types =
        (void *)((uint8_t *)inst_type_instance_core_func_imports
                 + ((instance_decl_size.func_count
                     + instance_decl_size.resource_count * 3)
                    * sizeof(WASMFunctionImport)));

    curr_type->type = COMPONENT_VAL_TYPE_INSTANCE;
    curr_type->type_specific.instance = inst_type_instance;
    inst_type_instance->types_count = 0;
    inst_type_instance->types = inst_type_instance_types;
    inst_type_instance->func_count = 0;
    inst_type_instance->funcs = inst_type_instance_funcs;
    inst_type_instance->exports_count = 0;
    inst_type_instance->exports = inst_type_instance_exports;
    inst_type_instance->defined_core_funcs = inst_type_instance_core_funcs;
    for (val_idx = 0; val_idx < instance_decl_size.func_count
                                    + instance_decl_size.resource_count;
         val_idx++) {
        inst_type_instance->defined_core_funcs[val_idx].u.func_import =
            &inst_type_instance_core_func_imports[val_idx];
    }

    for (val_idx = 0; val_idx < type_definition->type.instance_type->count;
         val_idx++) {
        instance_decl =
            &type_definition->type.instance_type->instance_decls[val_idx];

        /*
        +-------------------------------------------+
        | WASMComponentTypeInstance                 |
        +-------------------------------------------+
        | WASMComponentInstTypeInstance             | ==> will hold pointers to
        the vectors defined below
        +-------------------------------------------+
        | WASMComponentTypeInstance[types_count]    | ==> holds pointer to the
        Defined Types implementations (or outer types)
        +-------------------------------------------+
        | WASMComponentFuncTypeInstance[func_count] |
        +-------------------------------------------+
        | WASMComponentExport[exports_count]        |
        +-------------------------------------------+
        | -- Defined Types --                       |
        +-------------------------------------------+
        */

        switch (instance_decl->tag) {
            case WASM_COMP_COMPONENT_DECL_INSTANCE_CORE_TYPE:
                // TODO: not present in current test binaries
                break;
            case WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE:
                if (instance_decl->decl.type->tag == WASM_COMP_DEF_TYPE) {
                    curr_type_size = fill_def_type_instance(
                        inst_type_instance->types,
                        &inst_type_instance->types_count,
                        inst_type_instance_defined_types,
                        instance_decl_size.types_size, instance_decl->decl.type,
                        error_buf, error_buf_size);
                    if (!curr_type_size) {
                        return 0;
                    }
                    inst_type_instance_defined_types =
                        (void *)((uint8_t *)inst_type_instance_defined_types
                                 + curr_type_size);
                }
                else if (instance_decl->decl.type->tag == WASM_COMP_FUNC_TYPE) {
                    curr_type_size = fill_func_type_instance(
                        inst_type_instance->types,
                        &inst_type_instance->types_count,
                        inst_type_instance_defined_types,
                        instance_decl_size.types_size, instance_decl->decl.type,
                        error_buf, error_buf_size);
                    if (!curr_type_size) {
                        return 0;
                    }
                    inst_type_instance_defined_types =
                        (void *)((uint8_t *)inst_type_instance_defined_types
                                 + curr_type_size);
                }
                break;
            case WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS:
                WASMComponentAliasDefinition *alias = instance_decl->decl.alias;
                if (instance_decl->decl.alias->alias_target_type
                    != WASM_COMP_ALIAS_TARGET_OUTER) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "For now, only outer aliases are supported for "
                        "instance type definitions\n");
                    return false;
                }
                WASMComponentAliasTarget target_instance;
                target_instance.target.instance = parent;
                for (uint32 ct = 1;
                     ct < instance_decl->decl.alias->target.outer.ct; ct++) {
                    if (!target_instance.target.instance->parent) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "ERROR: Outer alias level %d not reachable, "
                            "current instance has %d parent levels\n",
                            instance_decl->decl.alias->target.outer.ct, ct);
                        return false;
                    }
                    target_instance.target.instance =
                        target_instance.target.instance->parent;
                }
                target_instance.ref.idx = alias->target.outer.idx;
                target_instance.sort = alias->sort;
                target_instance.type = WASM_COMP_ALIAS_TARGET_OUTER;
                void *alias_ptr =
                    get_alias(&target_instance, error_buf, error_buf_size);
                if (!alias_ptr) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Failed to retirieve alias\n");
                    return false;
                }
                switch (target_instance.sort->sort) {
                    case WASM_COMP_SORT_TYPE:
                        inst_type_instance
                            ->types[inst_type_instance->types_count] =
                            (WASMComponentTypeInstance *)alias_ptr;
                        inst_type_instance->types_count++;
                        break;
                    default:
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "For now only type outer aliases are supported in "
                            "component instance definitions\n");
                        break;
                }
                break;

            case WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL:
                inst_type_instance->exports[inst_type_instance->exports_count]
                    .export_name = instance_decl->decl.export_decl->export_name;
                inst_type_instance->exports[inst_type_instance->exports_count]
                    .type = instance_decl->decl.export_decl->extern_desc->type;
                switch (instance_decl->decl.export_decl->extern_desc->type) {

                    case WASM_COMP_EXTERN_TYPE:
                        if (instance_decl->decl.export_decl->extern_desc
                                ->extern_desc.type.type_bound->tag
                            == WASM_COMP_TYPEBOUND_EQ) {
                            inst_type_instance
                                ->types[inst_type_instance->types_count] =
                                inst_type_instance
                                    ->types[instance_decl->decl.export_decl
                                                ->extern_desc->extern_desc.type
                                                .type_bound->type_idx];
                            inst_type_instance
                                ->exports[inst_type_instance->exports_count]
                                .exp.type =
                                inst_type_instance
                                    ->types[inst_type_instance->types_count];
                            inst_type_instance->types_count++;
                            inst_type_instance->exports_count++;
                        }
                        else if (instance_decl->decl.export_decl->extern_desc
                                     ->extern_desc.type.type_bound->tag
                                 == WASM_COMP_TYPEBOUND_TYPE) {
                            // Sub resource definition
                            LOG_DEBUG("Fill sub resource type instance");
                            curr_type_size =
                                sizeof(WASMComponentTypeInstance)
                                + sizeof(WASMComponentResourceInstance)
                                + 3 * sizeof(WASMFunctionInstance);
                            if (curr_type_size
                                > instance_decl_size.types_size) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Defined types size exceeded "
                                                 "for sub resource\n");
                                return 0;
                            }

                            WASMComponentResourceTypeSync resource_sync_type = {
                                .has_dtor = false
                            };
                            WASMComponentResourceType resource_type_def = {
                                .tag = WASM_COMP_RESOURCE_TYPE_SYNC,
                                .resource.sync = &resource_sync_type
                            };
                            WASMComponentTypes resource_type = {
                                .type.resource_type = &resource_type_def
                            };
                            fill_resource_type_instance(
                                inst_type_instance->types,
                                &inst_type_instance->types_count,
                                inst_type_instance_defined_types,
                                defined_types_size, &resource_type, parent,
                                error_buf, error_buf_size);

                            WASMComponentExportName *export_name =
                                instance_decl->decl.export_decl->export_name;
                            inst_type_instance
                                ->types[inst_type_instance->types_count - 1]
                                ->type_specific.resource->name =
                                export_name->tag
                                    ? export_name->exported.versioned.name->name
                                    : export_name->exported.simple.name->name;
                            inst_type_instance
                                ->exports[inst_type_instance->exports_count]
                                .exp.type =
                                inst_type_instance
                                    ->types[inst_type_instance->types_count
                                            - 1];
                            inst_type_instance->exports_count++;
                            inst_type_instance_defined_types =
                                (void *)((uint8_t *)
                                             inst_type_instance_defined_types
                                         + curr_type_size);
                        }
                        else {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "Type bound tab not recognised\n");
                        }
                        break;
                    case WASM_COMP_EXTERN_FUNC:
                        if (inst_type_instance
                                ->types[instance_decl->decl.export_decl
                                            ->extern_desc->extern_desc.func
                                            .type_idx]
                                ->type
                            != COMPONENT_VAL_TYPE_FUNCTION) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "Function export extern declaration type index "
                                "does not correspond to a function type\n");
                            return 0;
                        }
                        inst_type_instance
                            ->funcs[inst_type_instance->func_count] =
                            inst_type_instance
                                ->types[instance_decl->decl.export_decl
                                            ->extern_desc->extern_desc.func
                                            .type_idx]
                                ->type_specific.function;
                        inst_type_instance->func_count++;
                        inst_type_instance
                            ->exports[inst_type_instance->exports_count]
                            .exp.func_type =
                            inst_type_instance
                                ->funcs[inst_type_instance->func_count];
                        inst_type_instance->exports_count++;
                        break;
                    default:
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "Only type and function exports are supported in "
                            "instance type declaration for now\n");
                        break;
                }
                break;
            default:
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Unsupported Component instance declaration type\n");
                break;
        }
    }
    types[*types_count] = curr_type;
    (*types_count)++;
    return size;
}

bool
wasm_resolve_types(WASMComponentTypeSection *type_section,
                   WASMComponentInstance *comp_instance, char *error_buf,
                   uint32 error_buf_size)
{
    uint32 idx = 0, size = 0;
    WASMComponentTypes *type = NULL;
    if (!type_section || !comp_instance) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: Invalid section or component instance\n");
        return false;
    }

    for (idx = 0; idx < type_section->count; idx++) {
        type = &type_section->types[idx];
        size = 0;
        switch (type->tag) {
            case WASM_COMP_DEF_TYPE:
                size += fill_def_type_instance(
                    comp_instance->types, &comp_instance->types_count,
                    comp_instance->defined_types,
                    comp_instance->defined_types_size, type, error_buf,
                    error_buf_size);
                break;
            case WASM_COMP_FUNC_TYPE:
                size += fill_func_type_instance(
                    comp_instance->types, &comp_instance->types_count,
                    comp_instance->defined_types,
                    comp_instance->defined_types_size, type, error_buf,
                    error_buf_size);
                break;
            case WASM_COMP_COMPONENT_TYPE:
                // TODO: No example found
                LOG_WARNING("TODO: Component type not relevant for now, no "
                            "examples found\n");
                break;
            case WASM_COMP_INSTANCE_TYPE:
                size += fill_instance_type_instance(
                    comp_instance->types, &comp_instance->types_count,
                    comp_instance->defined_types,
                    comp_instance->defined_types_size, type, comp_instance,
                    error_buf, error_buf_size);
                break;
            case WASM_COMP_RESOURCE_TYPE_SYNC:
                size += fill_resource_type_instance(
                    comp_instance->types, &comp_instance->types_count,
                    comp_instance->defined_types,
                    comp_instance->defined_types_size, type, comp_instance,
                    error_buf, error_buf_size);
                break;
            case WASM_COMP_RESOURCE_TYPE_ASYNC:
            case WASM_COMP_INVALID_TYPE:
            default:
                break;
        }
        // TODO: error handling for fill instance failure, component type not
        // taken into account for now
        if (!size && type->tag != WASM_COMP_COMPONENT_TYPE) {
            set_error_buf_ex(error_buf, error_buf_size, "Returned size is 0\n");
            return false;
        }
        comp_instance->defined_types_size -= size;
        comp_instance->defined_types =
            (void *)((uint8_t *)comp_instance->defined_types + size);
    }
    return true;
}

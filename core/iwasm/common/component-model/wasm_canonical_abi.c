/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_canonical_abi.h"
#include "wasm_memory.h"
#include <stdlib.h>
#include <string.h>

static bool
compare_same_type_record_fields(ComponentWITRecordField field_1,
                                ComponentWITRecordField field_2);
static bool
compare_same_type_records(ComponentWITRecord record_1,
                          ComponentWITRecord record_2);
static bool
compare_same_type_objects(wit_value_t elem_1, wit_value_t elem_2);

wit_value_t
wit_value_alloc(void)
{
    wit_value_t new_val = wasm_runtime_malloc(sizeof(struct ComponentWITValue));

    return new_val;
}

bool
free_wit_value(wit_value_t value)
{
    if (value == NULL) {
        return false;
    }

    switch (value->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
            if (value->prim_type == WASM_COMP_PRIMVAL_STRING) {
                if (value->value.string_value.chars != NULL) {
                    wasm_runtime_free(value->value.string_value.chars);
                }
            }
            break;
        case COMPONENT_VAL_TYPE_LIST:
            if (value->value.list_value.elems != NULL) {
                for (uint32_t i = 0; i < value->value.list_value.size; i++) {
                    free_wit_value(value->value.list_value.elems[i]);
                }
                wasm_runtime_free((void *)value->value.list_value.elems);
            }
            break;
        case COMPONENT_VAL_TYPE_OPTION:
            if (value->value.option_value.optional_elem != NULL) {
                free_wit_value(value->value.option_value.optional_elem);
            }
            break;
        case COMPONENT_VAL_TYPE_RESULT:
            if (value->value.result_value.is_err) {
                free_wit_value(value->value.result_value.result.err);
            }
            else {
                free_wit_value(value->value.result_value.result.ok);
            }
            break;
        case COMPONENT_VAL_TYPE_RECORD:
            if (value->value.record_value.fields != NULL) {
                for (uint32_t i = 0; i < value->value.record_value.size; i++) {
                    wasm_runtime_free(value->value.record_value.fields[i].key);
                    free_wit_value(value->value.record_value.fields[i].value);
                }
                wasm_runtime_free(value->value.record_value.fields);
            }
            break;
        case COMPONENT_VAL_TYPE_TUPLE:
            if (value->value.tuple_value.elems != NULL) {
                for (uint32_t i = 0; i < value->value.tuple_value.size; i++) {
                    free_wit_value(value->value.tuple_value.elems[i]);
                }
                wasm_runtime_free((void *)value->value.tuple_value.elems);
            }
            break;
        case COMPONENT_VAL_TYPE_VARIANT:
            if (value->value.variant_value.value != NULL) {
                if (value->value.variant_value.discriminator != NULL)
                    wasm_runtime_free(value->value.variant_value.discriminator);
                free_wit_value(value->value.variant_value.value);
            }
            break;
        case COMPONENT_VAL_TYPE_FLAGS:
            if (value->value.flag_value.fields != NULL) {
                for (uint32_t i = 0; i < value->value.flag_value.size; i++) {
                    wasm_runtime_free(value->value.flag_value.fields[i].key);
                    free_wit_value(value->value.flag_value.fields[i].value);
                }
                wasm_runtime_free(value->value.flag_value.fields);
            }
            break;
        default:
            break;
    }

    wasm_runtime_free(value);
    return true;
}

WASMComponentValType
wit_value_typeof(wit_value_t value)
{
    return value->type;
}

WASMComponentPrimValType
wit_value_prim_typeof(wit_value_t value)
{
    return value->prim_type;
}

wit_value_t
wit_s8_ctor(int8_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_S8;
    new_val->value.s8_value = val;

    return new_val;
}

wit_value_t
wit_s16_ctor(int16_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_S16;
    new_val->value.s16_value = val;

    return new_val;
}

wit_value_t
wit_s32_ctor(int32_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_S32;
    new_val->value.s32_value = val;

    return new_val;
}

wit_value_t
wit_s64_ctor(int64_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_S64;
    new_val->value.s64_value = val;

    return new_val;
}

wit_value_t
wit_u8_ctor(uint8_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_U8;
    new_val->value.u8_value = val;

    return new_val;
}

wit_value_t
wit_u16_ctor(uint16_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_U16;
    new_val->value.u16_value = val;

    return new_val;
}

wit_value_t
wit_u32_ctor(uint32_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_U32;
    new_val->value.u32_value = val;

    return new_val;
}

wit_value_t
wit_u64_ctor(uint64_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_U64;
    new_val->value.u64_value = val;

    return new_val;
}

wit_value_t
wit_f32_ctor(float val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_F32;
    new_val->value.f32_value = val;

    return new_val;
}

wit_value_t
wit_f64_ctor(double val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_F64;
    new_val->value.f64_value = val;

    return new_val;
}

wit_value_t
wit_char_ctor(uint32_t val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_CHAR;
    new_val->value.char_value = val;

    return new_val;
}

static bool
compare_same_type_record_fields(ComponentWITRecordField field_1,
                                ComponentWITRecordField field_2)
{
    if (field_1.key_size == field_2.key_size) {
        if (strncmp(field_1.key, field_2.key, field_1.key_size) == 0) {
            if (compare_same_type_objects(field_1.value, field_2.value)) {
                return true;
            }
        }
    }
    return false;
}

static bool
compare_same_type_records(ComponentWITRecord record_1,
                          ComponentWITRecord record_2)
{
    if ((record_1.fields == NULL || record_2.fields == NULL)
        || (record_1.size != record_2.size)) {
        return false;
    }
    for (uint32_t index = 0; index < record_1.size; index++) {
        if (!compare_same_type_record_fields(record_1.fields[index],
                                             record_2.fields[index])) {
            return false;
        }
    }
    return true;
}

static bool
compare_same_type_objects(wit_value_t elem_1, wit_value_t elem_2)
{
    if (elem_1 == NULL || elem_2 == NULL)
        return false;
    if (elem_1->type != elem_2->type)
        return false;
    switch (elem_1->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
            if (elem_1->prim_type == elem_2->prim_type)
                return true;
            return false;
        case COMPONENT_VAL_TYPE_LIST:
            if (elem_1->value.list_value.size == 0
                || elem_2->value.list_value.size == 0) {
                return true;
            }
            return compare_same_type_objects(elem_1->value.list_value.elems[0],
                                             elem_2->value.list_value.elems[0]);
        case COMPONENT_VAL_TYPE_RECORD:
            return compare_same_type_records(elem_1->value.record_value,
                                             elem_2->value.record_value);
        default:
            return true;
    }
}

wit_value_t
wit_bool_ctor(bool val)
{
    wit_value_t new_val = wit_value_alloc();

    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_val->prim_type = WASM_COMP_PRIMVAL_BOOL;
    new_val->value.bool_value = val;

    return new_val;
}

wit_value_t
wit_list_ctor(wit_value_t *elems, uint32_t size)
{
    if (size > 0 && elems == NULL) {
        return NULL;
    }

    // verify if every element of the list is the same type
    if (size > 1) {
        for (uint32_t index = 1; index < size; index++) {
            if (!compare_same_type_objects(elems[0], elems[index])) {
                return NULL; // this list has elements with different Component
                             // types
            }
        }
    }

    wit_value_t new_list = wit_value_alloc();
    if (new_list == NULL) {
        return NULL;
    }

    new_list->type = COMPONENT_VAL_TYPE_LIST;
    new_list->value.list_value.elems = elems;
    new_list->value.list_value.size = size;

    return new_list;
}

wit_value_t
wit_values_list_ctor(wit_value_t *elems, uint32_t size)
{
    if (size > 0 && elems == NULL) {
        return NULL;
    }

    wit_value_t new_list = wit_value_alloc();
    if (new_list == NULL) {
        return NULL;
    }

    new_list->type = COMPONENT_VAL_TYPE_LIST;
    new_list->value.list_value.elems = elems;
    new_list->value.list_value.size = size;

    return new_list;
}

wit_value_t
wit_string_ctor(char *string, uint32_t size_bytes, uint32_t tagged_code_units,
                StringEncoding hint_encoding)
{
    if (size_bytes > 0 && string == NULL) {
        return NULL;
    }

    wit_value_t new_string = wit_value_alloc();
    if (new_string == NULL) {
        return NULL;
    }

    new_string->value.string_value.chars =
        (char *)wasm_runtime_malloc(size_bytes + 1);
    bh_memcpy_s(new_string->value.string_value.chars, size_bytes + 1, string,
                size_bytes);
    new_string->value.string_value.chars[size_bytes] = '\0';
    new_string->type = COMPONENT_VAL_TYPE_PRIMVAL;
    new_string->prim_type = WASM_COMP_PRIMVAL_STRING;
    new_string->value.string_value.size_bytes = size_bytes;
    new_string->value.string_value.hint_encoding = hint_encoding;
    new_string->value.string_value.tagged_code_units = tagged_code_units;

    return new_string;
}

wit_value_t
wit_option_ctor(wit_value_t val)
{
    wit_value_t new_option = wit_value_alloc();
    if (new_option == NULL) {
        return NULL;
    }

    new_option->type = COMPONENT_VAL_TYPE_OPTION;
    new_option->value.option_value.optional_elem = val;

    return new_option;
}

wit_value_t
wit_result_ctor(bool is_err, wit_value_t value)
{
    wit_value_t new_val = wit_value_alloc();
    if (new_val == NULL) {
        return NULL;
    }

    new_val->type = COMPONENT_VAL_TYPE_RESULT;
    new_val->value.result_value.is_err = is_err;
    if (is_err)
        new_val->value.result_value.result.err = value;
    else
        new_val->value.result_value.result.ok = value;
    return new_val;
}

void
init_record_field(ComponentWITRecordField *field, char *key, uint32_t key_size,
                  wit_value_t value)
{
    field->key = (char *)wasm_runtime_malloc(key_size + 1);
    if (field->key == NULL) {
        return;
    }
    bh_memcpy_s(field->key, key_size + 1, key, key_size);
    field->key[key_size] = '\0';
    field->key_size = key_size;
    field->value = value;
}

wit_value_t
wit_record_ctor(ComponentWITRecordField *fields, uint32_t size)
{
    if (size > 0 && fields == NULL) {
        return NULL;
    }

    wit_value_t new_record = wit_value_alloc();
    if (new_record == NULL) {
        return NULL;
    }

    new_record->type = COMPONENT_VAL_TYPE_RECORD;
    new_record->value.record_value.fields = fields;
    new_record->value.record_value.size = size;

    return new_record;
}

wit_value_t
wit_tuple_ctor(wit_value_t *elems, uint32_t size)
{
    if (size > 0 && elems == NULL) {
        return NULL;
    }

    wit_value_t new_tuple = wit_value_alloc();
    if (new_tuple == NULL) {
        return NULL;
    }

    new_tuple->type = COMPONENT_VAL_TYPE_TUPLE;
    new_tuple->value.tuple_value.elems = elems;
    new_tuple->value.tuple_value.size = size;

    return new_tuple;
}

wit_value_t
wit_variant_ctor(char *discriminator, uint32_t discriminator_size,
                 wit_value_t value)
{
    wit_value_t new_variant = wit_value_alloc();
    if (new_variant == NULL) {
        return NULL;
    }

    new_variant->value.variant_value.discriminator =
        (char *)wasm_runtime_malloc(discriminator_size);
    bh_memcpy_s(new_variant->value.variant_value.discriminator,
                discriminator_size, discriminator, discriminator_size);
    new_variant->type = COMPONENT_VAL_TYPE_VARIANT;
    new_variant->value.variant_value.discriminator_size = discriminator_size;
    new_variant->value.variant_value.value = value;

    return new_variant;
}

wit_value_t
wit_enum_ctor(uint32_t value)
{
    wit_value_t new_enum = wit_value_alloc();
    if (new_enum == NULL) {
        return NULL;
    }

    new_enum->type = COMPONENT_VAL_TYPE_ENUM;
    new_enum->value.enum_value.value = value;

    return new_enum;
}

wit_value_t
wit_flag_ctor(ComponentWITRecordField *fields, uint32_t size)
{
    if ((size > 0 && fields == NULL) || (size == 0 && fields != NULL)) {
        return NULL;
    }

    wit_value_t new_flag = wit_value_alloc();
    if (new_flag == NULL) {
        return NULL;
    }

    new_flag->type = COMPONENT_VAL_TYPE_FLAGS;
    new_flag->value.flag_value.fields = fields;
    new_flag->value.flag_value.size = size;

    return new_flag;
}

wit_value_t
wit_resource_ctor(uint32_t value)
{
    wit_value_t new_resource = wit_value_alloc();
    if (new_resource == NULL) {
        return NULL;
    }

    new_resource->type = COMPONENT_VAL_TYPE_RESOURCE_SYNC;
    new_resource->value.resource_value.value = value;

    return new_resource;
}

wit_value_t
wit_errctx_ctor(uint32_t value)
{
    wit_value_t errctx = wit_value_alloc();
    if (errctx == NULL) {
        return NULL;
    }

    errctx->type = COMPONENT_VAL_TYPE_ERROR_CONTEXT;
    errctx->value.resource_value.value = value;

    return errctx;
}

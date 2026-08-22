/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wave_adapter.h"
#include "wave_parser.h"

typedef WAVE_STYPE YYSTYPE;

#include "wave_lexer.h"
#include "wasm_export.h"
#include <stdio.h>

bool
wave_parse_invocation_str(const char *input, wave_invocation_t *out)
{
    yyscan_t scanner;
    int ret;

    out->args = NULL;

    if (wave_lex_init(&scanner) != 0) {
        return false;
    }

    YY_BUFFER_STATE buf = wave__scan_string(input, scanner);

    // Bison parse
    ret = wave_parse(scanner, out);

    wave__delete_buffer(buf, scanner);
    wave_lex_destroy(scanner);

    return (ret == 0);
}

void
wave_free_invocation_struct(wave_invocation_t *inv)
{
    if (!inv)
        return;
    if (inv->func_name)
        wasm_runtime_free(inv->func_name);

    if (inv->args) {
        free_wit_value(inv->args);
    }
    inv->func_name = NULL;
}

bool
wave_pop_func_name(const char *input, wave_invocation_t *inv)
{
    if (!input || !inv)
        return false;

    memset(inv, 0, sizeof(wave_invocation_t));

    const char *p = input;
    while (*p && isspace((unsigned char)*p))
        p++;

    const char *start = p;
    while (*p && *p != '(' && !isspace((unsigned char)*p)) {
        p++;
    }

    size_t len = p - start;
    if (len == 0)
        return false;

    inv->func_name = wasm_runtime_malloc(len + 1);
    if (!inv->func_name)
        return false;

    memcpy(inv->func_name, start, len);
    inv->func_name[len] = '\0';

    return true;
}

static bool
coerce_primitive(wit_value_t val, WASMComponentPrimValType expected_prim)
{
    if (val->prim_type == WASM_COMP_PRIMVAL_S64) {
        int64_t raw_val = val->value.s64_value;
        switch (expected_prim) {
            case WASM_COMP_PRIMVAL_U64:
                val->prim_type = WASM_COMP_PRIMVAL_U64;
                val->value.u64_value = (uint8_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_S32:
                val->prim_type = WASM_COMP_PRIMVAL_S32;
                val->value.s32_value = (int32_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_U32:
                val->prim_type = WASM_COMP_PRIMVAL_U32;
                val->value.u32_value = (uint32_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_S16:
                val->prim_type = WASM_COMP_PRIMVAL_S16;
                val->value.s16_value = (int16_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_U16:
                val->prim_type = WASM_COMP_PRIMVAL_U16;
                val->value.u16_value = (uint16_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_S8:
                val->prim_type = WASM_COMP_PRIMVAL_S8;
                val->value.s8_value = (int8_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_U8:
                val->prim_type = WASM_COMP_PRIMVAL_U8;
                val->value.u8_value = (uint8_t)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_F64:
                val->prim_type = WASM_COMP_PRIMVAL_F64;
                val->value.f64_value = (double)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_F32:
                val->prim_type = WASM_COMP_PRIMVAL_F32;
                val->value.f32_value = (float)raw_val;
                return true;
            case WASM_COMP_PRIMVAL_S64:
                return true;
            default:
                return false;
        }
    }

    if (val->prim_type == WASM_COMP_PRIMVAL_F64) {
        double raw_val = val->value.f64_value;
        if (expected_prim == WASM_COMP_PRIMVAL_F32) {
            val->prim_type = WASM_COMP_PRIMVAL_F32;
            val->value.f32_value = (float)raw_val;
            return true;
        }
    }

    return true;
}

static bool
wave_coerce_value(wit_value_t val, WASMComponentTypeInstance *expected_type)
{
    if (!val || !expected_type)
        return false;

    if (expected_type->type == COMPONENT_VAL_TYPE_PRIMVAL) {
        return coerce_primitive(val, expected_type->type_specific.primval);
    }

    if (expected_type->type == COMPONENT_VAL_TYPE_TUPLE) {
        WASMComponentTupleInstance *expected_tuple =
            expected_type->type_specific.tuple;

        if (val->type != COMPONENT_VAL_TYPE_TUPLE)
            return false;
        if (val->value.tuple_value.size != expected_tuple->count)
            return false;

        for (uint32_t i = 0; i < expected_tuple->count; i++) {
            if (!wave_coerce_value(val->value.tuple_value.elems[i],
                                   expected_tuple->element_types[i])) {
                return false;
            }
        }
        return true;
    }

    if (expected_type->type == COMPONENT_VAL_TYPE_RECORD) {
        WASMComponentRecordInstance *expected_record =
            expected_type->type_specific.record;

        if (val->type != COMPONENT_VAL_TYPE_RECORD)
            return false;

        if (val->value.record_value.size != expected_record->count)
            return false;
        // -------------------------------------------------------------------------------

        for (uint32_t i = 0; i < val->value.record_value.size; i++) {
            ComponentWITRecordField *parsed_field =
                &val->value.record_value.fields[i];

            bool found = false;
            for (uint32_t j = 0; j < expected_record->count; j++) {

                const char *expected_name =
                    expected_record->fields[j].label->name;

                if (strcmp(parsed_field->key, expected_name) == 0) {
                    if (!wave_coerce_value(parsed_field->value,
                                           expected_record->fields[j].type)) {
                        return false;
                    }
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }

    if (expected_type->type == COMPONENT_VAL_TYPE_LIST) {
        WASMComponentListInstance *expected_list =
            expected_type->type_specific.list;

        if (val->type != COMPONENT_VAL_TYPE_LIST)
            return false;

        for (uint32_t i = 0; i < val->value.list_value.size; i++) {
            if (!wave_coerce_value(val->value.list_value.elems[i],
                                   expected_list->element_type)) {
                return false;
            }
        }
        return true;
    }

    return true;
}

bool
wave_coerce_invocation(const WASMComponentInstance *component_inst,
                       wave_invocation_t *inv,
                       WASMComponentParamListInstance *params)
{
    if (!inv || !params)
        return false;
    char buf[128];

    for (uint32_t i = 0; i < inv->arg_count; i++) {
        WASMComponentTypeInstance *expected_type = params->params[i].type;

        if (!wave_coerce_value(inv->args->value.list_value.elems[i],
                               expected_type))
            return false;
    }

    return true;
}
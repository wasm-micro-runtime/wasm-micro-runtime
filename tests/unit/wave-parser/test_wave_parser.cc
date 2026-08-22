/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>

extern "C" {
    #include "wave_adapter.h"
    #include "wasm_export.h"
}

class WaveParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        RuntimeInitArgs init_args;
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_System_Allocator;
        
        bool success = wasm_runtime_full_init(&init_args);
        ASSERT_TRUE(success);
    }

    void TearDown() override {
        wasm_runtime_destroy();
    }
};

TEST_F(WaveParserTest, ParseSimpleInvocation) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool success = wave_parse_invocation_str("my_func(42, 3.14)", &inv);
    
    ASSERT_TRUE(success);
    EXPECT_STREQ(inv.func_name, "my_func");
    
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceInvocationIntToFloat) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool parse_success = wave_parse_invocation_str("my_func(2)", &inv);
    ASSERT_TRUE(parse_success);
    
    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    expected_type.type_specific.primval = WASM_COMP_PRIMVAL_F64;
    
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + sizeof(WASMComponentLabelValTypeInstance) * 4;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    ASSERT_NE(params, nullptr);
    memset(params, 0, param_list_size);
    
    params->count = 1;
    params->params[0].type = &expected_type;
    
    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);
    
    wit_value_t coerced_arg = inv.args->value.list_value.elems[0];
    EXPECT_EQ(coerced_arg->prim_type, WASM_COMP_PRIMVAL_F64);
    EXPECT_DOUBLE_EQ(coerced_arg->value.f64_value, 2.0);
    
    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, ParseEmptyArguments) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool success = wave_parse_invocation_str("ping()", &inv);
    
    ASSERT_TRUE(success);
    EXPECT_STREQ(inv.func_name, "ping");
    ASSERT_NE(inv.args, nullptr);
    EXPECT_EQ(inv.arg_count, 0);
    EXPECT_EQ(inv.args->value.list_value.size, 0);
    
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, ParseComplexTypes) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool success = wave_parse_invocation_str("test_types(true, \"hello\", [1, 2], (3, 4))", &inv);
    
    ASSERT_TRUE(success);
    EXPECT_EQ(inv.arg_count, 4);
    
    wit_value_t arg_bool = inv.args->value.list_value.elems[0];
    EXPECT_EQ(arg_bool->type, COMPONENT_VAL_TYPE_PRIMVAL);
    EXPECT_EQ(arg_bool->value.bool_value, true);
    
    wit_value_t arg_str = inv.args->value.list_value.elems[1];
    EXPECT_EQ(arg_str->type, COMPONENT_VAL_TYPE_PRIMVAL);
    EXPECT_EQ(arg_str->prim_type, WASM_COMP_PRIMVAL_STRING);
    EXPECT_STREQ(arg_str->value.string_value.chars, "hello");
    
    wit_value_t arg_list = inv.args->value.list_value.elems[2];
    EXPECT_EQ(arg_list->type, COMPONENT_VAL_TYPE_LIST);
    EXPECT_EQ(arg_list->value.list_value.size, 2);
    
    wit_value_t arg_tuple = inv.args->value.list_value.elems[3];
    EXPECT_EQ(arg_tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    EXPECT_EQ(arg_tuple->value.tuple_value.size, 2);
    
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceS64ToSmallPrimitives) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool parse_success = wave_parse_invocation_str("multi_prim(100, 200, 300)", &inv);
    ASSERT_TRUE(parse_success);
    
    WASMComponentTypeInstance type_f32, type_s16, type_u8;
    memset(&type_f32, 0, sizeof(WASMComponentTypeInstance));
    memset(&type_s16, 0, sizeof(WASMComponentTypeInstance));
    memset(&type_u8, 0, sizeof(WASMComponentTypeInstance));
    
    type_f32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f32.type_specific.primval = WASM_COMP_PRIMVAL_F32;
    
    type_s16.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_s16.type_specific.primval = WASM_COMP_PRIMVAL_S16;
    
    type_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;

    WASMComponentParamListInstance *params = nullptr;
    
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 3);
    
    params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    ASSERT_NE(params, nullptr);
    memset(params, 0, param_list_size);
    // -----------------------------------
    
    params->count = 3;
    params->params[0].type = &type_f32;
    params->params[1].type = &type_s16;
    params->params[2].type = &type_u8;
    
    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);
    
    EXPECT_EQ(inv.args->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_F32);
    EXPECT_FLOAT_EQ(inv.args->value.list_value.elems[0]->value.f32_value, 100.0f);
    
    EXPECT_EQ(inv.args->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_S16);
    EXPECT_EQ(inv.args->value.list_value.elems[1]->value.s16_value, 200);
    
    EXPECT_EQ(inv.args->value.list_value.elems[2]->prim_type, WASM_COMP_PRIMVAL_U8);
    EXPECT_EQ(inv.args->value.list_value.elems[2]->value.u8_value, 44); 
    
    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceTupleElements) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    bool parse_success = wave_parse_invocation_str("process_tuple((5, 10))", &inv);
    ASSERT_TRUE(parse_success);
    
    WASMComponentTypeInstance type_f64, type_s32;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    memset(&type_s32, 0, sizeof(WASMComponentTypeInstance));
    
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;
    type_s32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_s32.type_specific.primval = WASM_COMP_PRIMVAL_S32;
    
    WASMComponentTupleInstance expected_tuple;
    memset(&expected_tuple, 0, sizeof(WASMComponentTupleInstance));
    expected_tuple.count = 2;
    
    WASMComponentTypeInstance* tuple_types[2] = {&type_f64, &type_s32};
    expected_tuple.element_types = tuple_types;
    
    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_TUPLE;
    expected_type.type_specific.tuple = &expected_tuple;
    
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + sizeof(WASMComponentLabelValTypeInstance);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    
    params->count = 1;
    params->params[0].type = &expected_type;
    
    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);
    
    wit_value_t coerced_tuple = inv.args->value.list_value.elems[0];
    EXPECT_EQ(coerced_tuple->value.tuple_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_F64);
    EXPECT_DOUBLE_EQ(coerced_tuple->value.tuple_value.elems[0]->value.f64_value, 5.0);
    EXPECT_EQ(coerced_tuple->value.tuple_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_S32);
    EXPECT_EQ(coerced_tuple->value.tuple_value.elems[1]->value.s32_value, 10);
    
    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, FailOnArgCountMismatch) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));
    
    wave_parse_invocation_str("my_func(1, 2, 3)", &inv);
    
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + sizeof(WASMComponentLabelValTypeInstance) * 2;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    
    params->count = 2; 
    
    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    
    ASSERT_FALSE(coerce_success);
    
    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, ParseNegativeNumbers) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool success = wave_parse_invocation_str("math_op(-42, -3.1415)", &inv);

    ASSERT_TRUE(success);
    EXPECT_EQ(inv.arg_count, 2);

    wit_value_t arg1 = inv.args->value.list_value.elems[0];
    EXPECT_EQ(arg1->prim_type, WASM_COMP_PRIMVAL_S64);
    EXPECT_EQ(arg1->value.s64_value, -42);

    wit_value_t arg2 = inv.args->value.list_value.elems[1];
    EXPECT_EQ(arg2->prim_type, WASM_COMP_PRIMVAL_F64);
    EXPECT_DOUBLE_EQ(arg2->value.f64_value, -3.1415);

    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceNegativeIntegers) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("set_temps(-10, -50)", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_s32, type_s8;
    memset(&type_s32, 0, sizeof(WASMComponentTypeInstance));
    memset(&type_s8, 0, sizeof(WASMComponentTypeInstance));

    type_s32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_s32.type_specific.primval = WASM_COMP_PRIMVAL_S32;

    type_s8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_s8.type_specific.primval = WASM_COMP_PRIMVAL_S8;

    WASMComponentParamListInstance *params = nullptr;
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    ASSERT_NE(params, nullptr);
    memset(params, 0, param_list_size);

    params->count = 2;
    params->params[0].type = &type_s32;
    params->params[1].type = &type_s8;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    EXPECT_EQ(inv.args->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_S32);
    EXPECT_EQ(inv.args->value.list_value.elems[0]->value.s32_value, -10);

    EXPECT_EQ(inv.args->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_S8);
    EXPECT_EQ(inv.args->value.list_value.elems[1]->value.s8_value, -50);

    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceEmptyList) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_items([])", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f32;
    memset(&type_f32, 0, sizeof(WASMComponentTypeInstance));
    type_f32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f32.type_specific.primval = WASM_COMP_PRIMVAL_F32;

    WASMComponentListInstance expected_list;
    memset(&expected_list, 0, sizeof(WASMComponentListInstance));
    expected_list.element_type = &type_f32;

    WASMComponentTypeInstance type_list;
    memset(&type_list, 0, sizeof(WASMComponentTypeInstance));
    type_list.type = COMPONENT_VAL_TYPE_LIST;
    type_list.type_specific.list = &expected_list;

    WASMComponentParamListInstance *params = nullptr;
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    ASSERT_NE(params, nullptr);
    memset(params, 0, param_list_size);

    params->count = 1;
    params->params[0].type = &type_list;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, FailOnListElementTypeMismatch) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_items([1, 2, 3])", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_string;
    memset(&type_string, 0, sizeof(WASMComponentTypeInstance));
    type_string.type = COMPONENT_VAL_TYPE_PRIMVAL;

    WASMComponentListInstance expected_list;
    memset(&expected_list, 0, sizeof(WASMComponentListInstance));
    expected_list.element_type = &type_string;

    WASMComponentTypeInstance type_list;
    memset(&type_list, 0, sizeof(WASMComponentTypeInstance));
    type_list.type = COMPONENT_VAL_TYPE_LIST;
    type_list.type_specific.list = &expected_list;

    WASMComponentParamListInstance *params = nullptr;
    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    ASSERT_NE(params, nullptr);
    memset(params, 0, param_list_size);

    params->count = 1;
    params->params[0].type = &type_list;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_FALSE(coerce_success);

    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceSimpleRecord) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_point({x: 10, y: 20})", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *expected_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    ASSERT_NE(expected_record, nullptr);
    memset(expected_record, 0, record_size);

    expected_record->count = 2;
    
    expected_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[0].label->name = strdup("x");
    expected_record->fields[0].label->name_len = 1;
    expected_record->fields[0].type = &type_f64;
    
    expected_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[1].label->name = strdup("y");
    expected_record->fields[1].label->name_len = 1;
    expected_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_RECORD;
    expected_type.type_specific.record = expected_record;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &expected_type;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    wit_value_t coerced_record = inv.args->value.list_value.elems[0];
    EXPECT_EQ(coerced_record->type, COMPONENT_VAL_TYPE_RECORD);
    EXPECT_EQ(coerced_record->value.record_value.size, 2);

    EXPECT_EQ(coerced_record->value.record_value.fields[0].value->prim_type, WASM_COMP_PRIMVAL_F64);
    EXPECT_DOUBLE_EQ(coerced_record->value.record_value.fields[0].value->value.f64_value, 10.0);

    EXPECT_EQ(coerced_record->value.record_value.fields[1].value->prim_type, WASM_COMP_PRIMVAL_F64);
    EXPECT_DOUBLE_EQ(coerced_record->value.record_value.fields[1].value->value.f64_value, 20.0);

    free(expected_record->fields[0].label->name);
    wasm_runtime_free(expected_record->fields[0].label);
    free(expected_record->fields[1].label->name);
    wasm_runtime_free(expected_record->fields[1].label);
    
    wasm_runtime_free(params);
    wasm_runtime_free(expected_record);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceListOfRecords) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("draw_shape([{x: 1, y: 2}, {x: 3, y: 4}])", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *expected_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    memset(expected_record, 0, record_size);

    expected_record->count = 2;
    
    expected_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[0].label->name = strdup("x");
    expected_record->fields[0].label->name_len = 1;
    expected_record->fields[0].type = &type_f64;
    
    expected_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[1].label->name = strdup("y");
    expected_record->fields[1].label->name_len = 1;
    expected_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance type_record;
    memset(&type_record, 0, sizeof(WASMComponentTypeInstance));
    type_record.type = COMPONENT_VAL_TYPE_RECORD;
    type_record.type_specific.record = expected_record;

    WASMComponentListInstance expected_list;
    memset(&expected_list, 0, sizeof(WASMComponentListInstance));
    expected_list.element_type = &type_record;

    WASMComponentTypeInstance type_list;
    memset(&type_list, 0, sizeof(WASMComponentTypeInstance));
    type_list.type = COMPONENT_VAL_TYPE_LIST;
    type_list.type_specific.list = &expected_list;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &type_list;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    wit_value_t list_node = inv.args->value.list_value.elems[0];
    EXPECT_EQ(list_node->type, COMPONENT_VAL_TYPE_LIST);
    EXPECT_EQ(list_node->value.list_value.size, 2);

    wit_value_t second_record = list_node->value.list_value.elems[1];
    EXPECT_DOUBLE_EQ(second_record->value.record_value.fields[0].value->value.f64_value, 3.0);
    EXPECT_DOUBLE_EQ(second_record->value.record_value.fields[1].value->value.f64_value, 4.0);

    free(expected_record->fields[0].label->name);
    wasm_runtime_free(expected_record->fields[0].label);
    free(expected_record->fields[1].label->name);
    wasm_runtime_free(expected_record->fields[1].label);

    wasm_runtime_free(params);
    wasm_runtime_free(expected_record);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, FailOnMissingRecordField) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_point({x: 10})", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *expected_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    memset(expected_record, 0, record_size);

    expected_record->count = 2;
    
    expected_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[0].label->name = strdup("x");
    expected_record->fields[0].label->name_len = 1;
    expected_record->fields[0].type = &type_f64;
    
    expected_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[1].label->name = strdup("y");
    expected_record->fields[1].label->name_len = 1;
    expected_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_RECORD;
    expected_type.type_specific.record = expected_record;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &expected_type;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    
    ASSERT_FALSE(coerce_success);

    free(expected_record->fields[0].label->name);
    wasm_runtime_free(expected_record->fields[0].label);
    free(expected_record->fields[1].label->name);
    wasm_runtime_free(expected_record->fields[1].label);

    wasm_runtime_free(params);
    wasm_runtime_free(expected_record);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceNestedRecords) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("draw_line({start: {x: 1, y: 2}, end: {x: 3, y: 4}})", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t inner_record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *inner_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(inner_record_size);
    memset(inner_record, 0, inner_record_size);
    inner_record->count = 2;
    
    inner_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    inner_record->fields[0].label->name = strdup("x");
    inner_record->fields[0].label->name_len = 1;
    inner_record->fields[0].type = &type_f64;
    
    inner_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    inner_record->fields[1].label->name = strdup("y");
    inner_record->fields[1].label->name_len = 1;
    inner_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance type_inner_record;
    memset(&type_inner_record, 0, sizeof(WASMComponentTypeInstance));
    type_inner_record.type = COMPONENT_VAL_TYPE_RECORD;
    type_inner_record.type_specific.record = inner_record;

    size_t outer_record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *outer_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(outer_record_size);
    memset(outer_record, 0, outer_record_size);
    outer_record->count = 2;

    outer_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    outer_record->fields[0].label->name = strdup("start");
    outer_record->fields[0].label->name_len = 5;
    outer_record->fields[0].type = &type_inner_record;

    outer_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    outer_record->fields[1].label->name = strdup("end");
    outer_record->fields[1].label->name_len = 3;
    outer_record->fields[1].type = &type_inner_record;

    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_RECORD;
    expected_type.type_specific.record = outer_record;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &expected_type;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    free(inner_record->fields[0].label->name);
    wasm_runtime_free(inner_record->fields[0].label);
    free(inner_record->fields[1].label->name);
    wasm_runtime_free(inner_record->fields[1].label);
    wasm_runtime_free(inner_record);

    free(outer_record->fields[0].label->name);
    wasm_runtime_free(outer_record->fields[0].label);
    free(outer_record->fields[1].label->name);
    wasm_runtime_free(outer_record->fields[1].label);
    wasm_runtime_free(outer_record);

    wasm_runtime_free(params);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, CoerceRecordOutOfOrderFields) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_point({y: 20, x: 10})", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *expected_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    memset(expected_record, 0, record_size);

    expected_record->count = 2;
    
    expected_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[0].label->name = strdup("x");
    expected_record->fields[0].label->name_len = 1;
    expected_record->fields[0].type = &type_f64;
    
    expected_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[1].label->name = strdup("y");
    expected_record->fields[1].label->name_len = 1;
    expected_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_RECORD;
    expected_type.type_specific.record = expected_record;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &expected_type;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_TRUE(coerce_success);

    free(expected_record->fields[0].label->name);
    wasm_runtime_free(expected_record->fields[0].label);
    free(expected_record->fields[1].label->name);
    wasm_runtime_free(expected_record->fields[1].label);
    wasm_runtime_free(params);
    wasm_runtime_free(expected_record);
    wave_free_invocation_struct(&inv);
}

TEST_F(WaveParserTest, FailOnTooManyRecordFields) {
    wave_invocation_t inv;
    memset(&inv, 0, sizeof(wave_invocation_t));

    bool parse_success = wave_parse_invocation_str("process_point({x: 10, y: 20, z: 30})", &inv);
    ASSERT_TRUE(parse_success);

    WASMComponentTypeInstance type_f64;
    memset(&type_f64, 0, sizeof(WASMComponentTypeInstance));
    type_f64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    type_f64.type_specific.primval = WASM_COMP_PRIMVAL_F64;

    size_t record_size = sizeof(WASMComponentRecordInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 2);
    WASMComponentRecordInstance *expected_record = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    memset(expected_record, 0, record_size);

    expected_record->count = 2; 
    
    expected_record->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[0].label->name = strdup("x");
    expected_record->fields[0].label->name_len = 1;
    expected_record->fields[0].type = &type_f64;
    
    expected_record->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    expected_record->fields[1].label->name = strdup("y");
    expected_record->fields[1].label->name_len = 1;
    expected_record->fields[1].type = &type_f64;

    WASMComponentTypeInstance expected_type;
    memset(&expected_type, 0, sizeof(WASMComponentTypeInstance));
    expected_type.type = COMPONENT_VAL_TYPE_RECORD;
    expected_type.type_specific.record = expected_record;

    size_t param_list_size = sizeof(WASMComponentParamListInstance) + (sizeof(WASMComponentLabelValTypeInstance) * 1);
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(param_list_size);
    memset(params, 0, param_list_size);
    params->count = 1;
    params->params[0].type = &expected_type;

    bool coerce_success = wave_coerce_invocation(nullptr, &inv, params);
    ASSERT_FALSE(coerce_success);

    free(expected_record->fields[0].label->name);
    wasm_runtime_free(expected_record->fields[0].label);
    free(expected_record->fields[1].label->name);
    wasm_runtime_free(expected_record->fields[1].label);
    wasm_runtime_free(params);
    wasm_runtime_free(expected_record);
    wave_free_invocation_struct(&inv);
}
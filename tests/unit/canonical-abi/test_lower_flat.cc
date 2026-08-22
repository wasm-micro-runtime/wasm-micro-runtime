/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include "../component/helpers.h"
#include "wasm_memory.h"
#include "wasm_component_flat.h"
#include <vector>
#include <iostream>

class ComponentInstantiationLowerFlatTest : public testing::Test {
  public:
    ComponentInstantiationLowerFlatTest() {}
    ~ComponentInstantiationLowerFlatTest() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;
    bool exception = false;
    std::unique_ptr<ComponentHelper> helper;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    std::string loaded_wasm_file;
    std::string memory_layout_file;

    virtual void SetUp() {
        helper = std::make_unique<ComponentHelper>();
        helper->do_setup();
        loaded_wasm_file = "stored.wasm";
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlat) {
    helper->reset_component();
    bool ret = helper->read_wasm_file(loaded_wasm_file.c_str());
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    auto memories = helper->get_memories();
    ASSERT_GT(memories.size(), 0);

    WASMMemoryInstance *mem = memories.front();
    memories.pop_back();

    ASSERT_NE(mem, nullptr);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatU8) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup type for u8
    WASMComponentTypeInstance u8_type;
    memset(&u8_type, 0, sizeof(WASMComponentTypeInstance));
    u8_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    u8_type.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    u8_type.alignment = compute_alignment(&u8_type);
    u8_type.elem_size = compute_elem_size(&u8_type);

    // 3. Get LiftLowerContext using the function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 4. Create wit_value_t with u8 value 42
    uint8_t val = 42;
    wit_value_t input = wit_u8_ctor(val);
    ASSERT_NE(input, nullptr);

    // 5. Initialize CoreValueList to receive output
    CoreValueList out;
    cvl_init(&out);

    // 6. Call lower_flat
    bool success = lower_flat(&cx, &u8_type, &out, input);

    // 7. Verify
    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 1);                       // u8 flattens to 1 core value
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);  // u8 is passed as i32
    ASSERT_EQ(out.values[0].val.i32, val);         // Value preserved

    // 8. Round-trip verification: lift
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted = nullptr;
    bool lift_success = lift_flat(&cx, &vi, &u8_type, &lifted);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted, nullptr);
    ASSERT_EQ(lifted->value.u8_value, val);
    ASSERT_TRUE(vi_done(&vi));

    // Cleanup
    free_wit_value(input);
    free_wit_value(lifted);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatString) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup type for string
    WASMComponentTypeInstance string_type;
    memset(&string_type, 0, sizeof(WASMComponentTypeInstance));
    string_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    string_type.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    string_type.alignment = compute_alignment(&string_type);
    string_type.elem_size = compute_elem_size(&string_type);

    // 3. Get LiftLowerContext using the function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    WASMFunctionInstance *realloc_func = cx.canonical_opts->lift_lower_opts->realloc_func;
    ASSERT_NE(realloc_func, nullptr) << "cabi_realloc function required for string lowering";

    // 4. Create wit_value_t with string value
    const char *test_string = "Hello, Lower!";
    wit_value_t input = wit_string_ctor(
        strdup(test_string),        // chars
        strlen(test_string),        // size_bytes
        strlen(test_string),        // tagged_code_units (UTF-8 = byte count)
        ENCODING_UTF_8              // hint_encoding
    );
    ASSERT_NE(input, nullptr);

    // 5. Initialize CoreValueList to receive output
    CoreValueList out;
    cvl_init(&out);

    // 6. Call lower_flat
    bool success = lower_flat(&cx, &string_type, &out, input);

    // 7. Verify the flat output
    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 2);                       // String flattens to 2 values: (ptr, tagged_code_units)
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);  // ptr
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32);  // tagged_code_units

    uint32_t ptr = out.values[0].val.i32;
    uint32_t tagged_code_units = out.values[1].val.i32;

    // Verify pointer is valid and length matches
    ASSERT_GT(ptr, 0);
    ASSERT_EQ(tagged_code_units, strlen(test_string));

    // 8. Verify string bytes were written to memory
    uint8_t *mem_data = mem->memory_data;
    ASSERT_EQ(memcmp(mem_data + ptr, test_string, strlen(test_string)), 0);

    // 9. Round-trip verification: lift
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted = nullptr;
    bool lift_success = lift_flat(&cx, &vi, &string_type, &lifted);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted, nullptr);
    ASSERT_STREQ(lifted->value.string_value.chars, test_string);
    ASSERT_EQ(lifted->value.string_value.size_bytes, strlen(test_string));
    ASSERT_TRUE(vi_done(&vi));

    // Cleanup
    free_wit_value(input);
    free_wit_value(lifted);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatValuesRecord) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    uint32_t field_count = 2;
    WASMComponentRecordInstance *rec_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentRecordInstance) + field_count * sizeof(WASMComponentLabelValTypeInstance));
    rec_inst->count = field_count;
    rec_inst->fields[0].label = nullptr; 
    rec_inst->fields[0].type = &t_u8;
    rec_inst->fields[1].label = nullptr;
    rec_inst->fields[1].type = &t_u32;

    WASMComponentTypeInstance t_record;
    memset(&t_record, 0, sizeof(WASMComponentTypeInstance));
    t_record.type = COMPONENT_VAL_TYPE_RECORD;
    t_record.type_specific.record = rec_inst;

    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    params->count = param_count;
    params->params[0].type = &t_record;
    params->params[0].label = nullptr;

    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    ComponentWITRecordField *fields = (ComponentWITRecordField *)wasm_runtime_malloc(2 * sizeof(ComponentWITRecordField));
    fields[0].value = wit_u8_ctor(10);
    fields[0].key = strdup("a");
    fields[1].value = wit_u32_ctor(200);
    fields[1].key = strdup("b");
    
    wit_value_t rec_val = wit_record_ctor(fields, 2);
    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = rec_val;
    wit_value_t input_list = wit_list_ctor(elems, 1);

    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 2);
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 10);
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[1].val.i32, 200);

    free_wit_value(input_list);
    wasm_runtime_free(params);
    wasm_runtime_free(rec_inst);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatValuesTuple) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    uint32_t elem_count = 2;
    WASMComponentTupleInstance *tuple_inst = (WASMComponentTupleInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentTupleInstance) + elem_count * sizeof(WASMComponentTypeInstance*));
    tuple_inst->count = elem_count;
    tuple_inst->element_types = (WASMComponentTypeInstance**)(tuple_inst + 1);
    tuple_inst->element_types[0] = &t_u8;
    tuple_inst->element_types[1] = &t_u32;

    WASMComponentTypeInstance t_tuple;
    memset(&t_tuple, 0, sizeof(WASMComponentTypeInstance));
    t_tuple.type = COMPONENT_VAL_TYPE_TUPLE;
    t_tuple.type_specific.tuple = tuple_inst;

    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    params->count = param_count;
    params->params[0].type = &t_tuple;
    params->params[0].label = nullptr;

    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t *tuple_elems = (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
    tuple_elems[0] = wit_u8_ctor(55);
    tuple_elems[1] = wit_u32_ctor(999);
    
    wit_value_t tuple_val = wit_value_alloc();
    tuple_val->type = COMPONENT_VAL_TYPE_TUPLE;
    tuple_val->value.tuple_value.elems = tuple_elems;
    tuple_val->value.tuple_value.size = 2;

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = tuple_val;
    wit_value_t input_list = wit_list_ctor(elems, 1);

    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 2);
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 55);
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[1].val.i32, 999);

    free_wit_value(input_list);
    wasm_runtime_free(params);
    wasm_runtime_free(tuple_inst);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatValuesVariant) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    WASMComponentTypeInstance t_f32;
    memset(&t_f32, 0, sizeof(WASMComponentTypeInstance));
    t_f32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_f32.type_specific.primval = WASM_COMP_PRIMVAL_F32;
    t_f32.alignment = 4; t_f32.elem_size = 4;

    uint32_t case_count = 2;
    WASMComponentVariantInstance *var_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(
        offsetof(WASMComponentVariantInstance, cases) + (case_count * sizeof(WASMComponentCaseValInstance)));
    var_inst->count = case_count;
    
    var_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    char circle_name[] = "circle"; 
    var_inst->cases[0].label->name = circle_name;
    var_inst->cases[0].label->name_len = 6;
    var_inst->cases[0].value_type = &t_f32;

    char square_name[] = "square"; 
    var_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[1].label->name = square_name;
    var_inst->cases[1].label->name_len = 6;
    var_inst->cases[1].value_type = &t_f32;

    WASMComponentTypeInstance t_variant;
    memset(&t_variant, 0, sizeof(WASMComponentTypeInstance));
    t_variant.type = COMPONENT_VAL_TYPE_VARIANT;
    t_variant.type_specific.variant = var_inst;

    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    params->count = param_count;
    params->params[0].type = &t_variant;
    params->params[0].label = nullptr;

    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t payload = wit_f32_ctor(32.5f);
    wit_value_t variant_val = wit_variant_ctor(square_name, 6, payload);
    
    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = variant_val;
    wit_value_t input_list = wit_list_ctor(elems, 1);

    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 2); 
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32); 
    ASSERT_EQ(out.values[0].val.i32, 1); 
    ASSERT_EQ(out.values[1].type, CORE_TYPE_F32);
    ASSERT_FLOAT_EQ(out.values[1].val.f32, 32.5f);

    free_wit_value(input_list);
    wasm_runtime_free(params);
    wasm_runtime_free(var_inst->cases[0].label);
    wasm_runtime_free(var_inst->cases[1].label);
    wasm_runtime_free(var_inst);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatValuesEnum) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    uint32_t count = 3;
    WASMComponentEnumType *enum_inst = (WASMComponentEnumType *)wasm_runtime_malloc(sizeof(WASMComponentEnumType));
    enum_inst->count = count;
    enum_inst->labels = (WASMComponentCoreName *)wasm_runtime_malloc(count * sizeof(WASMComponentCoreName));
    const char* labels[] = {"red", "green", "blue"};
    for(uint32_t i = 0; i < count; i++) {
        enum_inst->labels[i].name = strdup(labels[i]);
        enum_inst->labels[i].name_len = (uint32_t)strlen(labels[i]);
    }

    WASMComponentTypeInstance t_enum;
    memset(&t_enum, 0, sizeof(WASMComponentTypeInstance));
    t_enum.type = COMPONENT_VAL_TYPE_ENUM;
    t_enum.type_specific.enum_type = enum_inst;

    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    params->count = param_count;
    params->params[0].type = &t_enum;
    params->params[0].label = nullptr;

    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t enum_val = wit_enum_ctor(1); // green
    
    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = enum_val;
    wit_value_t input_list = wit_list_ctor(elems, 1);

    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 1);
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 1);

    free_wit_value(input_list);
    wasm_runtime_free(params);
    for(uint32_t i=0; i<count; i++) free(enum_inst->labels[i].name);
    wasm_runtime_free(enum_inst->labels);
    wasm_runtime_free(enum_inst);
}

TEST_F(ComponentInstantiationLowerFlatTest, TestLowerFlatValuesResult) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    WASMComponentResultInstance res_inst;
    res_inst.result_type = &t_u32;
    res_inst.error_type = &t_u8;

    WASMComponentTypeInstance t_result;
    memset(&t_result, 0, sizeof(WASMComponentTypeInstance));
    t_result.type = COMPONENT_VAL_TYPE_RESULT;
    t_result.type_specific.result = &res_inst;

    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    params->count = param_count;
    params->params[0].type = &t_result;
    params->params[0].label = nullptr;

    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t payload = wit_u32_ctor(100);
    wit_value_t result_val = wit_result_ctor(false, payload);
    
    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = result_val;
    wit_value_t input_list = wit_list_ctor(elems, 1);

    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);
    ASSERT_EQ(out.count, 2); 
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32); 
    ASSERT_EQ(out.values[0].val.i32, 0); 
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32); 
    ASSERT_EQ(out.values[1].val.i32, 100); 

    free_wit_value(input_list);
    wasm_runtime_free(params);
}
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

class ComponentInstantiationLowerFlatValuesTest : public testing::Test {
  public:
    ComponentInstantiationLowerFlatValuesTest() {}
    ~ComponentInstantiationLowerFlatValuesTest() {}
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

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesSetup) {
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

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesU32) {
    // 1. Runtime Initialization
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Define Type: u32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // 3. Create Param List with single u32 parameter
    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_u32;
    params->params[0].label = nullptr;

    // 4. Setup Context using function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 5. Create input wit_value_t list containing a single u32
    wit_value_t u32_value = wit_u32_ctor(42);
    ASSERT_NE(u32_value, nullptr);

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = u32_value;
    wit_value_t input_list = wit_list_ctor(elems, 1);
    ASSERT_NE(input_list, nullptr);

    // 6. Execute lower_flat_values
    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);

    // 7. Verify output CoreValueList
    ASSERT_EQ(out.count, 1);
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 42);

    // 8. Round-trip: lift the values back
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted_result = nullptr;
    bool lift_success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &lifted_result);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted_result, nullptr);

    ASSERT_EQ(lifted_result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->value.u32_value, 42);

    // 9. Cleanup
    free_wit_value(input_list);
    free_wit_value(lifted_result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesMultipleParamsSameType) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // Define Type: u32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // Create Param List with (u32, u32)
    uint32_t param_count = 2;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_u32;
    params->params[0].label = nullptr;
    params->params[1].type = &t_u32;
    params->params[1].label = nullptr;

    // Setup Context using function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // Create input wit_value_t list containing (u32=100, u32=200)
    wit_value_t u32_value1 = wit_u32_ctor(100);
    wit_value_t u32_value2 = wit_u32_ctor(200);
    ASSERT_NE(u32_value1, nullptr);
    ASSERT_NE(u32_value2, nullptr);

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
    elems[0] = u32_value1;
    elems[1] = u32_value2;
    wit_value_t input_list = wit_list_ctor(elems, 2);
    ASSERT_NE(input_list, nullptr);

    // Execute lower_flat_values
    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);

    // Verify output CoreValueList
    ASSERT_EQ(out.count, 2);

    // First value: u32 = 100
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 100);

    // Second value: u32 = 200
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[1].val.i32, 200);

    // Round-trip: lift the values back
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted_result = nullptr;
    bool lift_success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &lifted_result);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted_result, nullptr);

    ASSERT_EQ(lifted_result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->value.u32_value, 100);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->value.u32_value, 200);

    // Cleanup
    free_wit_value(input_list);
    free_wit_value(lifted_result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesString) {
    // 1. Runtime Initialization
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Define Type: string
    WASMComponentTypeInstance t_string;
    memset(&t_string, 0, sizeof(WASMComponentTypeInstance));
    t_string.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_string.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    t_string.alignment = 4;
    t_string.elem_size = 8;

    // 3. Create Param List with single string parameter
    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_string;
    params->params[0].label = nullptr;

    // 4. Setup Context using function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // Verify realloc is available (required for string lowering)
    WASMFunctionInstance *realloc_func = cx.canonical_opts->lift_lower_opts->realloc_func;
    ASSERT_NE(realloc_func, nullptr) << "cabi_realloc function required for string lowering";

    // 5. Create input wit_value_t list containing a single string
    const char *test_str = "Hello, Lower Values!";
    wit_value_t str_value = wit_string_ctor(
        strdup(test_str),
        strlen(test_str),
        strlen(test_str),
        ENCODING_UTF_8
    );
    ASSERT_NE(str_value, nullptr);

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = str_value;
    wit_value_t input_list = wit_list_ctor(elems, 1);
    ASSERT_NE(input_list, nullptr);

    // 6. Execute lower_flat_values
    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);

    // 7. Verify output CoreValueList (string flattens to ptr, len)
    ASSERT_EQ(out.count, 2);
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);  // ptr
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I32);  // len

    uint32_t ptr = out.values[0].val.i32;
    uint32_t len = out.values[1].val.i32;

    ASSERT_GT(ptr, 0);
    ASSERT_EQ(len, strlen(test_str));

    // Verify string was written to memory
    uint8_t *mem_data = mem->memory_data;
    ASSERT_EQ(memcmp(mem_data + ptr, test_str, len), 0);

    // 8. Round-trip: lift the values back
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted_result = nullptr;
    bool lift_success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &lifted_result);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted_result, nullptr);

    ASSERT_EQ(lifted_result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_STREQ(lifted_result->value.list_value.elems[0]->value.string_value.chars, test_str);

    // 9. Cleanup
    free_wit_value(input_list);
    free_wit_value(lifted_result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesMultipleF32) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // Define Type: f32
    WASMComponentTypeInstance t_f32;
    memset(&t_f32, 0, sizeof(WASMComponentTypeInstance));
    t_f32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_f32.type_specific.primval = WASM_COMP_PRIMVAL_F32;

    // Create Param List with (f32, f32, f32)
    uint32_t param_count = 3;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_f32;
    params->params[0].label = nullptr;
    params->params[1].type = &t_f32;
    params->params[1].label = nullptr;
    params->params[2].type = &t_f32;
    params->params[2].label = nullptr;

    // Setup Context
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // Create input values (f32, f32, f32)
    wit_value_t f32_val1 = wit_f32_ctor(3.14f);
    wit_value_t f32_val2 = wit_f32_ctor(2.71828f);
    wit_value_t f32_val3 = wit_f32_ctor(1.41421f);

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(3 * sizeof(wit_value_t));
    elems[0] = f32_val1;
    elems[1] = f32_val2;
    elems[2] = f32_val3;
    wit_value_t input_list = wit_list_ctor(elems, 3);
    ASSERT_NE(input_list, nullptr);

    // Execute lower_flat_values
    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);

    // Verify output CoreValueList
    ASSERT_EQ(out.count, 3);

    // f32 -> f32
    ASSERT_EQ(out.values[0].type, CORE_TYPE_F32);
    ASSERT_FLOAT_EQ(out.values[0].val.f32, 3.14f);

    ASSERT_EQ(out.values[1].type, CORE_TYPE_F32);
    ASSERT_FLOAT_EQ(out.values[1].val.f32, 2.71828f);

    ASSERT_EQ(out.values[2].type, CORE_TYPE_F32);
    ASSERT_FLOAT_EQ(out.values[2].val.f32, 1.41421f);

    // Round-trip: lift the values back
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted_result = nullptr;
    bool lift_success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &lifted_result);

    ASSERT_TRUE(lift_success);
    ASSERT_NE(lifted_result, nullptr);

    // Verify values match
    ASSERT_EQ(lifted_result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(lifted_result->value.list_value.elems[0]->value.f32_value, 3.14f);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(lifted_result->value.list_value.elems[1]->value.f32_value, 2.71828f);
    ASSERT_EQ(lifted_result->value.list_value.elems[2]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[2]->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(lifted_result->value.list_value.elems[2]->value.f32_value, 1.41421f);

    // Cleanup
    free_wit_value(input_list);
    free_wit_value(lifted_result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLowerFlatValuesTest, TestLowerFlatValuesHeterogeneousParams) {
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // Define Types: u32, u64, s32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    WASMComponentTypeInstance t_u64;
    memset(&t_u64, 0, sizeof(WASMComponentTypeInstance));
    t_u64.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u64.type_specific.primval = WASM_COMP_PRIMVAL_U64;
    t_u64.alignment = 8;
    t_u64.elem_size = 8;

    WASMComponentTypeInstance t_s32;
    memset(&t_s32, 0, sizeof(WASMComponentTypeInstance));
    t_s32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_s32.type_specific.primval = WASM_COMP_PRIMVAL_S32;
    t_s32.alignment = 4;
    t_s32.elem_size = 4;

    // Create Param List with (u32, u64, s32)
    uint32_t param_count = 3;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_u32;
    params->params[0].label = nullptr;
    params->params[1].type = &t_u64;
    params->params[1].label = nullptr;
    params->params[2].type = &t_s32;
    params->params[2].label = nullptr;

    // Setup Context using function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t u32_value = wit_u32_ctor(100);
    wit_value_t u64_value = wit_u64_ctor(9876543210ULL);
    wit_value_t s32_value = wit_s32_ctor(-12345);
    ASSERT_NE(u32_value, nullptr);
    ASSERT_NE(u64_value, nullptr);
    ASSERT_NE(s32_value, nullptr);

    wit_value_t *elems = (wit_value_t *)wasm_runtime_malloc(3 * sizeof(wit_value_t));
    elems[0] = u32_value;
    elems[1] = u64_value;
    elems[2] = s32_value;

    wit_value_t input_list = wit_values_list_ctor(elems, 3);
    ASSERT_NE(input_list, nullptr);

    // Execute lower_flat_values
    CoreValueList out;
    cvl_init(&out);
    bool success = lower_flat_values(&cx, MAX_FLAT_PARAMS, input_list, params, nullptr, nullptr, &out);

    ASSERT_TRUE(success);

    // Verify output CoreValueList
    ASSERT_EQ(out.count, 3);

    // First value: u32 = 100
    ASSERT_EQ(out.values[0].type, CORE_TYPE_I32);
    ASSERT_EQ(out.values[0].val.i32, 100);

    // Second value: u64 = 9876543210
    ASSERT_EQ(out.values[1].type, CORE_TYPE_I64);
    ASSERT_EQ(out.values[1].val.i64, 9876543210ULL);

    // Third value: s32 = -12345 (as i32)
    ASSERT_EQ(out.values[2].type, CORE_TYPE_I32);
    ASSERT_EQ((int32_t)out.values[2].val.i32, -12345);

    // Round-trip: lift the values back
    CoreValueIter vi;
    vi_init(&vi, out.values, out.count);

    wit_value_t lifted_result = nullptr;
    bool lift_success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &lifted_result);

    ASSERT_TRUE(lift_success) << "lift_flat_values round-trip failed";
    ASSERT_NE(lifted_result, nullptr) << "Lifted result is NULL";

    // Verify values match after round-trip
    ASSERT_EQ(lifted_result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(lifted_result->value.list_value.elems[0]->value.u32_value, 100);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(lifted_result->value.list_value.elems[1]->value.u64_value, 9876543210ULL);
    ASSERT_EQ(lifted_result->value.list_value.elems[2]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(lifted_result->value.list_value.elems[2]->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(lifted_result->value.list_value.elems[2]->value.s32_value, -12345);

    // Cleanup
    free_wit_value(input_list);
    free_wit_value(lifted_result);
    wasm_runtime_free(params);
}

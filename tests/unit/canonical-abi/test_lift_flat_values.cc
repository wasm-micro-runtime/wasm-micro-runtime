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

class ComponentInstantiationLiftFlatValuesTest : public testing::Test {
  public:
    ComponentInstantiationLiftFlatValuesTest() {}
    ~ComponentInstantiationLiftFlatValuesTest() {}
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

TEST_F(ComponentInstantiationLiftFlatValuesTest, TestLiftFlatValuesSetup) {
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

TEST_F(ComponentInstantiationLiftFlatValuesTest, TestLiftFlatValuesU32) {
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

    // 3. Create Param List with single u32
    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_u32;
    params->params[0].label = nullptr;

    // 4. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = false;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 5. Simulate Flat Values (single u32 = 42)
    CoreValue values[1];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = 42;

    CoreValueIter vi;
    vi_init(&vi, values, 1);

    // 6. Execute lift_flat_values (flat_types.count <= max_flat)
    wit_value_t result = nullptr;
    bool success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);
        
    // 7. Verify Result is an array containing single u32
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.list_value.elems[0]->value.u32_value, 42);

    // Verify iterator consumed the value
    ASSERT_TRUE(vi_done(&vi));

    // 8. Cleanup
    free_wit_value(result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLiftFlatValuesTest, TestLiftFlatValuesString) {
    // 1. Runtime Initialization
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Write test string to memory
    const char *test_str = "Hello, World!";
    uint32_t str_len = strlen(test_str);
    uint32_t str_ptr = 16; // Place string at offset 16 in memory

    uint8_t *mem_data = mem->memory_data;
    ASSERT_NE(mem_data, nullptr);
    memcpy(mem_data + str_ptr, test_str, str_len);

    // 3. Define Type: string
    WASMComponentTypeInstance t_string;
    memset(&t_string, 0, sizeof(WASMComponentTypeInstance));
    t_string.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_string.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    t_string.alignment = 4;
    t_string.elem_size = 8;

    // 4. Create Param List with single string parameter
    uint32_t param_count = 1;
    WASMComponentParamListInstance *params = (WASMComponentParamListInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentParamListInstance) + param_count * sizeof(WASMComponentLabelValTypeInstance));
    ASSERT_NE(params, nullptr);
    params->count = param_count;
    params->params[0].type = &t_string;
    params->params[0].label = nullptr;

    // 5. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = false;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 6. Simulate Flat Values (string = ptr, len)
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = str_ptr;  // pointer to string
    values[1].type = CORE_TYPE_I32;
    values[1].val.i32 = str_len;  // length of string

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // 7. Execute lift_flat_values (flat_types.count <= max_flat)
    wit_value_t result = nullptr;
    bool success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // 8. Verify Result is an array containing single string
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_NE(result->value.string_value.chars, nullptr);
    ASSERT_STREQ(result->value.list_value.elems[0]->value.string_value.chars, "Hello, World!");
    ASSERT_EQ(result->value.list_value.elems[0]->value.string_value.size_bytes, str_len);

    // Verify iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // 9. Cleanup
    free_wit_value(result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLiftFlatValuesTest, TestLiftFlatValuesMultipleParamsSameType) {
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

    // Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = false;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // Simulate Flat Values (u32=100, u32=200)
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = 100;
    values[1].type = CORE_TYPE_I32;
    values[1].val.i32 = 200;

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // Execute lift_flat_values
    wit_value_t result = nullptr;
    bool success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // Verify Result is an array containing (u32, u32)
    // First element: u32 = 100
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.list_value.elems[0]->value.u32_value, 100);

    // Second element: u32 = 200
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.list_value.elems[1]->value.u32_value, 200);

    // Verify iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // Cleanup
    free_wit_value(result);
    wasm_runtime_free(params);
}

TEST_F(ComponentInstantiationLiftFlatValuesTest, TestLiftFlatValuesHeterogeneousParams) {
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

    // Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = false;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // Simulate Flat Values (u32=100, u64=9876543210, s32=-12345)
    CoreValue values[3];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = 100;
    values[1].type = CORE_TYPE_I64;
    values[1].val.i64 = 9876543210ULL;
    values[2].type = CORE_TYPE_I32;
    values[2].val.i32 = (uint32_t)-12345;  // s32 is passed as i32

    CoreValueIter vi;
    vi_init(&vi, values, 3);

    // Execute lift_flat_values
    wit_value_t result = nullptr;
    bool success = lift_flat_values(&cx, MAX_FLAT_PARAMS, &vi, params, nullptr, &result);

    ASSERT_TRUE(success) << "lift_flat_values failed for heterogeneous params";
    ASSERT_NE(result, nullptr) << "Result is NULL";

    // Verify Result is an array containing (u32, u64, s32)
    // First element: u32 = 100
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.list_value.elems[0]->value.u32_value, 100);

    // Second element: u64 = 9876543210
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(result->value.list_value.elems[1]->value.u64_value, 9876543210ULL);

    // Second element: s32 = -12345
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.list_value.elems[2]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.list_value.elems[2]->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(result->value.list_value.elems[2]->value.s32_value, -12345);

    // Verify iterator consumed all values
    ASSERT_TRUE(vi_done(&vi));

    // Cleanup
    free_wit_value(result);
    wasm_runtime_free(params);
}

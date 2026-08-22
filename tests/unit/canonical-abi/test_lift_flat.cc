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

class ComponentInstantiationLiftFlatTest : public testing::Test {
  public:
    ComponentInstantiationLiftFlatTest() {}
    ~ComponentInstantiationLiftFlatTest() {}
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
        loaded_wasm_file = "var_in_mem.wasm";
        memory_layout_file = "var_in_mem_layout.txt";
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlat) {
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

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatU8) {
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

    WASMMemoryInstance *mem = memories[0];
    ASSERT_NE(mem, nullptr);

    // 1. Setup the type for u8
    WASMComponentTypeInstance u8_type;
    memset(&u8_type, 0, sizeof(WASMComponentTypeInstance));
    u8_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    u8_type.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    u8_type.alignment = compute_alignment(&u8_type);
    u8_type.elem_size = compute_elem_size(&u8_type);

    // 2. Setup LiftLowerContext
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

    // 3. Create CoreValue array simulating register values
    // For u8, we pass a single i32 value
    CoreValue values[1];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = 42;  // The u8 value we want to lift

    // 4. Initialize the iterator
    CoreValueIter vi;
    vi_init(&vi, values, 1);

    // 5. Call lift_flat
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &u8_type, &result);

    // 6. Verify
    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(result->value.u8_value, 42);

    // Verify iterator is exhausted
    ASSERT_TRUE(vi_done(&vi));

    // Clean up
    free_wit_value(result);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatString) {
    helper->reset_component();
    bool ret = helper->read_wasm_file(loaded_wasm_file.c_str());
    helper->load_memory_offsets(memory_layout_file.c_str());

    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw != NULL);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    auto memories = helper->get_memories();
    ASSERT_GT(memories.size(), 0);

    WASMMemoryInstance *mem = memories[0];
    ASSERT_NE(mem, nullptr);

    // 1. Setup the type for string
    WASMComponentTypeInstance string_type;
    memset(&string_type, 0, sizeof(WASMComponentTypeInstance));
    string_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    string_type.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    string_type.alignment = compute_alignment(&string_type);
    string_type.elem_size = compute_elem_size(&string_type);

    // 2. Setup LiftLowerContext
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

    // 3. Read the string descriptor from memory to get the actual pointer
    // In loaded.wasm, the string descriptor is at offset 4:
    //   - Offset 4-7: pointer to actual "Hello" bytes
    //   - Offset 8-11: length (5)
    // We need to read these values to know where the string actually is
    uint8_t *mem_data = mem->memory_data;
    ASSERT_NE(mem_data, nullptr);

    uint32_t str_offset = helper->get_memory_offsets("STR_DESC");
    uint32_t string_ptr = *(uint32_t*)(mem_data + str_offset);   // Read pointer from offset 4
    uint32_t string_len = *(uint32_t*)(mem_data + str_offset + 4);   // Read length from offset 8

    // 4. Create CoreValue array simulating register values
    // For string, we pass two i32 values: (pointer, length)
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = string_ptr;  // Actual pointer to string bytes
    values[1].type = CORE_TYPE_I32;
    values[1].val.i32 = string_len;  // Length of string

    // 5. Initialize the iterator
    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // 6. Call lift_flat
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &string_type, &result);

    // 7. Verify
    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_NE(result->value.string_value.chars, nullptr);
    ASSERT_STREQ(result->value.string_value.chars, "Hello from Rust!");
    ASSERT_EQ(result->value.string_value.size_bytes, 16);

    // Verify iterator is exhausted
    ASSERT_TRUE(vi_done(&vi));

    // Clean up
    free_wit_value(result);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatTupleU32U32) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: u32 and Tuple (u32, u32)
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    // (Alignment/Size are not critical for lift_flat, but good to have set)
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // Construct Tuple Structure
    WASMComponentTupleInstance *tuple_inst = (WASMComponentTupleInstance *)wasm_runtime_malloc(sizeof(WASMComponentTupleInstance));
    ASSERT_NE(tuple_inst, nullptr);
    tuple_inst->count = 2;
    tuple_inst->element_types = (WASMComponentTypeInstance **)wasm_runtime_malloc(2 * sizeof(WASMComponentTypeInstance*));
    tuple_inst->element_types[0] = &t_u32;
    tuple_inst->element_types[1] = &t_u32;

    WASMComponentTypeInstance t_tuple;
    memset(&t_tuple, 0, sizeof(WASMComponentTypeInstance));
    t_tuple.type = COMPONENT_VAL_TYPE_TUPLE;
    t_tuple.type_specific.tuple = tuple_inst;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Simulate "Flat" Values (Register Values)
    // ---------------------------------------------------------
    // Tuple (100, 200) -> Flattened to [i32(100), i32(200)]
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = 100;
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = 200;

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 5. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_tuple, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Result (Despecialization into Tuple)
    // ---------------------------------------------------------
    // Tuple is despecialized into a TUPLE
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_TUPLE);
    
    // Verify Field 0 -> 100
    ASSERT_EQ(result->value.tuple_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.tuple_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.tuple_value.elems[0]->value.u32_value, 100);

    // Verify Field 1 -> 200
    ASSERT_EQ(result->value.tuple_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.tuple_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.tuple_value.elems[1]->value.u32_value, 200);

    // Verify that the iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    // Clean up wit_value structures (free_wit_value performs deep free)
    free_wit_value(result);
    
    // Clean up internal structures manually created for the test
    wasm_runtime_free(tuple_inst->element_types);
    wasm_runtime_free(tuple_inst);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatEnumGreen) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Construct Enum Schema: Color { Red, Green, Blue }
    // ---------------------------------------------------------
    uint32_t count = 3;
    WASMComponentEnumType *enum_inst = (WASMComponentEnumType *)wasm_runtime_malloc(sizeof(WASMComponentEnumType));
    ASSERT_NE(enum_inst, nullptr);
    enum_inst->count = count;
    enum_inst->labels = (WASMComponentCoreName *)wasm_runtime_malloc(count * sizeof(WASMComponentCoreName));

    const char* labels[] = {"Red", "Green", "Blue"};
    for(uint32_t i = 0; i < count; i++) {
        enum_inst->labels[i].name = strdup(labels[i]);
        enum_inst->labels[i].name_len = (uint32_t)strlen(labels[i]);
    }

    WASMComponentTypeInstance t_enum;
    memset(&t_enum, 0, sizeof(WASMComponentTypeInstance));
    t_enum.type = COMPONENT_VAL_TYPE_ENUM;
    t_enum.type_specific.enum_type = enum_inst;
    // Alignment/Size are not directly used in lift_flat, but good to be consistent
    t_enum.alignment = 1; t_enum.elem_size = 1; 

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Simulate Flat Value (CoreValue)
    // ---------------------------------------------------------
    // We want to lift "Green", which is at index 1.
    // Canonical ABI: Enums are represented flat as an i32 (discriminant).
    CoreValue values[1];
    values[0].type = CORE_TYPE_I32;
    values[0].val.i32 = 1; // Index 1 = Green

    CoreValueIter vi;
    vi_init(&vi, values, 1);

    // ---------------------------------------------------------
    // 5. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    // This will internally call lift_flat_enum -> convert -> lift_flat_variant
    bool success = lift_flat(&cx, &vi, &t_enum, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_ENUM);

    // Verify Discriminant
    ASSERT_EQ(result->value.enum_value.value, 1);

    // Verify that the iterator consumed the value
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);

    // Cleanup of the manually created Enum type structure
    for(uint32_t i = 0; i < count; i++) {
        free(enum_inst->labels[i].name);
    }
    wasm_runtime_free(enum_inst->labels);
    wasm_runtime_free(enum_inst);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatOptionSomeU32) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: u32 and option<u32>
    // ---------------------------------------------------------
    // Inner type: u32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // Option definition
    WASMComponentOptionInstance option_inst;
    option_inst.element_type = &t_u32;

    WASMComponentTypeInstance t_option;
    memset(&t_option, 0, sizeof(WASMComponentTypeInstance));
    t_option.type = COMPONENT_VAL_TYPE_OPTION;
    t_option.type_specific.option = &option_inst;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
    LiftOptions lift_opts = { .string_encoding = ENCODING_UTF_8, .memory = mem };
    LiftLowerOptions ll_opts = { .lift_opts = &lift_opts, .realloc_func = nullptr };
    CanonicalOptions can_opts = { .lift_lower_opts = &ll_opts, .post_return_func = nullptr, .async = false, .callback_func = nullptr};
    LiftLowerContext cx = { .canonical_opts = &can_opts, .inst = helper->component_inst, .borrow_scope_type = BORROW_SCOPE_TASK, .borrow_scope = nullptr };

    // ---------------------------------------------------------
    // 4. Simulate Flat Values (Stack/Register State)
    // ---------------------------------------------------------
    // We want to lift: some(12345)
    // Flat representation: [ i32(discriminant), i32(payload) ]
    // Discriminant 1 = "some" (0 would be "none")
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = 1;     // Discriminant: 1 (some)
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = 12345; // Payload: 12345

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 5. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_option, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_OPTION);

    // Verify Payload is present and correct
    ASSERT_NE(result->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(result->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.option_value.optional_elem->value.u32_value, 12345);

    // Verify iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatResultErrorU8) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: result<u32, u8>
    // ---------------------------------------------------------
    // OK type: u32
    WASMComponentTypeInstance t_u32;
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // Error type: u8
    WASMComponentTypeInstance t_u8;
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // Result definition
    WASMComponentResultInstance res_inst;
    res_inst.result_type = &t_u32;
    res_inst.error_type = &t_u8;

    WASMComponentTypeInstance t_result;
    memset(&t_result, 0, sizeof(WASMComponentTypeInstance));
    t_result.type = COMPONENT_VAL_TYPE_RESULT;
    t_result.type_specific.result = &res_inst;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
    LiftOptions lift_opts = { .string_encoding = ENCODING_UTF_8, .memory = mem };
    LiftLowerOptions ll_opts = { .lift_opts = &lift_opts, .realloc_func = nullptr };
    CanonicalOptions can_opts = { .lift_lower_opts = &ll_opts, .post_return_func = nullptr, .async = false, .callback_func = nullptr};
    LiftLowerContext cx = { .canonical_opts = &can_opts, .inst = helper->component_inst, .borrow_scope_type = BORROW_SCOPE_TASK, .borrow_scope = nullptr };

    // ---------------------------------------------------------
    // 4. Simulate Flat Values (Stack/Register State)
    // ---------------------------------------------------------
    // We want to lift: error(200) -> 200 is the u8 error code
    // Flat representation: [ i32(discriminant), i32(payload) ]
    // Discriminant 1 = "error" (0 would be "ok")
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = 1;   // Discriminant: 1 (error)
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = 200; // Payload: 200

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 5. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_result, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);

    // Verify it is the error case
    ASSERT_TRUE(result->value.result_value.is_err);

    // Verify Payload is correct
    ASSERT_NE(result->value.result_value.result.err, nullptr);
    ASSERT_EQ(result->value.result_value.result.err->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(result->value.result_value.result.err->value.u8_value, 200);

    // Verify iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatResultOkU32) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: result<u32, u8>
    // ---------------------------------------------------------
    // OK type: u32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // Error type: u8
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // Result definition
    WASMComponentResultInstance res_inst;
    res_inst.result_type = &t_u32;
    res_inst.error_type = &t_u8;

    WASMComponentTypeInstance t_result;
    memset(&t_result, 0, sizeof(WASMComponentTypeInstance));
    t_result.type = COMPONENT_VAL_TYPE_RESULT;
    t_result.type_specific.result = &res_inst;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Simulate Flat Values (Stack/Register State)
    // ---------------------------------------------------------
    // We want to lift: ok(123456)
    // Flat representation: [ i32(discriminant), i32(payload) ]
    // Discriminant 0 = "ok" (1 would be "error")
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = 0;      // Discriminant: 0 (ok)
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = 123456; // Payload: 123456

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 5. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_result, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);

    // Verify it is the ok case
    ASSERT_FALSE(result->value.result_value.is_err);

    // Verify Payload is correct (u32)
    ASSERT_NE(result->value.result_value.result.ok, nullptr);
    ASSERT_EQ(result->value.result_value.result.ok->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.result_value.result.ok->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(result->value.result_value.result.ok->value.u32_value, 123456);

    // Verify iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatTupleFromMemoryPair) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    
    // Load the memory offsets parsed from the Rust output
    helper->load_memory_offsets(memory_layout_file.c_str());

    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: u8, u16 and Tuple (u8, u16)
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    WASMComponentTypeInstance t_u16;
    memset(&t_u16, 0, sizeof(WASMComponentTypeInstance));
    t_u16.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u16.type_specific.primval = WASM_COMP_PRIMVAL_U16;
    t_u16.alignment = 2; t_u16.elem_size = 2;

    // Construct Tuple Structure
    WASMComponentTupleInstance *tuple_inst = (WASMComponentTupleInstance *)wasm_runtime_malloc(sizeof(WASMComponentTupleInstance));
    ASSERT_NE(tuple_inst, nullptr);
    tuple_inst->count = 2;
    tuple_inst->element_types = (WASMComponentTypeInstance **)wasm_runtime_malloc(2 * sizeof(WASMComponentTypeInstance*));
    tuple_inst->element_types[0] = &t_u8;
    tuple_inst->element_types[1] = &t_u16;

    WASMComponentTypeInstance t_tuple;
    memset(&t_tuple, 0, sizeof(WASMComponentTypeInstance));
    t_tuple.type = COMPONENT_VAL_TYPE_TUPLE;
    t_tuple.type_specific.tuple = tuple_inst;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Retrieve Data from Linear Memory
    // ---------------------------------------------------------
    // We get the address of the static 'PAIR' variable from the layout file.
    // Rust definition: static PAIR: (u8, u16) = (100, 10000);
    uint32_t pair_offset = helper->get_memory_offsets("PAIR_TUPLE_DESC");
    uint8_t *mem_data = mem->memory_data;

    // Read the u8 (offset 0)
    uint8_t val_u8 = *(uint8_t*)(mem_data + pair_offset);
    
    // Read the u16. 
    // IMPORTANT: Alignment padding rules apply here.
    // Address of u8 is 'pair_offset'. Size is 1. Next byte is 'pair_offset + 1'.
    // However, u16 requires 2-byte alignment. 
    // So 'pair_offset + 1' is skipped (padding), and u16 starts at 'pair_offset + 2'.
    uint16_t val_u16 = *(uint16_t*)(mem_data + pair_offset + 2);

    // Verify we read what we expect from the Rust static initialization
    ASSERT_EQ(val_u8, 100);
    ASSERT_EQ(val_u16, 10000);

    // ---------------------------------------------------------
    // 5. Simulate "Flat" Values (Register Values)
    // ---------------------------------------------------------
    // Even though the data resides in memory, for a primitive tuple,
    // lift_flat expects the values to be on the stack/registers.
    // We populate the registers with the data we just read from memory.
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = val_u8;
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = val_u16;

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 6. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_tuple, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 7. Verify Result (Despecialization into Tuple)
    // ---------------------------------------------------------
    // Tuple is despecialized into a TUPLE
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_TUPLE);
    
    // Verify Field 0 -> "0": 100
    ASSERT_EQ(result->value.tuple_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.tuple_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(result->value.tuple_value.elems[0]->value.u8_value, 100);
    
    // Verify Field 1 -> "1": 10000
    ASSERT_EQ(result->value.tuple_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.tuple_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_U16);
    ASSERT_EQ(result->value.tuple_value.elems[1]->value.u16_value, 10000);

    // Verify that the iterator consumed both values
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 8. Cleanup
    // ---------------------------------------------------------
    // Clean up wit_value structures (free_wit_value performs deep free)
    free_wit_value(result);
    
    // Clean up internal structures manually created for the test
    wasm_runtime_free(tuple_inst->element_types);
    wasm_runtime_free(tuple_inst);
} 

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatVariantFromMemory) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    
    // Load offsets from the layout file generated by the Rust component
    helper->load_memory_offsets(memory_layout_file.c_str());

    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: f32 and Variant Shape { circle(f32), square(f32) }
    // ---------------------------------------------------------
    
    // 2a. Define f32
    WASMComponentTypeInstance t_f32;
    memset(&t_f32, 0, sizeof(WASMComponentTypeInstance));
    t_f32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_f32.type_specific.primval = WASM_COMP_PRIMVAL_F32;
    t_f32.alignment = 4; t_f32.elem_size = 4;

    // 2b. Construct Variant Structure
    // Case 0: "circle" (f32)
    // Case 1: "square" (f32)
    uint32_t count = 2;
    WASMComponentVariantInstance *var_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(
        offsetof(WASMComponentVariantInstance, cases) + (count * sizeof(WASMComponentCaseValInstance))
    );
    ASSERT_NE(var_inst, nullptr);
    var_inst->count = count;

    // Configure Case 0: "circle"
    char label0[] = "circle";
    var_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[0].label->name = label0;
    var_inst->cases[0].label->name_len = (uint32_t)strlen(label0);
    var_inst->cases[0].value_type = &t_f32; 

    // Configure Case 1: "square"
    char label1[] = "square";
    var_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[1].label->name = label1;
    var_inst->cases[1].label->name_len = (uint32_t)strlen(label1);
    var_inst->cases[1].value_type = &t_f32; 

    WASMComponentTypeInstance t_variant;
    memset(&t_variant, 0, sizeof(WASMComponentTypeInstance));
    t_variant.type = COMPONENT_VAL_TYPE_VARIANT;
    t_variant.type_specific.variant = var_inst;
    t_variant.alignment = 4; // Max alignment of fields

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Retrieve Data from Linear Memory
    // ---------------------------------------------------------
    // Retrieve the address of 'static VARIANT' from Rust
    uint32_t var_offset = helper->get_memory_offsets("VARIANT_DESC");
    uint8_t *mem_data = mem->memory_data;

    // Step A: Read Discriminant (1 byte)
    // In Rust/WIT, the variant tag is usually a u8 (if cases < 256).
    uint8_t discriminant = *(uint8_t*)(mem_data + var_offset);

    // Step B: Read Payload (f32)
    // ALIGNMENT RULE: The payload (f32) must be aligned to 4 bytes.
    // Address = var_offset + 1 (tag size).
    // Next 4-byte boundary = var_offset + 4.
    // Therefore, bytes at offsets +1, +2, +3 are padding.
    float payload = *(float*)(mem_data + var_offset + 4);

    // Sanity check on raw memory values before lifting
    ASSERT_EQ(discriminant, 1); // 1 = square
    ASSERT_FLOAT_EQ(payload, 32.5f);

    // ---------------------------------------------------------
    // 5. Simulate "Flat" Values (Register Values)
    // ---------------------------------------------------------
    // lift_flat expects values in a CoreValueIter.
    // [0]: Discriminant (as i32)
    // [1]: Payload (as f32)
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = (int32_t)discriminant;
    values[1].type = CORE_TYPE_F32; values[1].val.f32 = payload;

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 6. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    bool success = lift_flat(&cx, &vi, &t_variant, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 7. Verify Result
    // ---------------------------------------------------------
    // Verify general type
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_VARIANT);
    
    // Verify Discriminant Name ("square")
    ASSERT_STREQ(result->value.variant_value.discriminator, "square");
    
    // Verify Payload Value (32.5)
    ASSERT_NE(result->value.variant_value.value, nullptr);
    ASSERT_EQ(result->value.variant_value.value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.variant_value.value->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(result->value.variant_value.value->value.f32_value, 32.5f);

    // Verify iterator exhausted
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 8. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);

    // Clean up manually created variant metadata
    for(uint32_t i = 0; i < count; i++) {
        wasm_runtime_free(var_inst->cases[i].label);
    }
    wasm_runtime_free(var_inst);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatEnumFromMemory) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    
    // Load offsets (Assumes "APP_STATUS_DESC" exists in the layout file)
    helper->load_memory_offsets(memory_layout_file.c_str());

    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Enum Type: Status { Idle, Running, Stopped }
    // ---------------------------------------------------------
    uint32_t count = 3;
    WASMComponentEnumType *enum_inst = (WASMComponentEnumType *)wasm_runtime_malloc(sizeof(WASMComponentEnumType));
    ASSERT_NE(enum_inst, nullptr);
    enum_inst->count = count;
    enum_inst->labels = (WASMComponentCoreName *)wasm_runtime_malloc(count * sizeof(WASMComponentCoreName));

    const char* labels[] = {"idle", "running", "stopped"};
    for(uint32_t i = 0; i < count; i++) {
        enum_inst->labels[i].name = strdup(labels[i]);
        enum_inst->labels[i].name_len = (uint32_t)strlen(labels[i]);
    }

    WASMComponentTypeInstance t_enum;
    memset(&t_enum, 0, sizeof(WASMComponentTypeInstance));
    t_enum.type = COMPONENT_VAL_TYPE_ENUM;
    t_enum.type_specific.enum_type = enum_inst;
    // Enums are usually 1-byte aligned if count <= 256
    t_enum.alignment = 1; t_enum.elem_size = 1;

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Retrieve Data from Linear Memory
    // ---------------------------------------------------------
    // We get the address of the static 'STATUS_DESC' variable.
    uint32_t enum_offset = helper->get_memory_offsets("STATUS_DESC");
    uint8_t *mem_data = mem->memory_data;

    // Read the discriminant byte
    uint8_t discriminant_u8 = *(uint8_t*)(mem_data + enum_offset);

    // Verify raw memory assumption (We expect 'Running', which is index 1)
    // If this fails, either the Rust code uses a different value or layout is wrong.
    ASSERT_EQ(discriminant_u8, 1);

    // ---------------------------------------------------------
    // 5. Simulate "Flat" Values (Register Values)
    // ---------------------------------------------------------
    // ABI Rule: Even if stored as u8 in memory, enums are passed as i32 on the stack (flat).
    CoreValue values[1];
    values[0].type = CORE_TYPE_I32; 
    values[0].val.i32 = (int32_t)discriminant_u8; 

    CoreValueIter vi;
    vi_init(&vi, values, 1);

    // ---------------------------------------------------------
    // 6. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    // Internal flow: lift_flat -> lift_flat_enum -> convert_enum_to_variant -> lift_flat_variant
    bool success = lift_flat(&cx, &vi, &t_enum, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 7. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_ENUM);

    // Check Discriminant: Index 1 = "running"
    ASSERT_EQ(result->value.enum_value.value, 1);

    // Verify iterator exhausted
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 8. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);

    // Clean up manual enum definition
    wasm_runtime_free(enum_inst->labels);
    wasm_runtime_free(enum_inst);
}

TEST_F(ComponentInstantiationLiftFlatTest, TestLiftFlatResultFromMemory) {
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    
    // Load offsets (Assumes "RES_OK_DESC" exists in layout file)
    helper->load_memory_offsets(memory_layout_file.c_str());

    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Define Types: result<u32, u8>
    // ---------------------------------------------------------
    // OK Payload: u32
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // Error Payload: u8
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // Result Metadata
    WASMComponentResultInstance res_inst;
    res_inst.result_type = &t_u32;
    res_inst.error_type = &t_u8;

    WASMComponentTypeInstance t_result;
    memset(&t_result, 0, sizeof(WASMComponentTypeInstance));
    t_result.type = COMPONENT_VAL_TYPE_RESULT;
    t_result.type_specific.result = &res_inst;
    // Result Alignment = max(align(tag), align(payloads)) = max(1, 4, 1) = 4
    t_result.alignment = 4; 

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 4. Retrieve Data from Linear Memory
    // ---------------------------------------------------------
    // Retrieve address of 'static RES_OK' (Ok(1337))
    uint32_t res_offset = helper->get_memory_offsets("RES_OK_DESC");
    uint8_t *mem_data = mem->memory_data;

    // Step A: Read Discriminant (Offset 0)
    uint8_t discriminant = *(uint8_t*)(mem_data + res_offset);

    // Step B: Read Payload
    // ALIGNMENT CALCULATION:
    // Base = res_offset.
    // Tag size = 1.
    // Current pos = res_offset + 1.
    // Payload align = 4 (for u32).
    // Next aligned address = ((res_offset + 1) + 3) & ~3 = res_offset + 4.
    uint32_t payload = *(uint32_t*)(mem_data + res_offset + 4);

    // Verify raw memory assumptions (Ok = 0, Payload = 1337)
    ASSERT_EQ(discriminant, 0); 
    ASSERT_EQ(payload, 1337);

    // ---------------------------------------------------------
    // 5. Simulate "Flat" Values
    // ---------------------------------------------------------
    // Result flattens to [i32(discriminant), i32(payload)]
    CoreValue values[2];
    values[0].type = CORE_TYPE_I32; values[0].val.i32 = (int32_t)discriminant;
    values[1].type = CORE_TYPE_I32; values[1].val.i32 = payload;

    CoreValueIter vi;
    vi_init(&vi, values, 2);

    // ---------------------------------------------------------
    // 6. Execute Lift Flat
    // ---------------------------------------------------------
    wit_value_t result = nullptr;
    // Internal flow: lift_flat_result -> convert_result_to_variant -> lift_flat_variant
    bool success = lift_flat(&cx, &vi, &t_result, &result);

    ASSERT_TRUE(success);
    ASSERT_NE(result, nullptr);

    // ---------------------------------------------------------
    // 7. Verify Result
    // ---------------------------------------------------------
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);

    // Check it is the ok case
    ASSERT_FALSE(result->value.result_value.is_err);

    // Check Payload
    ASSERT_NE(result->value.result_value.result.ok, nullptr);
    ASSERT_EQ(result->value.result_value.result.ok->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result->value.result_value.result.ok->value.u32_value, 1337);

    // Verify iterator exhausted
    ASSERT_TRUE(vi_done(&vi));

    // ---------------------------------------------------------
    // 8. Cleanup
    // ---------------------------------------------------------
    free_wit_value(result);
}

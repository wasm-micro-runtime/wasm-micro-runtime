/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include "../component/helpers.h"
#include "wasm_memory.h"
#include "wasm_component_canonical.h"
#include <vector>
#include <iostream>

class ComponentInstantiationTest : public testing::Test
{
  public:
    ComponentInstantiationTest() {}
    ~ComponentInstantiationTest() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;
    bool exception = false;
    std::unique_ptr<ComponentHelper> helper;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    std::string loaded_wasm_file;

    virtual void SetUp() {
        helper = std::make_unique<ComponentHelper>();
        helper->do_setup();
        loaded_wasm_file = "loaded.wasm";
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

TEST_F(ComponentInstantiationTest, TestLoadMemoryInstantiation)
{
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

TEST_F(ComponentInstantiationTest, TestLoadStringFromMemory)
{
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

    // Set up the type for a string
    WASMComponentTypeInstance string_type;
    memset(&string_type, 0, sizeof(WASMComponentTypeInstance));
    string_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    string_type.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    string_type.alignment = compute_alignment(&string_type);
    string_type.elem_size = compute_elem_size(&string_type);

    // Set up LiftLowerContext for loading
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // Call the load function at offset 0
    uint8_t string_addr = 4;
    bool load_result = load(&cx, string_addr, &string_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_STRING);

    // Verify the string content
    const char *expected_string = "Hello";
    ASSERT_NE(loaded_value->value.string_value.chars, nullptr);
    ASSERT_STREQ(loaded_value->value.string_value.chars, expected_string);
    ASSERT_EQ(loaded_value->value.string_value.size_bytes, strlen(expected_string));

    // Clean up
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadU8FromMemory)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file(loaded_wasm_file.c_str());
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw!= NULL);

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
    u8_type.type_specific.primval = WASM_COMP_PRIMVAL_U8; // Specify that we are reading a U8
    u8_type.alignment = compute_alignment(&u8_type);
    u8_type.elem_size = compute_elem_size(&u8_type);

    // 2. Setup LiftLowerContext for loading
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; // Irrelevant for u8, but required by the structure
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call the load function at offset 0
    // In your.wat file, the value 42 is at offset 0
    uint8_t u8_addr = 0; 
    bool load_result = load(&cx, u8_addr, &u8_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_U8);

    // 4. Verify the u8 content
    // Verify if the value is 42 (0x2A from the.wat)
    // Note: Accessing the.u8_value member of the value union
    ASSERT_EQ(loaded_value->value.u8_value, 42);

    // Clean up
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadS8FromMemory)
{
    helper->reset_component();
    bool ret = helper->read_wasm_file(loaded_wasm_file.c_str());
    ASSERT_TRUE(ret);
    ASSERT_TRUE(helper->component_raw!= NULL);

    ret = helper->load_component();
    ASSERT_TRUE(ret);
    ret = helper->instantiate_component();
    ASSERT_TRUE(ret);

    auto memories = helper->get_memories();
    ASSERT_GT(memories.size(), 0);

    WASMMemoryInstance *mem = memories[0];
    ASSERT_NE(mem, nullptr);

    // 1. Setup the type for s8
    WASMComponentTypeInstance s8_type;
    memset(&s8_type, 0, sizeof(WASMComponentTypeInstance));
    s8_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    s8_type.type_specific.primval = WASM_COMP_PRIMVAL_S8; // Specify that we are reading an S8
    s8_type.alignment = compute_alignment(&s8_type);
    s8_type.elem_size = compute_elem_size(&s8_type);

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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call the load function at offset 12
    // The s8 value (-15) is located at offset 12 in the.wat file
    uint8_t s8_addr = 12; 
    bool load_result = load(&cx, s8_addr, &s8_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_S8);

    // 4. Verify the s8 content
    // We expect -15. The cast ensures the comparison is done correctly as signed integers.
    // Assuming the union member is named 's8_value' consistent with 'u8_value'
    ASSERT_EQ(loaded_value->value.s8_value, -15);

    // Clean up
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadU16FromMemory)
{
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

    // 1. Setup the type for u16
    WASMComponentTypeInstance u16_type;
    memset(&u16_type, 0, sizeof(WASMComponentTypeInstance));
    u16_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    u16_type.type_specific.primval = WASM_COMP_PRIMVAL_U16; // Specify that we are reading a U16
    u16_type.alignment = compute_alignment(&u16_type);
    u16_type.elem_size = compute_elem_size(&u16_type);

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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call the load function at offset 16
    // In the .wat file, the u16 value (12345) is at offset 16
    uint32_t u16_addr = 16; 
    bool load_result = load(&cx, u16_addr, &u16_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_U16);

    // 4. Verify the u16 content
    // We expect 12345
    ASSERT_EQ(loaded_value->value.u16_value, 12345);

    // Clean up
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadS16FromMemory)
{
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

    // 1. Setup the type for s16
    WASMComponentTypeInstance s16_type;
    memset(&s16_type, 0, sizeof(WASMComponentTypeInstance));
    s16_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    s16_type.type_specific.primval = WASM_COMP_PRIMVAL_S16; // Specify that we are reading an S16
    s16_type.alignment = compute_alignment(&s16_type);
    s16_type.elem_size = compute_elem_size(&s16_type);

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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call the load function at offset 18
    // In the .wat file, the s16 value (-555) starts at offset 18
    uint32_t s16_addr = 20; 
    bool load_result = load(&cx, s16_addr, &s16_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_S16);

    // 4. Verify the s16 content
    // We expect -555
    ASSERT_EQ(loaded_value->value.s16_value, -555);

    // Clean up
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadU32FromMemory)
{
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

    // 1. Setup type u32
    WASMComponentTypeInstance u32_type;
    memset(&u32_type, 0, sizeof(WASMComponentTypeInstance));
    u32_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    u32_type.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    u32_type.alignment = compute_alignment(&u32_type);
    u32_type.elem_size = compute_elem_size(&u32_type);

    // 2. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; 
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call load at Offset 24
    uint32_t u32_addr = 24; 
    bool load_result = load(&cx, u32_addr, &u32_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_U32);

    // 4. Verify value 987654321
    ASSERT_EQ(loaded_value->value.u32_value, 987654321);

    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadS32FromMemory)
{
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

    // 1. Setup type s32
    WASMComponentTypeInstance s32_type;
    memset(&s32_type, 0, sizeof(WASMComponentTypeInstance));
    s32_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    s32_type.type_specific.primval = WASM_COMP_PRIMVAL_S32;
    s32_type.alignment = compute_alignment(&s32_type);
    s32_type.elem_size = compute_elem_size(&s32_type);

    // 2. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; 
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call load at Offset 28
    // Start at 24 (u32) + 4 bytes = 28
    uint32_t s32_addr = 28; 
    bool load_result = load(&cx, s32_addr, &s32_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_S32);

    // 4. Verify value -12345678
    ASSERT_EQ(loaded_value->value.s32_value, -12345678);

    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadU64FromMemory)
{
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

    // 1. Setup type u64
    WASMComponentTypeInstance u64_type;
    memset(&u64_type, 0, sizeof(WASMComponentTypeInstance));
    u64_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    u64_type.type_specific.primval = WASM_COMP_PRIMVAL_U64; 
    u64_type.alignment = compute_alignment(&u64_type);
    u64_type.elem_size = compute_elem_size(&u64_type);

    // 2. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; 
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call load at Offset 32
    // Previous s32 ended at 31. 32 is 8-byte aligned.
    uint32_t u64_addr = 32; 
    bool load_result = load(&cx, u64_addr, &u64_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_U64);

    // 4. Verify value
    ASSERT_EQ(loaded_value->value.u64_value, 1234567890123456789ULL);

    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadS64FromMemory)
{
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

    // 1. Setup type s64
    WASMComponentTypeInstance s64_type;
    memset(&s64_type, 0, sizeof(WASMComponentTypeInstance));
    s64_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    s64_type.type_specific.primval = WASM_COMP_PRIMVAL_S64;
    s64_type.alignment = compute_alignment(&s64_type);
    s64_type.elem_size = compute_elem_size(&s64_type);

    // 2. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; 
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call load at Offset 40
    uint32_t s64_addr = 40; 
    bool load_result = load(&cx, s64_addr, &s64_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_S64);

    // 4. Verify value
    ASSERT_EQ(loaded_value->value.s64_value, -100);

    free_wit_value(loaded_value);

    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadF32FromMemory)
{
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

    WASMComponentTypeInstance f32_type;
    memset(&f32_type, 0, sizeof(WASMComponentTypeInstance));
    f32_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    f32_type.type_specific.primval = WASM_COMP_PRIMVAL_F32; 
    f32_type.alignment = compute_alignment(&f32_type);
    f32_type.elem_size = compute_elem_size(&f32_type);

    // --- 3. Setup Context (Lift/Lower) ---
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;


    uint32_t f32_addr = 48; 
    bool load_result = load(&cx, f32_addr, &f32_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_F32);


    ASSERT_FLOAT_EQ(loaded_value->value.f32_value, 123.45f);

    // --- 6. Cleanup ---
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadF64FromMemory)
{
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

    // 1. Setup type f64
    WASMComponentTypeInstance f64_type;
    memset(&f64_type, 0, sizeof(WASMComponentTypeInstance));
    f64_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    f64_type.type_specific.primval = WASM_COMP_PRIMVAL_F64; 
    f64_type.alignment = compute_alignment(&f64_type);
    f64_type.elem_size = compute_elem_size(&f64_type);

    // 2. Setup Context
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8; 
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t loaded_value = nullptr;

    // 3. Call load at Offset 48
    uint32_t f64_addr = 56; 
    bool load_result = load(&cx, f64_addr, &f64_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_F64);

    // 4. Verify value: 1234.5678
    ASSERT_DOUBLE_EQ(loaded_value->value.f64_value, 1234.5678);

    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationTest, TestLoadRudimentaryChar)
{
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
    // 2. Define Char Type
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_char;
    memset(&t_char, 0, sizeof(WASMComponentTypeInstance));
    
    t_char.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_char.type_specific.primval = WASM_COMP_PRIMVAL_CHAR; 
    
    // CRITICAL: Even for ASCII 'A', alignment and size are 4 bytes.
    t_char.alignment = 4; 
    t_char.elem_size = 4;

    // ---------------------------------------------------------
    // 3. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 64
    // Value in memory: 0x00000041 (Decimal 65)
    bool res = load(&cx, 64, &t_char, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 4. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(out_val->prim_type, WASM_COMP_PRIMVAL_CHAR);
    
    // Verify against decimal value 65 ('A')
    ASSERT_EQ(out_val->value.char_value, 65);
    
    // Verify against character literal
    ASSERT_EQ(out_val->value.char_value, 'A');

    // ---------------------------------------------------------
    // 5. Cleanup
    // ---------------------------------------------------------
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadRecordFromOffset512)
{
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
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
    // ---------------------------------------------------------
    // 2. Define Primitive Types
    // ---------------------------------------------------------
    // Use {0} for zero-initialization instead of memset

    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8; 
    t_u8.alignment = 1; 
    t_u8.elem_size = 1;

    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32; 
    t_u32.alignment = 4; 
    t_u32.elem_size = 4;

    // ---------------------------------------------------------
    // 3. Construct Record Type (Input Schema)
    // ---------------------------------------------------------
    // Schema: { first: u8, second: u32, third: u8 }
    
    uint32_t field_count = 3;

    // // Calculate total size: Struct Header + Array of Fields
    uint32_t total_size = sizeof(WASMComponentRecordInstance) + 
                          (field_count * sizeof(WASMComponentLabelValTypeInstance));

    // Allocate memory using the runtime allocator
    WASMComponentRecordInstance *rec_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(total_size);
    ASSERT_NE(rec_inst, nullptr);
    
    // Set field count
    rec_inst->count = field_count;

    // --- Field 0: "first" ---
    char first_label[] = "first";
    rec_inst->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    rec_inst->fields[0].label->name = first_label;
    rec_inst->fields[0].label->name_len = 5;
    rec_inst->fields[0].type = &t_u8;

    // --- Field 1: "second" ---
    char second_label[] = "second";
    rec_inst->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    rec_inst->fields[1].label->name = second_label;
    rec_inst->fields[1].label->name_len = 6;
    rec_inst->fields[1].type = &t_u32;

    // --- Field 2: "third" ---
    char third_label[] = "third";
    rec_inst->fields[2].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    rec_inst->fields[2].label->name = third_label;
    rec_inst->fields[2].label->name_len = 5;
    rec_inst->fields[2].type = &t_u8;

    // 4. Wrap in Main Type Instance
    WASMComponentTypeInstance record_type;
    memset(&record_type, 0, sizeof(WASMComponentTypeInstance));
    record_type.type = COMPONENT_VAL_TYPE_RECORD;
    record_type.type_specific.record = rec_inst; // Point to the allocated structure
    
    // Alignment logic: Max(u8, u32) = 4
    record_type.alignment = 4;
    // Size logic: 1(u8) + 3(pad) + 4(u32) + 1(u8) + 3(tail pad) = 12
    record_type.elem_size = 12;

    // ---------------------------------------------------------
    // 5. Execute Load
    // ---------------------------------------------------------

    // Set up LiftLowerContext for loading
    LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 512
    // Memory layout expected:
    // 512: u8 (10)
    // 513-515: Padding (skipped)
    // 516: u32 (2024)
    // 520: u8 (255)
    
    uint32_t record_addr = 512;
    bool res = load(&cx, record_addr, &record_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Results (Output)
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_RECORD);
    
    // Cast to the Output structure (ComponentWITRecordField)
    ComponentWITRecordField *result_fields = (ComponentWITRecordField *)out_val->value.record_value.fields;
 
    // Check Field 0
    ASSERT_STREQ(result_fields[0].key, "first");
    ASSERT_EQ(result_fields[0].value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result_fields[0].value->value.u8_value, 10);

    // Check Field 1
    ASSERT_STREQ(result_fields[1].key, "second");
    ASSERT_EQ(result_fields[1].value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result_fields[1].value->value.u32_value, 2024);

    // Check Field 2
    ASSERT_STREQ(result_fields[2].key, "third");
    ASSERT_EQ(result_fields[2].value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(result_fields[2].value->value.u8_value, 255);

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    // Free the manually allocated labels
    wasm_runtime_free(rec_inst->fields[0].label);
    wasm_runtime_free(rec_inst->fields[1].label);
    wasm_runtime_free(rec_inst->fields[2].label);
    // Free the record instance
    wasm_runtime_free(rec_inst);
    
    // Free the result value
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadNestedCircleWithThreePoints)
{
    // 1. Init
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 2. Define Primitive Types
    WASMComponentTypeInstance t_u16;
    t_u16.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u16.type_specific.primval = WASM_COMP_PRIMVAL_U16;
    t_u16.alignment = 2; t_u16.elem_size = 2;

    WASMComponentTypeInstance t_u32;
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // 3. Construct Schema: Point { x, y, z }
    uint32_t p_fields = 3;
    WASMComponentRecordInstance *p_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentRecordInstance) + (p_fields * sizeof(WASMComponentLabelValTypeInstance)));
    p_inst->count = p_fields;
    
    // x
    p_inst->fields[0].label = (WASMComponentCoreName*)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    p_inst->fields[0].label->name = (char*)"x"; p_inst->fields[0].label->name_len = 1;
    p_inst->fields[0].type = &t_u16;
    // y
    p_inst->fields[1].label = (WASMComponentCoreName*)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    p_inst->fields[1].label->name = (char*)"y"; p_inst->fields[1].label->name_len = 1;
    p_inst->fields[1].type = &t_u16;
    // z
    p_inst->fields[2].label = (WASMComponentCoreName*)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    p_inst->fields[2].label->name = (char*)"z"; p_inst->fields[2].label->name_len = 1;
    p_inst->fields[2].type = &t_u16;

    WASMComponentTypeInstance record_point;
    record_point.type = COMPONENT_VAL_TYPE_RECORD;
    record_point.type_specific.record = p_inst;
    record_point.alignment = 2; record_point.elem_size = 6;

    // 4. Construct Schema: Circle { center: Point, radius: u32 }
    uint32_t c_fields = 2;
    WASMComponentRecordInstance *c_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(
        sizeof(WASMComponentRecordInstance) + (c_fields * sizeof(WASMComponentLabelValTypeInstance)));
    c_inst->count = c_fields;

    c_inst->fields[0].label = (WASMComponentCoreName*)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    c_inst->fields[0].label->name = (char*)"center"; c_inst->fields[0].label->name_len = 6;
    c_inst->fields[0].type = &record_point;

    c_inst->fields[1].label = (WASMComponentCoreName*)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    c_inst->fields[1].label->name = (char*)"radius"; c_inst->fields[1].label->name_len = 6;
    c_inst->fields[1].type = &t_u32;

    WASMComponentTypeInstance record_circle;
    record_circle.type = COMPONENT_VAL_TYPE_RECORD;
    record_circle.type_specific.record = c_inst;
    record_circle.alignment = 4; record_circle.elem_size = 12; // 6 (Point) + 2 (Pad) + 4 (u32)

    // 5. Context & Execute
   LiftOptions lift_opts;
    lift_opts.string_encoding = ENCODING_UTF_8;
    lift_opts.memory = mem;

    LiftLowerOptions lift_lower_opts;
    lift_lower_opts.lift_opts = &lift_opts;
    lift_lower_opts.realloc_func = nullptr;

    CanonicalOptions canonical_opts;
    canonical_opts.lift_lower_opts = &lift_lower_opts;
    canonical_opts.post_return_func = nullptr;
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;


    bool res = load(&cx, 620, &record_circle, &out_val);

    // 6. Verificare
    ASSERT_TRUE(res);
    ComponentWITRecordField *circle_fields = (ComponentWITRecordField *)out_val->value.record_value.fields;
    
    wit_value_t center_val = circle_fields[0].value;
    ComponentWITRecordField *point_fields = (ComponentWITRecordField *)center_val->value.record_value.fields;
    ASSERT_EQ(point_fields[0].value->value.u16_value, 10);
    ASSERT_EQ(point_fields[2].value->value.u16_value, 30);

    ASSERT_EQ(circle_fields[1].value->value.u32_value, 500);

    // Cleanup
    wasm_runtime_free(p_inst->fields[0].label); wasm_runtime_free(p_inst->fields[1].label); wasm_runtime_free(p_inst->fields[2].label);
    wasm_runtime_free(p_inst);
    wasm_runtime_free(c_inst->fields[0].label); wasm_runtime_free(c_inst->fields[1].label);
    wasm_runtime_free(c_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadVariantWithValue)
{
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
    // 2. Define Primitive Types
    // ---------------------------------------------------------
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

    // ---------------------------------------------------------
    // 3. Construct Variant Schema
    //    Target: variant { success(u32), failure(u8) }
    // ---------------------------------------------------------
    uint32_t case_count = 2;
    
    // Calculate total allocation size including flexible array
    uint32_t total_size = offsetof(WASMComponentVariantInstance, cases) + 
                          (case_count * sizeof(WASMComponentCaseValInstance));

    WASMComponentVariantInstance *var_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(total_size);
    ASSERT_NE(var_inst, nullptr);
    memset(var_inst, 0, total_size); // Ensure clean memory

    var_inst->count = case_count;

    // --- Case 0: "success" (u32) ---
    char success_label[] = "success";
    var_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[0].label->name = success_label;
    var_inst->cases[0].label->name_len = 7;
    // Note: Using 'value_type' as per your struct definition
    var_inst->cases[0].value_type = &t_u32; 

    // --- Case 1: "failure" (u8) ---
    char fail_label[] = "failure";
    var_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[1].label->name = fail_label;
    var_inst->cases[1].label->name_len = 7;
    var_inst->cases[1].value_type = &t_u8;

    // 4. Wrap in Main Type Instance
    WASMComponentTypeInstance variant_type;
    memset(&variant_type, 0, sizeof(WASMComponentTypeInstance));
    variant_type.type = COMPONENT_VAL_TYPE_VARIANT;
    variant_type.type_specific.variant = var_inst;
    // Alignment of a variant is the max alignment of its cases (max(4, 1) = 4)
    variant_type.alignment = 4; 

    // ---------------------------------------------------------
    // 5. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 800
    // Expected Memory:
    // 800: Discriminant 0 ("success")
    // 801-803: Padding (skipped due to alignment)
    // 804: Value 2024
    bool res = load(&cx, 800, &variant_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_VARIANT);
    
    // Check Discriminant
    ASSERT_STREQ(out_val->value.variant_value.discriminator, "success");
    
    // Check Payload
    wit_value_t payload = out_val->value.variant_value.value;
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(payload->value.u32_value, 2024);

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(var_inst->cases[0].label);
    wasm_runtime_free(var_inst->cases[1].label);
    wasm_runtime_free(var_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadVariantNoValue)
{
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
    // 2. Define Primitive Types
    // ---------------------------------------------------------
    // We define u32 because the "restricted" case uses it.
    // This affects the alignment of the whole variant, even if we load "none".
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32; 
    t_u32.alignment = 4; t_u32.elem_size = 4;

    // ---------------------------------------------------------
    // 3. Construct Variant Schema
    //    Target: variant { none, any, restricted(u32) }
    // ---------------------------------------------------------
    uint32_t case_count = 3;

    // Calculate total allocation size including flexible array
    uint32_t total_size = offsetof(WASMComponentVariantInstance, cases) + 
                          (case_count * sizeof(WASMComponentCaseValInstance));

    WASMComponentVariantInstance *var_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(total_size);
    ASSERT_NE(var_inst, nullptr);
    memset(var_inst, 0, total_size); // Ensure clean memory

    var_inst->count = case_count;

    // --- Case 0: "none" (No payload) ---
    char none_label[] = "none";
    var_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[0].label->name = none_label;
    var_inst->cases[0].label->name_len = 4;
    var_inst->cases[0].value_type = nullptr; // Explicitly no type

    // --- Case 1: "any" (No payload) ---
    char any_label[] = "any";
    var_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[1].label->name = any_label;
    var_inst->cases[1].label->name_len = 3;
    var_inst->cases[1].value_type = nullptr;

    // --- Case 2: "restricted" (u32) ---
    // Necessary to enforce Alignment=4 for the whole variant logic
    char restricted_label[] = "restricted";
    var_inst->cases[2].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    var_inst->cases[2].label->name = restricted_label;
    var_inst->cases[2].label->name_len = 10;
    var_inst->cases[2].value_type = &t_u32;

    // 4. Wrap in Main Type Instance
    WASMComponentTypeInstance variant_type;
    memset(&variant_type, 0, sizeof(WASMComponentTypeInstance));
    variant_type.type = COMPONENT_VAL_TYPE_VARIANT;
    variant_type.type_specific.variant = var_inst;
    // Alignment is max(align(none), align(u32)) = 4
    variant_type.alignment = 4; 

    // ---------------------------------------------------------
    // 5. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 900
    // Expected Memory:
    // 900: Discriminant 0 ("none")
    // No payload follows.
    bool res = load(&cx, 900, &variant_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_VARIANT);
    
    // Check Discriminant
    ASSERT_STREQ(out_val->value.variant_value.discriminator, "none");
    
    // Check Payload
    // Since "none" has no type, the value should be nullptr 
    ASSERT_EQ(out_val->value.variant_value.value, nullptr);

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(var_inst->cases[0].label);
    wasm_runtime_free(var_inst->cases[1].label);
    wasm_runtime_free(var_inst->cases[2].label);
    wasm_runtime_free(var_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadSmallFlags)
{
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
    // 2. Construct Flag Schema (Input)
    //    Schema: flags { read, write, exec }
    // ---------------------------------------------------------
    uint32_t flag_count = 3;

    // Allocate the Flag Type definition structure
    WASMComponentFlagType *flag_inst = (WASMComponentFlagType *)wasm_runtime_malloc(sizeof(WASMComponentFlagType));
    ASSERT_NE(flag_inst, nullptr);
    
    // Allocate the array of names
    flag_inst->count = flag_count;
    flag_inst->flags = (WASMComponentCoreName *)wasm_runtime_malloc(flag_count * sizeof(WASMComponentCoreName));
    ASSERT_NE(flag_inst->flags, nullptr);

    // Define Flag 0: "read" (Bit 0)
    char read[] = "read";
    flag_inst->flags[0].name = read;
    flag_inst->flags[0].name_len = 4;

    // Define Flag 1: "write" (Bit 1)
    char write[] = "write";
    flag_inst->flags[1].name = write;
    flag_inst->flags[1].name_len = 5;

    // Define Flag 2: "exec" (Bit 2)
    char exec[] = "exec";
    flag_inst->flags[2].name = exec;
    flag_inst->flags[2].name_len = 4;

    // ---------------------------------------------------------
    // 3. Wrap in Type Instance
    // ---------------------------------------------------------
    WASMComponentTypeInstance type_instance;
    memset(&type_instance, 0, sizeof(WASMComponentTypeInstance));
    type_instance.type = COMPONENT_VAL_TYPE_FLAGS;
    type_instance.type_specific.flag = flag_inst;
    
    // Canonical ABI: 0-8 flags = 1 byte size, 1 byte alignment
    type_instance.elem_size = 1;
    type_instance.alignment = 1;

    // ---------------------------------------------------------
    // 4. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 1000
    // Memory value: 0x05 (Binary 101)
    // Expected: read=true, write=false, exec=true
    bool res = load(&cx, 1000, &type_instance, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 5. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_FLAGS);
    
    ComponentWITFlag flag_result = out_val->value.flag_value;
    ASSERT_EQ(flag_result.size, 3);
    
    // Verify Flag 0: "read" (Should be TRUE)
    ASSERT_STREQ(flag_result.fields[0].key, "read");
    ASSERT_EQ(flag_result.fields[0].value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(flag_result.fields[0].value->prim_type, WASM_COMP_PRIMVAL_BOOL);
    ASSERT_EQ(flag_result.fields[0].value->value.bool_value, true);

    // Verify Flag 1: "write" (Should be FALSE)
    ASSERT_STREQ(flag_result.fields[1].key, "write");
    ASSERT_EQ(flag_result.fields[1].value->value.bool_value, false);

    // Verify Flag 2: "exec" (Should be TRUE)
    ASSERT_STREQ(flag_result.fields[2].key, "exec");
    ASSERT_EQ(flag_result.fields[2].value->value.bool_value, true);

    // ---------------------------------------------------------
    // 6. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(flag_inst->flags);
    wasm_runtime_free(flag_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadMediumFlags)
{
    // 1. Runtime Init
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Construct Flag Schema (Input)
    //    Schema: flags { f0, f1, ... f9 } (10 flags)
    // ---------------------------------------------------------
    uint32_t flag_count = 10;

    WASMComponentFlagType *flag_inst = (WASMComponentFlagType *)wasm_runtime_malloc(sizeof(WASMComponentFlagType));
    ASSERT_NE(flag_inst, nullptr);
    
    flag_inst->count = flag_count;
    flag_inst->flags = (WASMComponentCoreName *)wasm_runtime_malloc(flag_count * sizeof(WASMComponentCoreName));
    ASSERT_NE(flag_inst->flags, nullptr);

    // Generate names f0 through f9
    // Note: In a real scenario, you might want to use strdup or static strings. 
    // Here we point to static string literals for simplicity.
    char f0[] = "f0";
    char f1[] = "f1";
    char f2[] = "f2";
    char f3[] = "f3";
    char f4[] = "f4";
    char f5[] = "f5";
    char f6[] = "f6";
    char f7[] = "f7";
    char f8[] = "f8";
    char f9[] = "f9";

    flag_inst->flags[0].name = f0; flag_inst->flags[0].name_len = 2;
    flag_inst->flags[1].name = f1; flag_inst->flags[1].name_len = 2;
    flag_inst->flags[2].name = f2; flag_inst->flags[2].name_len = 2;
    flag_inst->flags[3].name = f3; flag_inst->flags[3].name_len = 2;
    flag_inst->flags[4].name = f4; flag_inst->flags[4].name_len = 2;
    flag_inst->flags[5].name = f5; flag_inst->flags[5].name_len = 2;
    flag_inst->flags[6].name = f6; flag_inst->flags[6].name_len = 2;
    flag_inst->flags[7].name = f7; flag_inst->flags[7].name_len = 2;
    flag_inst->flags[8].name = f8; flag_inst->flags[8].name_len = 2;
    flag_inst->flags[9].name = f9; flag_inst->flags[9].name_len = 2;

    // ---------------------------------------------------------
    // 3. Wrap in Type Instance
    // ---------------------------------------------------------
    WASMComponentTypeInstance type_instance;
    memset(&type_instance, 0, sizeof(WASMComponentTypeInstance));
    type_instance.type = COMPONENT_VAL_TYPE_FLAGS;
    type_instance.type_specific.flag = flag_inst;
    
    // Canonical ABI: 9-16 flags = 2 bytes size, 2 bytes alignment
    type_instance.elem_size = 2;
    type_instance.alignment = 2;

    // ---------------------------------------------------------
    // 4. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 1002
    // Memory value: 513 (0x0201) -> Binary: ...1000000001
    // Expected: f0=true, f9=true, others=false
    bool res = load(&cx, 1002, &type_instance, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 5. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_FLAGS);
    ComponentWITFlag flag_result = out_val->value.flag_value;
    ASSERT_EQ(flag_result.size, 10);

    // Check f0 (Bit 0)
    ASSERT_STREQ(flag_result.fields[0].key, "f0");
    ASSERT_EQ(flag_result.fields[0].value->value.bool_value, true);

    // Check f1 (Bit 1) - Should be false
    ASSERT_STREQ(flag_result.fields[1].key, "f1");
    ASSERT_EQ(flag_result.fields[1].value->value.bool_value, false);

    // Check f9 (Bit 9) - Should be true
    ASSERT_STREQ(flag_result.fields[9].key, "f9");
    ASSERT_EQ(flag_result.fields[9].value->value.bool_value, true);

    // ---------------------------------------------------------
    // 6. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(flag_inst->flags);
    wasm_runtime_free(flag_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadListU8)
{
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
    // 2. Define Element Type (u8)
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8; 
    // u8 is 1 byte aligned, 1 byte size
    t_u8.alignment = 1; 
    t_u8.elem_size = 1;

    // ---------------------------------------------------------
    // 3. Define List Type (list<u8>)
    // ---------------------------------------------------------
    // Allocate the List structure using your definition
    WASMComponentListInstance *list_inst = (WASMComponentListInstance *)wasm_runtime_malloc(sizeof(WASMComponentListInstance));
    ASSERT_NE(list_inst, nullptr);
    
    // Link the element type
    list_inst->element_type = &t_u8;

    // Wrap in Type Instance
    WASMComponentTypeInstance list_type;
    memset(&list_type, 0, sizeof(WASMComponentTypeInstance));
    list_type.type = COMPONENT_VAL_TYPE_LIST;
    list_type.type_specific.list = list_inst;
    
    // Critical: A "List" type itself (the descriptor) is ALWAYS 2 x i32.
    // So alignment is 4, size is 8.
    // This refers to the [ptr, len] pair, NOT the array size.
    list_type.alignment = 4;
    list_type.elem_size = 8;

    // ---------------------------------------------------------
    // 4. Execute Load
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // Load from Offset 1200
    // It reads {ptr: 25000, len: 5} from 1200.
    // Then it reads 5 bytes starting at 25000.
    bool res = load(&cx, 1200, &list_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 5. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_LIST);
    
    // Check List Size
    ASSERT_EQ(out_val->value.list_value.size, 5);
    ASSERT_NE(out_val->value.list_value.elems, nullptr);

    // Access the array of WIT values
    wit_value_t *elements = out_val->value.list_value.elems;

    // Verify Element 0: 10
    ASSERT_EQ(elements[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(elements[0]->value.u8_value, 10);

    // Verify Element 1: 20
    ASSERT_EQ(elements[1]->value.u8_value, 20);

    // Verify Element 2: 30
    ASSERT_EQ(elements[2]->value.u8_value, 30);

    // Verify Element 3: 40
    ASSERT_EQ(elements[3]->value.u8_value, 40);

    // Verify Element 4: 50
    ASSERT_EQ(elements[4]->value.u8_value, 50);

    // ---------------------------------------------------------
    // 6. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(list_inst);
    // Note: free_wit_value should recursively free the elements array
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestDynamicListU16)
{
    // 1. Init Runtime
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 2. Define Element Type (u16)
    WASMComponentTypeInstance t_u16;
    memset(&t_u16, 0, sizeof(WASMComponentTypeInstance));
    t_u16.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u16.type_specific.primval = WASM_COMP_PRIMVAL_U16; 
    t_u16.alignment = 2; t_u16.elem_size = 2;

    // 3. Construct Dynamic List Type
    WASMComponentListInstance *list_inst = (WASMComponentListInstance *)wasm_runtime_malloc(sizeof(WASMComponentListInstance));
    list_inst->element_type = &t_u16;

    WASMComponentTypeInstance list_type;
    memset(&list_type, 0, sizeof(WASMComponentTypeInstance));
    list_type.type = COMPONENT_VAL_TYPE_LIST;       // <--- Triggers Case 1
    list_type.type_specific.list = list_inst;
    list_type.alignment = 4; // Pointer alignment
    list_type.elem_size = 8; // Size of (ptr, len)

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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // 5. Load from Offset 1300
    // Reads ptr/len from 1300, then jumps to 30000
    bool res = load(&cx, 1300, &list_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(out_val->value.list_value.size, 3);

    // Verify Values [100, 200, 300]
    wit_value_t *elems = out_val->value.list_value.elems;
    ASSERT_EQ(elems[0]->value.u16_value, 100);
    ASSERT_EQ(elems[1]->value.u16_value, 200);
    ASSERT_EQ(elems[2]->value.u16_value, 300);

    // Cleanup
    wasm_runtime_free(list_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestFixedSizeListU8)
{
    // 1. Init Runtime
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 2. Define Element Type (u8)
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8; 
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // 3. Construct Fixed List Type (Length 4)
    WASMComponentListLenInstance *fixed_inst = (WASMComponentListLenInstance *)wasm_runtime_malloc(sizeof(WASMComponentListLenInstance));
    fixed_inst->element_type = &t_u8;
    fixed_inst->len = 4; // <--- Length is hardcoded in the type!

    WASMComponentTypeInstance fixed_list_type;
    memset(&fixed_list_type, 0, sizeof(WASMComponentTypeInstance));
    fixed_list_type.type = COMPONENT_VAL_TYPE_FIXED_SIZE_LIST; // <--- Triggers Case 2
    fixed_list_type.type_specific.list_len = fixed_inst;
    
    // Size Logic: count * elem_size = 4 * 1 = 4 bytes
    fixed_list_type.alignment = 1; 
    fixed_list_type.elem_size = 4; 

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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // 5. Load from Offset 1320
    // Reads 4 bytes directly from 1320. No pointer indirection.
    // Memory: AA BB CC DD
    bool res = load(&cx, 1320, &fixed_list_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_LIST); // Result is still a list in WIT
    ASSERT_EQ(out_val->value.list_value.size, 4);

    // Verify Values
    wit_value_t *elems = out_val->value.list_value.elems;
    ASSERT_EQ(elems[0]->value.u8_value, 0xAA);
    ASSERT_EQ(elems[1]->value.u8_value, 0xBB);
    ASSERT_EQ(elems[2]->value.u8_value, 0xCC);
    ASSERT_EQ(elems[3]->value.u8_value, 0xDD);

    // Cleanup
    wasm_runtime_free(fixed_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestDynamicListOfRecords)
{
    // ---------------------------------------------------------
    // 1. Init Runtime
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // ---------------------------------------------------------
    // 2. Define Primitive Type (u8)
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8; 
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // ---------------------------------------------------------
    // 3. Construct Record Type: Point { x: u8, y: u8 }
    // ---------------------------------------------------------
    uint32_t field_count = 2;
    uint32_t record_alloc_size = sizeof(WASMComponentRecordInstance) + 
                                 (field_count * sizeof(WASMComponentLabelValTypeInstance));

    WASMComponentRecordInstance *rec_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_alloc_size);
    ASSERT_NE(rec_inst, nullptr);
    memset(rec_inst, 0, record_alloc_size);

    rec_inst->count = field_count;

    // --- Field 0: "x" ---
    char x_label[] = "x";
    rec_inst->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    rec_inst->fields[0].label->name = x_label;
    rec_inst->fields[0].label->name_len = 1;
    rec_inst->fields[0].type = &t_u8;

    // --- Field 1: "y" ---
    char y_label[] = "y";
    rec_inst->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    rec_inst->fields[1].label->name = y_label;
    rec_inst->fields[1].label->name_len = 1;
    rec_inst->fields[1].type = &t_u8;

    // Wrap Record in Type Instance
    WASMComponentTypeInstance record_type;
    memset(&record_type, 0, sizeof(WASMComponentTypeInstance));
    record_type.type = COMPONENT_VAL_TYPE_RECORD;
    record_type.type_specific.record = rec_inst;
    record_type.alignment = 1; // 1 byte alignment (max of fields)
    record_type.elem_size = 2; // 1 + 1 = 2 bytes total

    // ---------------------------------------------------------
    // 4. Construct List Type: list<Point>
    // ---------------------------------------------------------
    WASMComponentListInstance *list_inst = (WASMComponentListInstance *)wasm_runtime_malloc(sizeof(WASMComponentListInstance));
    ASSERT_NE(list_inst, nullptr);
    
    // Link the element type to our Record Type
    list_inst->element_type = &record_type;

    WASMComponentTypeInstance list_type;
    memset(&list_type, 0, sizeof(WASMComponentTypeInstance));
    list_type.type = COMPONENT_VAL_TYPE_LIST;
    list_type.type_specific.list = list_inst;
    list_type.alignment = 4; // List descriptor alignment is always 4
    list_type.elem_size = 8; // List descriptor size is always 8 (ptr+len)

    // ---------------------------------------------------------
    // 5. Setup Context
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
    canonical_opts.async = true;
    canonical_opts.callback_func = nullptr;

    LiftLowerContext cx;
    cx.canonical_opts = &canonical_opts;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t out_val = nullptr;

    // ---------------------------------------------------------
    // 6. Load from Offset 1400
    // ---------------------------------------------------------
    // It reads {ptr: 35000, len: 2} from 1400.
    // Then it iterates 2 times, loading records from 35000.
    bool res = load(&cx, 1400, &list_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(out_val->value.list_value.size, 2);

    wit_value_t *elems = out_val->value.list_value.elems;

    // --- Verify Element 0: {x: 10, y: 20} ---
    ASSERT_EQ(elems[0]->type, COMPONENT_VAL_TYPE_RECORD);
    ComponentWITRecordField *fields0 = elems[0]->value.record_value.fields;
    
    // Check 'x'
    ASSERT_STREQ(fields0[0].key, x_label);
    ASSERT_EQ(fields0[0].value->value.u8_value, 10);
    // Check 'y'
    ASSERT_STREQ(fields0[1].key, y_label);
    ASSERT_EQ(fields0[1].value->value.u8_value, 20);

    // --- Verify Element 1: {x: 30, y: 40} ---
    ASSERT_EQ(elems[1]->type, COMPONENT_VAL_TYPE_RECORD);
    ComponentWITRecordField *fields1 = elems[1]->value.record_value.fields;

    // Check 'x'
    ASSERT_STREQ(fields1[0].key, x_label);
    ASSERT_EQ(fields1[0].value->value.u8_value, 30);
    // Check 'y'
    ASSERT_STREQ(fields1[1].key, y_label);
    ASSERT_EQ(fields1[1].value->value.u8_value, 40);

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    wasm_runtime_free(rec_inst->fields[0].label);
    wasm_runtime_free(rec_inst->fields[1].label);
    wasm_runtime_free(rec_inst);
    wasm_runtime_free(list_inst);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadTupleU64String)
{
    // ---------------------------------------------------------
    // 1. Runtime Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // ---------------------------------------------------------
    // 2. Define Types
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_u64;
    memset(&t_u64, 0, sizeof(WASMComponentTypeInstance));
    t_u64.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_u64.type_specific.primval = WASM_COMP_PRIMVAL_U64; 
    t_u64.alignment = 8; t_u64.elem_size = 8;

    WASMComponentTypeInstance t_string;
    memset(&t_string, 0, sizeof(WASMComponentTypeInstance));
    t_string.type = COMPONENT_VAL_TYPE_PRIMVAL; 
    t_string.type_specific.primval = WASM_COMP_PRIMVAL_STRING;
    t_string.alignment = 4; t_string.elem_size = 8;

    // ---------------------------------------------------------
    // 3. Construct Tuple Type: tuple<u64, string>
    // ---------------------------------------------------------
    uint32_t field_count = 2;
    uint32_t total_size = (field_count * sizeof(WASMComponentTypeInstance));
    
    WASMComponentTupleInstance *tuple_inst = (WASMComponentTupleInstance *)wasm_runtime_malloc(sizeof(WASMComponentTupleInstance));

    ASSERT_NE(tuple_inst, nullptr);
    memset(tuple_inst, 0, sizeof(WASMComponentTupleInstance));
    tuple_inst->count = field_count;

    tuple_inst->element_types = (WASMComponentTypeInstance **)wasm_runtime_malloc(total_size);
    ASSERT_NE(tuple_inst->element_types, nullptr);
    memset(tuple_inst->element_types, 0, total_size);
    tuple_inst->element_types[0] = &t_u64;
    tuple_inst->element_types[1] = &t_string;

    WASMComponentTypeInstance tuple_type;
    memset(&tuple_type, 0, sizeof(WASMComponentTypeInstance));
    tuple_type.type = COMPONENT_VAL_TYPE_TUPLE;
    tuple_type.type_specific.tuple = tuple_inst;
    
    tuple_type.alignment = 8; 
    tuple_type.elem_size = 16;

    // ---------------------------------------------------------
    // 4. Setup Context
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
    wit_value_t out_val = nullptr;

    // ---------------------------------------------------------
    // 5. Load from Offset 4000
    // ---------------------------------------------------------
    bool res = load(&cx, 4000, &tuple_type, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    // ---------------------------------------------------------
    // 6. Verify Results
    // ---------------------------------------------------------
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_TUPLE);

    wit_value_t *elems = out_val->value.list_value.elems;
    // Field 0: u64 = 100
    ASSERT_EQ(out_val->value.tuple_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(out_val->value.tuple_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(out_val->value.tuple_value.elems[0]->value.u64_value, 100);

    // Field 1: string = "HelloTuple"
    ASSERT_EQ(out_val->value.tuple_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(out_val->value.tuple_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_STREQ(out_val->value.tuple_value.elems[1]->value.string_value.chars, "HelloTuple");

    // Cleanup
    wasm_runtime_free(tuple_inst);
    // TODO cannot free memory for record instance used here in this test
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadEnumFromSpecificOffset)
{
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    uint32_t label_count = 3;
    WASMComponentEnumType enum_schema;
    enum_schema.count = label_count;
    enum_schema.labels = (WASMComponentCoreName *)wasm_runtime_malloc(label_count * sizeof(WASMComponentCoreName));

    enum_schema.labels[0].name = (char*)"Red";
    enum_schema.labels[0].name_len = 3;

    enum_schema.labels[1].name = (char*)"Green";
    enum_schema.labels[1].name_len = 5;

    enum_schema.labels[2].name = (char*)"Blue";
    enum_schema.labels[2].name_len = 4;

    // 3. Definim tipul ENUM pentru functia load
    WASMComponentTypeInstance t_enum;
    memset(&t_enum, 0, sizeof(WASMComponentTypeInstance));
    t_enum.type = COMPONENT_VAL_TYPE_ENUM;
    t_enum.type_specific.enum_type = &enum_schema;
    t_enum.alignment = 1; 
    t_enum.elem_size = 1;

    // 4. Pregatim contextul de executie
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
    
    wit_value_t out_val = nullptr;

    bool res = load(&cx, 4151, &t_enum, &out_val);

    ASSERT_TRUE(res);
    ASSERT_NE(out_val, nullptr);

    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(out_val->value.enum_value.value, 1u); // Green is index 1

    free_wit_value(out_val);

    res = load(&cx, 4152, &t_enum, &out_val);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(out_val->value.enum_value.value, 2u); // Blue is index 2

    // 6. Cleanup
    wasm_runtime_free(enum_schema.labels);
    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadOptionU32)
{
    // 1. Setup Runtime
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 2. Define Internal Type (u32)
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // 3. Define Option Type Schema
    WASMComponentOptionInstance option_inst;
    option_inst.element_type = &t_u32;

    WASMComponentTypeInstance t_option;
    memset(&t_option, 0, sizeof(WASMComponentTypeInstance));
    t_option.type = COMPONENT_VAL_TYPE_OPTION;
    t_option.type_specific.option = &option_inst;
    t_option.alignment = 4;   // Max of discriminant(1) and u32(4)
    t_option.elem_size = 8;    // 1 (tag) + 3 (pad) + 4 (u32)

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
    
    wit_value_t out_val = nullptr;

    // --- TEST A: Load "none" from 4200 ---
    bool res = load(&cx, 4200, &t_option, &out_val);
    ASSERT_TRUE(res);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_EQ(out_val->value.option_value.optional_elem, nullptr);
    free_wit_value(out_val);

    // --- TEST B: Load "some(12345)" from 4208 ---
    res = load(&cx, 4208, &t_option, &out_val);
    ASSERT_TRUE(res);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_OPTION);

    // Check Payload
    wit_value_t payload = out_val->value.option_value.optional_elem;
    ASSERT_NE(payload, nullptr);
    ASSERT_EQ(payload->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(payload->value.u32_value, 12345);

    free_wit_value(out_val);
}

TEST_F(ComponentInstantiationTest, TestLoadResultU32U8)
{
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 1. Define Inner Types
    WASMComponentTypeInstance t_u32;
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    WASMComponentTypeInstance t_u8;
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    // 2. Define Result Schema: result<u32, u8>
    WASMComponentResultInstance res_inst;
    res_inst.result_type = &t_u32;
    res_inst.error_type = &t_u8;

    WASMComponentTypeInstance t_result;
    memset(&t_result, 0, sizeof(WASMComponentTypeInstance));
    t_result.type = COMPONENT_VAL_TYPE_RESULT;
    t_result.type_specific.result = &res_inst;
    t_result.alignment = 4;
    t_result.elem_size = 8; // 4 (tag + pad) + 4 (u32)

    // 3. Setup Context
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
    
    wit_value_t out_val = nullptr;

    // --- TEST A: Load OK(1000) from 4300 ---
    bool success = load(&cx, 4300, &t_result, &out_val);
    ASSERT_TRUE(success);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_FALSE(out_val->value.result_value.is_err);
    ASSERT_EQ(out_val->value.result_value.result.ok->value.u32_value, 1000);
    free_wit_value(out_val);

    // --- TEST B: Load ERR(255) from 4312 ---
    success = load(&cx, 4312, &t_result, &out_val);
    ASSERT_TRUE(success);
    ASSERT_EQ(out_val->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_TRUE(out_val->value.result_value.is_err);
    ASSERT_EQ(out_val->value.result_value.result.err->value.u8_value, 255);
    free_wit_value(out_val);
}

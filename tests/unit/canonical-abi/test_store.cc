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

class ComponentInstantiationStoreTest : public testing::Test {
  public:
    ComponentInstantiationStoreTest() {}
    ~ComponentInstantiationStoreTest() {}
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
        loaded_wasm_file = "stored.wasm";
    }

    virtual void TearDown() {
        helper->do_teardown();
        helper = nullptr;
    }
};

TEST_F(ComponentInstantiationStoreTest, TestStoreMemoryInstantiation) {
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

TEST_F(ComponentInstantiationStoreTest, TestStoreS8InMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup type for s8
    WASMComponentTypeInstance s8_type;
    memset(&s8_type, 0, sizeof(WASMComponentTypeInstance));
    s8_type.type = COMPONENT_VAL_TYPE_PRIMVAL;
    s8_type.type_specific.primval = WASM_COMP_PRIMVAL_S8;
    s8_type.alignment = compute_alignment(&s8_type);
    s8_type.elem_size = compute_elem_size(&s8_type);

    // 3. Get LiftLowerContext using the function's runtime options
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 4. CREATE a wit_value_t with an S8 value to store
    int8_t val = -42;
    wit_value_t value_to_store = wit_s8_ctor(val);
    ASSERT_NE(value_to_store, nullptr);

    // 5. STORE the value to memory at offset 100
    uint32_t store_addr = 3000;
    bool store_result = store(&cx, store_addr, &s8_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 6. LOAD it back to verify
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &s8_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(loaded_value->value.s8_value, val);

    // 7. Verify by directly inspecting memory
    int8_t *mem_ptr = (int8_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*mem_ptr, val);

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreStringInMemory) {
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
    ASSERT_NE(realloc_func, nullptr) << "cabi_realloc function not found in stored.wasm";

    // 4. Create a wit_value_t with a string value
    const char *test_string = "Hello, Store!";
    wit_value_t value_to_store = wit_string_ctor(
        strdup(test_string),        // chars (will be freed by wit_value)
        strlen(test_string),        // size_bytes
        strlen(test_string),        // tagged_code_units (UTF-8 = byte count)
        ENCODING_UTF_8              // hint_encoding
    );
    ASSERT_NE(value_to_store, nullptr);

    // 5. STORE the string to memory at offset 200
    // This stores the (ptr, len) pair at offset 200
    // The actual string bytes are allocated via realloc
    uint32_t store_addr = 200;
    bool store_result = store(&cx, store_addr, &string_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 6. Load it back to verify
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &string_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_STREQ(loaded_value->value.string_value.chars, test_string);
    ASSERT_EQ(loaded_value->value.string_value.size_bytes, strlen(test_string));

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreListInMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup type for u8
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1;
    t_u8.elem_size = 1;

    // 3. Define List Type (list<u8>)
    WASMComponentListInstance *list_inst = (WASMComponentListInstance *)wasm_runtime_malloc(sizeof(WASMComponentListInstance));
    ASSERT_NE(list_inst, nullptr);

    list_inst->element_type = &t_u8;

    WASMComponentTypeInstance list_type;
    memset(&list_type, 0, sizeof(WASMComponentTypeInstance));
    list_type.type = COMPONENT_VAL_TYPE_LIST;
    list_type.type_specific.list = list_inst;
    list_type.alignment = 4;  // [ptr, len] pair
    list_type.elem_size = 8;  // 2 x i32

    // 4. Get LiftLowerContext
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);
    ASSERT_NE(component_func->canon_options, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    WASMFunctionInstance *realloc_func = cx.canonical_opts->lift_lower_opts->realloc_func;
    ASSERT_NE(realloc_func, nullptr) << "cabi_realloc function not found";

    // 5. Create list elements (array of u8 values: [10, 20, 30, 40, 50])
    const uint32_t list_length = 5;
    wit_value_t *elements = (wit_value_t *)wasm_runtime_malloc(list_length * sizeof(wit_value_t));
    ASSERT_NE(elements, nullptr);

    elements[0] = wit_u8_ctor(10);
    elements[1] = wit_u8_ctor(20);
    elements[2] = wit_u8_ctor(30);
    elements[3] = wit_u8_ctor(40);
    elements[4] = wit_u8_ctor(50);

    // 6. Create the list wit_value_t
    wit_value_t value_to_store = wit_list_ctor(elements, list_length);
    ASSERT_NE(value_to_store, nullptr);
    ASSERT_EQ(value_to_store->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(value_to_store->value.list_value.size, list_length);

    // 7. STORE the list to memory at offset 200
    // This stores the (ptr, len) pair at offset 200
    // The actual list elements are allocated via realloc
    uint32_t store_addr = 200;
    bool store_result = store(&cx, store_addr, &list_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 8. Load it back to verify
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &list_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(loaded_value->value.list_value.size, list_length);

    // 9. Verify each element matches
    for (uint32_t i = 0; i < list_length; i++) {
        wit_value_t elem = loaded_value->value.list_value.elems[i];
        ASSERT_NE(elem, nullptr);
        ASSERT_EQ(elem->type, COMPONENT_VAL_TYPE_PRIMVAL);
        ASSERT_EQ(elem->prim_type, WASM_COMP_PRIMVAL_U8);

        uint8_t expected_value = (i + 1) * 10;  // 10, 20, 30, 40, 50
        ASSERT_EQ(elem->value.u8_value, expected_value)
            << "Element " << i << " mismatch";
    }

    // 10. Verify by directly reading the stored ptr/len pair
    uint32_t *ptr_len_pair = (uint32_t *)(mem->memory_data + store_addr);
    uint32_t stored_ptr = ptr_len_pair[0];
    uint32_t stored_len = ptr_len_pair[1];

    ASSERT_EQ(stored_len, list_length);
    ASSERT_GT(stored_ptr, 0);  // Should be allocated somewhere in memory

    // Verify the actual bytes in memory
    uint8_t *list_data = mem->memory_data + stored_ptr;
    for (uint32_t i = 0; i < list_length; i++) {
        ASSERT_EQ(list_data[i], (i + 1) * 10);
    }

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
    wasm_runtime_free(list_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreRecordInMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup field types
    // Field 1: u32 "x"
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // Field 2: u8 "y"
    WASMComponentTypeInstance t_u8;
    memset(&t_u8, 0, sizeof(WASMComponentTypeInstance));
    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1;
    t_u8.elem_size = 1;

    // 3. Create record type: record { x: u32, y: u8 }
    size_t record_size = sizeof(WASMComponentRecordInstance) + 2 * sizeof(WASMComponentLabelValTypeInstance);
    WASMComponentRecordInstance *record_inst = (WASMComponentRecordInstance *)wasm_runtime_malloc(record_size);
    ASSERT_NE(record_inst, nullptr);
    memset(record_inst, 0, record_size);
    record_inst->count = 2;

    // Field 0: "x" -> u32
    record_inst->fields[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    record_inst->fields[0].label->name = strdup("x");
    record_inst->fields[0].label->name_len = 1;
    record_inst->fields[0].type = &t_u32;

    // Field 1: "y" -> u8
    record_inst->fields[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    record_inst->fields[1].label->name = strdup("y");
    record_inst->fields[1].label->name_len = 1;
    record_inst->fields[1].type = &t_u8;

    // 4. Create record type instance
    WASMComponentTypeInstance record_type;
    memset(&record_type, 0, sizeof(WASMComponentTypeInstance));
    record_type.type = COMPONENT_VAL_TYPE_RECORD;
    record_type.type_specific.record = record_inst;
    record_type.alignment = compute_alignment(&record_type);
    record_type.elem_size = compute_elem_size(&record_type);

    // 5. Get LiftLowerContext
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 6. Create wit_value_t for record { x: 12345, y: 42 }
    ComponentWITRecordField *fields = (ComponentWITRecordField *)wasm_runtime_malloc(2 * sizeof(ComponentWITRecordField));
    ASSERT_NE(fields, nullptr);

    // Initialize field "x" = 12345
    fields[0].key = strdup("x");
    fields[0].key_size = 1;
    fields[0].value = wit_u32_ctor(12345);

    // Initialize field "y" = 42
    fields[1].key = strdup("y");
    fields[1].key_size = 1;
    fields[1].value = wit_u8_ctor(42);

    wit_value_t value_to_store = wit_record_ctor(fields, 2);
    ASSERT_NE(value_to_store, nullptr);

    // 7. Store at aligned address
    uint32_t store_addr = 256;  // 4-byte aligned
    bool store_result = store(&cx, store_addr, &record_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 8. Load it back
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &record_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(loaded_value->value.record_value.size, 2);

    // Verify field values
    ASSERT_EQ(loaded_value->value.record_value.fields[0].value->value.u32_value, 12345);
    ASSERT_EQ(loaded_value->value.record_value.fields[1].value->value.u8_value, 42);

    // 9. Verify memory layout
    uint32_t *x_ptr = (uint32_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*x_ptr, 12345);

    // y is at offset 4
    uint8_t *y_ptr = (uint8_t *)(mem->memory_data + store_addr + 4);
    ASSERT_EQ(*y_ptr, 42);

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
    free(record_inst->fields[0].label->name);
    wasm_runtime_free(record_inst->fields[0].label);
    free(record_inst->fields[1].label->name);
    wasm_runtime_free(record_inst->fields[1].label);
    wasm_runtime_free(record_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreVariantInMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup payload type (u32)
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // 3. Create variant type: variant { none, some(u32) }
    size_t variant_size = offsetof(WASMComponentVariantInstance, cases) + 2 * sizeof(WASMComponentCaseValInstance);
    WASMComponentVariantInstance *variant_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(variant_size);
    ASSERT_NE(variant_inst, nullptr);
    memset(variant_inst, 0, variant_size);
    variant_inst->count = 2;

    // Case 0: "none" with no payload
    variant_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    variant_inst->cases[0].label->name = strdup("none");
    variant_inst->cases[0].label->name_len = 4;
    variant_inst->cases[0].value_type = NULL;

    // Case 1: "some" with u32 payload
    variant_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    variant_inst->cases[1].label->name = strdup("some");
    variant_inst->cases[1].label->name_len = 4;
    variant_inst->cases[1].value_type = &t_u32;

    // 4. Create variant type instance
    WASMComponentTypeInstance variant_type;
    memset(&variant_type, 0, sizeof(WASMComponentTypeInstance));
    variant_type.type = COMPONENT_VAL_TYPE_VARIANT;
    variant_type.type_specific.variant = variant_inst;
    variant_type.alignment = compute_alignment(&variant_type);
    variant_type.elem_size = compute_elem_size(&variant_type);

    // 5. Get LiftLowerContext
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 6. Create wit_value_t for variant: some(99999)
    wit_value_t payload = wit_u32_ctor(99999);
    wit_value_t value_to_store = wit_variant_ctor((char*)"some", 4, payload);
    ASSERT_NE(value_to_store, nullptr);

    // 7. Store at aligned address
    uint32_t store_addr = 256;  // 4-byte aligned
    bool store_result = store(&cx, store_addr, &variant_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 8. Load it back
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &variant_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(loaded_value->value.variant_value.discriminator, "some");
    ASSERT_NE(loaded_value->value.variant_value.value, nullptr);
    ASSERT_EQ(loaded_value->value.variant_value.value->value.u32_value, 99999);

    // 9. Verify memory layout
    uint8_t *disc_ptr = (uint8_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*disc_ptr, 1);  // case index 1 = "some"

    uint32_t *payload_ptr = (uint32_t *)(mem->memory_data + store_addr + 4);
    ASSERT_EQ(*payload_ptr, 99999);

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
    free(variant_inst->cases[0].label->name);
    wasm_runtime_free(variant_inst->cases[0].label);
    free(variant_inst->cases[1].label->name);
    wasm_runtime_free(variant_inst->cases[1].label);
    wasm_runtime_free(variant_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreVariantNoneCase) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Setup payload type (u32) - for the "some" case definition
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    // 3. Create variant type: variant { none, some(u32) }
    size_t variant_size = offsetof(WASMComponentVariantInstance, cases) + 2 * sizeof(WASMComponentCaseValInstance);
    WASMComponentVariantInstance *variant_inst = (WASMComponentVariantInstance *)wasm_runtime_malloc(variant_size);
    ASSERT_NE(variant_inst, nullptr);
    memset(variant_inst, 0, variant_size);
    variant_inst->count = 2;

    // Case 0: "none" with no payload
    variant_inst->cases[0].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    variant_inst->cases[0].label->name = strdup("none");
    variant_inst->cases[0].label->name_len = 4;
    variant_inst->cases[0].value_type = NULL;  // No payload

    // Case 1: "some" with u32 payload
    variant_inst->cases[1].label = (WASMComponentCoreName *)wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    variant_inst->cases[1].label->name = strdup("some");
    variant_inst->cases[1].label->name_len = 4;
    variant_inst->cases[1].value_type = &t_u32;

    // 4. Create variant type instance
    WASMComponentTypeInstance variant_type;
    memset(&variant_type, 0, sizeof(WASMComponentTypeInstance));
    variant_type.type = COMPONENT_VAL_TYPE_VARIANT;
    variant_type.type_specific.variant = variant_inst;
    variant_type.alignment = compute_alignment(&variant_type);
    variant_type.elem_size = compute_elem_size(&variant_type);

    // 5. Get LiftLowerContext
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 6. Create wit_value_t for variant
    wit_value_t value_to_store = wit_variant_ctor((char*)"none", 4, NULL);
    ASSERT_NE(value_to_store, nullptr);

    // 7. Store at aligned address
    uint32_t store_addr = 256;
    bool store_result = store(&cx, store_addr, &variant_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 8. Load it back
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &variant_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(loaded_value->value.variant_value.discriminator, "none");
    ASSERT_EQ(loaded_value->value.variant_value.value, nullptr);

    // 9. Verify memory layout: discriminant = 0 (none is case 0)
    uint8_t *disc_ptr = (uint8_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*disc_ptr, 0);  // case index 0 = "none"

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
    free(variant_inst->cases[0].label->name);
    wasm_runtime_free(variant_inst->cases[0].label);
    free(variant_inst->cases[1].label->name);
    wasm_runtime_free(variant_inst->cases[1].label);
    wasm_runtime_free(variant_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreFlagsInMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Create flags type: flags { read, write, execute }
    WASMComponentFlagType *flag_type_inst = (WASMComponentFlagType *)wasm_runtime_malloc(sizeof(WASMComponentFlagType));
    ASSERT_NE(flag_type_inst, nullptr);
    memset(flag_type_inst, 0, sizeof(WASMComponentFlagType));
    flag_type_inst->count = 3;

    // Allocate the flags array separately (it's a pointer!)
    flag_type_inst->flags = (WASMComponentCoreName *)wasm_runtime_malloc(3 * sizeof(WASMComponentCoreName));
    ASSERT_NE(flag_type_inst->flags, nullptr);
    memset(flag_type_inst->flags, 0, 3 * sizeof(WASMComponentCoreName));

    // Set flag names
    flag_type_inst->flags[0].name = (char *)wasm_runtime_malloc(5);
    strcpy(flag_type_inst->flags[0].name, "read");
    flag_type_inst->flags[0].name_len = 4;

    flag_type_inst->flags[1].name = (char *)wasm_runtime_malloc(6);
    strcpy(flag_type_inst->flags[1].name, "write");
    flag_type_inst->flags[1].name_len = 5;

    flag_type_inst->flags[2].name = (char *)wasm_runtime_malloc(8);
    strcpy(flag_type_inst->flags[2].name, "execute");
    flag_type_inst->flags[2].name_len = 7;

    // 3. Create flags type instance
    WASMComponentTypeInstance flags_type;
    memset(&flags_type, 0, sizeof(WASMComponentTypeInstance));
    flags_type.type = COMPONENT_VAL_TYPE_FLAGS;
    flags_type.type_specific.flag = flag_type_inst;
    flags_type.alignment = compute_alignment(&flags_type);
    flags_type.elem_size = compute_elem_size(&flags_type);

    // With 3 flags, should be 1 byte
    ASSERT_EQ(flags_type.elem_size, 1);

    // 4. Get LiftLowerContext
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 5. Create wit_value_t for flags: { read: true, write: false, execute: true }
    ComponentWITRecordField *flag_fields = (ComponentWITRecordField *)wasm_runtime_malloc(3 * sizeof(ComponentWITRecordField));
    ASSERT_NE(flag_fields, nullptr);
    memset(flag_fields, 0, 3 * sizeof(ComponentWITRecordField));

    flag_fields[0].key = (char *)wasm_runtime_malloc(5);
    strcpy(flag_fields[0].key, "read");
    flag_fields[0].key_size = 4;
    flag_fields[0].value = wit_bool_ctor(true);

    flag_fields[1].key = (char *)wasm_runtime_malloc(6);
    strcpy(flag_fields[1].key, "write");
    flag_fields[1].key_size = 5;
    flag_fields[1].value = wit_bool_ctor(false);

    flag_fields[2].key = (char *)wasm_runtime_malloc(8);
    strcpy(flag_fields[2].key, "execute");
    flag_fields[2].key_size = 7;
    flag_fields[2].value = wit_bool_ctor(true);

    wit_value_t value_to_store = wit_flag_ctor(flag_fields, 3);
    ASSERT_NE(value_to_store, nullptr);

    // 6. Store at address
    uint32_t store_addr = 256;
    bool store_result = store(&cx, store_addr, &flags_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 7. Load it back
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &flags_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_NE(loaded_value, nullptr);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_FLAGS);

    // 8. Verify memory: should be byte 0b00000101 = 5
    uint8_t *flags_ptr = (uint8_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*flags_ptr, 0b00000101);  // read=1, write=0, execute=1

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);

    // Free the flag type instance
    wasm_runtime_free(flag_type_inst->flags[0].name);
    wasm_runtime_free(flag_type_inst->flags[1].name);
    wasm_runtime_free(flag_type_inst->flags[2].name);
    wasm_runtime_free(flag_type_inst->flags);
    wasm_runtime_free(flag_type_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreTupleInMemory) {
    // 1. Setup component and memory
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // 2. Define element types: u32 and s8
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    WASMComponentTypeInstance t_s8;
    memset(&t_s8, 0, sizeof(WASMComponentTypeInstance));
    t_s8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_s8.type_specific.primval = WASM_COMP_PRIMVAL_S8;
    t_s8.alignment = 1;
    t_s8.elem_size = 1;

    // 3. Construct Tuple Schema: tuple<u32, s8>
    WASMComponentTupleInstance *tuple_inst = (WASMComponentTupleInstance *)wasm_runtime_malloc(sizeof(WASMComponentTupleInstance));
    tuple_inst->count = 2;
    tuple_inst->element_types = (WASMComponentTypeInstance **)wasm_runtime_malloc(2 * sizeof(WASMComponentTypeInstance *));
    tuple_inst->element_types[0] = &t_u32;
    tuple_inst->element_types[1] = &t_s8;

    WASMComponentTypeInstance tuple_type;
    memset(&tuple_type, 0, sizeof(WASMComponentTypeInstance));
    tuple_type.type = COMPONENT_VAL_TYPE_TUPLE;
    tuple_type.type_specific.tuple = tuple_inst;
    tuple_type.alignment = compute_alignment(&tuple_type);
    tuple_type.elem_size = compute_elem_size(&tuple_type);
    std::cout << "alignment= " << tuple_type.alignment << ", elem_size= " << tuple_type.elem_size;

    // 4. Get Context
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // 5. Create wit_value_t for tuple: (123456, -10)
    // Note: wit_list_ctor is used because tuples are represented as lists of values internally
    wit_value_t *elements = (wit_value_t *)wasm_runtime_malloc(2 * sizeof(wit_value_t));
    elements[0] = wit_u32_ctor(123456);
    elements[1] = wit_s8_ctor(-10);
    
    // In many implementations, tuples use a specific ctor or reuse list ctor
    wit_value_t value_to_store = wit_tuple_ctor(elements, 2); 
    ASSERT_NE(value_to_store, nullptr);

    // 6. STORE at address 500
    uint32_t store_addr = 500; 
    bool store_result = store(&cx, store_addr, &tuple_type, value_to_store);
    ASSERT_TRUE(store_result);

    // 7. LOAD it back to verify the "Despecialization"
    // load_tuple will convert memory back to a TUPLE
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &tuple_type, &loaded_value);

    ASSERT_TRUE(load_result);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_TUPLE);

    // Verify 0 -> u32(123456)
    ASSERT_EQ(loaded_value->value.tuple_value.elems[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->value.tuple_value.elems[0]->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(loaded_value->value.tuple_value.elems[0]->value.u32_value, 123456);

    // Verify 1 -> s8(-10)
    ASSERT_EQ(loaded_value->value.tuple_value.elems[1]->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(loaded_value->value.tuple_value.elems[1]->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(loaded_value->value.tuple_value.elems[1]->value.s8_value, -10);

    // 8. Direct Memory Inspection
    uint32_t *mem_u32 = (uint32_t *)(mem->memory_data + store_addr);
    int8_t *mem_s8 = (int8_t *)(mem->memory_data + store_addr + 4);
    
    ASSERT_EQ(*mem_u32, 123456);
    ASSERT_EQ(*mem_s8, -10);

    // Cleanup
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
    wasm_runtime_free(tuple_inst->element_types);
    wasm_runtime_free(tuple_inst);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreEnumDirection) {
    // ---------------------------------------------------------
    // 1. Runtime and Component Initialization
    // ---------------------------------------------------------
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());

    WASMMemoryInstance *mem = helper->get_memories()[0];
    ASSERT_NE(mem, nullptr);

    // ---------------------------------------------------------
    // 2. Construct Enum Schema: Direction { North, East, South, West }
    // ---------------------------------------------------------
    uint32_t count = 4;
    WASMComponentEnumType *enum_type_inst = (WASMComponentEnumType *)wasm_runtime_malloc(sizeof(WASMComponentEnumType));
    ASSERT_NE(enum_type_inst, nullptr);
    enum_type_inst->count = count;
    enum_type_inst->labels = (WASMComponentCoreName *)wasm_runtime_malloc(count * sizeof(WASMComponentCoreName));

    const char* labels[] = {"North", "East", "South", "West"};
    for(uint32_t i = 0; i < count; i++) {
        enum_type_inst->labels[i].name = strdup(labels[i]);
        enum_type_inst->labels[i].name_len = (uint32_t)strlen(labels[i]);
    }

    // ---------------------------------------------------------
    // 3. Create the Main Type Instance
    // ---------------------------------------------------------
    WASMComponentTypeInstance t_enum;
    memset(&t_enum, 0, sizeof(WASMComponentTypeInstance));
    t_enum.type = COMPONENT_VAL_TYPE_ENUM;
    t_enum.type_specific.enum_type = enum_type_inst;
    // ABI Rule: 4 cases fit into a u8 (1 byte)
    t_enum.alignment = 1; 
    t_enum.elem_size = 1;

    // ---------------------------------------------------------
    // 4. Setup the LiftLowerContext
    // ---------------------------------------------------------
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    ASSERT_NE(component_func, nullptr);

    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    // ---------------------------------------------------------
    // 5. Create a wit_value_t for Enum: "South" (Index 2)
    // ---------------------------------------------------------
    // The enum value structure expects the index of the chosen label
    wit_value_t value_to_store = wit_enum_ctor(2); 
    ASSERT_NE(value_to_store, nullptr);

    // ---------------------------------------------------------
    // 6. Execute the STORE at Offset 500
    // ---------------------------------------------------------
    uint32_t store_addr = 500;
    bool store_result = store(&cx, store_addr, &t_enum, value_to_store);
    ASSERT_TRUE(store_result);

    // ---------------------------------------------------------
    // 7. Direct Memory Verification (Check Raw Bytes)
    // ---------------------------------------------------------
    // Index 2 (South) should be written as 0x02 in linear memory
    uint8_t *mem_ptr = (uint8_t *)(mem->memory_data + store_addr);
    ASSERT_EQ(*mem_ptr, 2);

    // ---------------------------------------------------------
    // 8. Load Verification (Check Despecialization)
    // ---------------------------------------------------------
    // Verification that the store_enum correctly mapped the index via convert_enum_instance_to_variant
    wit_value_t loaded_value = nullptr;
    bool load_result = load(&cx, store_addr, &t_enum, &loaded_value);
    ASSERT_TRUE(load_result);
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(loaded_value->value.enum_value.value, 2u); // South is index 2

    // ---------------------------------------------------------
    // 9. Cleanup
    // ---------------------------------------------------------
    for(uint32_t i = 0; i < count; i++) free(enum_type_inst->labels[i].name);
    wasm_runtime_free(enum_type_inst->labels);
    wasm_runtime_free(enum_type_inst);
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreOptionU32) {
    // ---------------------------------------------------------
    // 1. Runtime and Component Initialization
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
    WASMComponentTypeInstance t_u32;
    memset(&t_u32, 0, sizeof(WASMComponentTypeInstance));
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4;
    t_u32.elem_size = 4;

    WASMComponentOptionInstance option_inst;
    option_inst.element_type = &t_u32;

    WASMComponentTypeInstance t_option;
    memset(&t_option, 0, sizeof(WASMComponentTypeInstance));
    t_option.type = COMPONENT_VAL_TYPE_OPTION;
    t_option.type_specific.option = &option_inst;
    // Max alignment of cases: align(u8) is 1, align(u32) is 4 -> Result 4
    t_option.alignment = 4; 
    // Size: 1 (tag) + 3 (padding) + 4 (u32 payload) = 8 bytes
    t_option.elem_size = 8; 

    // ---------------------------------------------------------
    // 3. Setup Context
    // ---------------------------------------------------------
    WASMComponentFunctionInstance *component_func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx;
    cx.canonical_opts = component_func->canon_options;
    cx.inst = helper->component_inst;
    cx.borrow_scope.task = nullptr;

    wit_value_t value_to_store = nullptr;
    wit_value_t loaded_value = nullptr;

    // ---------------------------------------------------------
    // 4. TEST CASE A: option<u32> = none
    // ---------------------------------------------------------
    uint32_t addr_none = 500;
    // Create 'none' value (null inner element)
    value_to_store = wit_option_ctor(nullptr); 
    
    // Store 'none'
    ASSERT_TRUE(store(&cx, addr_none, &t_option, value_to_store));

    // Round-trip: Load it back
    ASSERT_TRUE(load(&cx, addr_none, &t_option, &loaded_value));

    // Assertions for 'none'
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_EQ(loaded_value->value.option_value.optional_elem, nullptr);

    free_wit_value(value_to_store);
    free_wit_value(loaded_value);

    // ---------------------------------------------------------
    // 5. TEST CASE B: option<u32> = some(42)
    // ---------------------------------------------------------
    uint32_t addr_some = 5016; // 4-byte aligned
    // Create 'some(42)' value
    wit_value_t inner_val = wit_u32_ctor(42);
    value_to_store = wit_option_ctor(inner_val);
    
    // Store 'some(42)'
    ASSERT_TRUE(store(&cx, addr_some, &t_option, value_to_store));

    // Round-trip: Load it back
    ASSERT_TRUE(load(&cx, addr_some, &t_option, &loaded_value));

    // Assertions for 'some'
    ASSERT_EQ(loaded_value->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(loaded_value->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(loaded_value->value.option_value.optional_elem->value.u32_value, 42);

    // ---------------------------------------------------------
    // 6. Direct Memory Inspection (Verification of ABI Padding)
    // ---------------------------------------------------------
    // The tag '1' should be at addr_some
    uint8_t tag = *(uint8_t*)(mem->memory_data + addr_some);
    ASSERT_EQ(tag, 1);
    // The value '42' should be at addr_some + 4 (after 3 bytes of padding)
    uint32_t val = *(uint32_t*)(mem->memory_data + addr_some + 4);
    ASSERT_EQ(val, 42);

    // ---------------------------------------------------------
    // 7. Cleanup
    // ---------------------------------------------------------
    free_wit_value(value_to_store);
    free_wit_value(loaded_value);
}

TEST_F(ComponentInstantiationStoreTest, TestStoreResult) {
    // 1. Initialize Runtime
    helper->reset_component();
    ASSERT_TRUE(helper->read_wasm_file(loaded_wasm_file.c_str()));
    ASSERT_TRUE(helper->load_component());
    ASSERT_TRUE(helper->instantiate_component());
    WASMMemoryInstance *mem = helper->get_memories()[0];

    // 2. Define result<u32, u8> Schema
    WASMComponentTypeInstance t_u32, t_u8;
    t_u32.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u32.type_specific.primval = WASM_COMP_PRIMVAL_U32;
    t_u32.alignment = 4; t_u32.elem_size = 4;

    t_u8.type = COMPONENT_VAL_TYPE_PRIMVAL;
    t_u8.type_specific.primval = WASM_COMP_PRIMVAL_U8;
    t_u8.alignment = 1; t_u8.elem_size = 1;

    WASMComponentResultInstance res_schema = { .result_type = &t_u32, .error_type = &t_u8 };
    WASMComponentTypeInstance result_type;
    memset(&result_type, 0, sizeof(WASMComponentTypeInstance));
    result_type.type = COMPONENT_VAL_TYPE_RESULT;
    result_type.type_specific.result = &res_schema;
    result_type.alignment = 4; // Max alignment of cases
    result_type.elem_size = 8; // Tag(1) + Pad(3) + u32(4)

    // 3. Setup Context
    WASMComponentFunctionInstance *func = wasm_component_lookup_function(helper->component_inst, "store-message");
    LiftLowerContext cx = { .canonical_opts = func->canon_options, .inst = helper->component_inst, .borrow_scope_type = BORROW_SCOPE_TASK, .borrow_scope = nullptr };

    // --- CASE 1: Store & Load OK(100) at Offset 500 ---
    uint32_t addr_ok = 500;
    wit_value_t payload_ok = wit_u32_ctor(100);
    wit_value_t value_to_store_ok = wit_result_ctor(false, payload_ok); // is_err = false

    ASSERT_TRUE(store(&cx, addr_ok, &result_type, value_to_store_ok));

    // Verify Memory: Tag 0 at 500, u32 100 at 504
    ASSERT_EQ(*(uint8_t*)(mem->memory_data + addr_ok), 0);
    ASSERT_EQ(*(uint32_t*)(mem->memory_data + addr_ok + 4), 100);

    wit_value_t loaded_ok = nullptr;
    ASSERT_TRUE(load(&cx, addr_ok, &result_type, &loaded_ok));
    ASSERT_EQ(loaded_ok->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_FALSE(loaded_ok->value.result_value.is_err);
    ASSERT_EQ(loaded_ok->value.result_value.result.ok->value.u32_value, 100);

    // --- CASE 2: Store & Load ERR(5) at Offset 512 ---
    uint32_t addr_err = 512;
    wit_value_t payload_err = wit_u8_ctor(5);
    wit_value_t value_to_store_err = wit_result_ctor(true, payload_err); // is_err = true

    ASSERT_TRUE(store(&cx, addr_err, &result_type, value_to_store_err));

    // Verify Memory: Tag 1 at 512, u8 5 at 516 (aligned to max payload)
    ASSERT_EQ(*(uint8_t*)(mem->memory_data + addr_err), 1);
    ASSERT_EQ(*(uint8_t*)(mem->memory_data + addr_err + 4), 5);

    wit_value_t loaded_err = nullptr;
    ASSERT_TRUE(load(&cx, addr_err, &result_type, &loaded_err));
    ASSERT_EQ(loaded_err->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_TRUE(loaded_err->value.result_value.is_err);
    ASSERT_EQ(loaded_err->value.result_value.result.err->value.u8_value, 5);

    // Cleanup
    free_wit_value(value_to_store_ok); free_wit_value(loaded_ok);
    free_wit_value(value_to_store_err); free_wit_value(loaded_err);
}
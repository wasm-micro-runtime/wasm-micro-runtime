/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "wasm_component_resource.h"
#include "wasm_component_resource_table.h"
#include "wasm_runtime_common.h"
#include "wasm_component.h"
#include "wasm_component_runtime.h"
}

class ResourceTableTest : public testing::Test
{
  public:
    ResourceTableTest() : table_(nullptr) {}
    ~ResourceTableTest() {}

    virtual void SetUp() {
        wasm_runtime_init();
    }

    virtual void TearDown() {
        if (table_) {
            wasm_component_table_destroy(table_);
            table_ = nullptr;
        }
        wasm_runtime_destroy();
    }

  protected:
    WASMComponentResourceTable *table_;

    // Helper function to create a test resource handle
    WASMResourceHandle* createTestResource(uint32_t rep = 42, bool own = true) {
        static WASMComponentResourceInstance dummy_rt = {
            .name = (char*)"test_resource",
            .interface_name = (char*)"test_interface",
            .impl = NULL,
            .drop_method = NULL,
            .new_method = NULL,
            .rep_method = NULL,
            .dtor_method = NULL,
            .ctor_method = NULL
        };

        return wasm_create_resource_handle(&dummy_rt, rep, own);
    }
};

// Test resource handle creation and destruction
TEST_F(ResourceTableTest, ResourceHandle_CreateDestroy)
{
    uint32_t test_rep = 456;
    WASMResourceHandle* handle = createTestResource(test_rep, true);

    ASSERT_NE(handle, nullptr);
    EXPECT_TRUE(handle->own);
    EXPECT_EQ(handle->rep, test_rep);
    EXPECT_EQ(handle->num_lends, 0u);

    wasm_destroy_resource_handle(handle);
}

// Test wrapper for i32 and guard conditions
TEST_F(ResourceTableTest, ResourceHandle_ConvenienceWrapper)
{
    static WASMComponentResourceInstance dummy_rt = {
        .name = (char*)"test_resource",
        .interface_name = (char*)"test_interface",
        .impl = NULL,
        .drop_method = NULL,
        .new_method = NULL,
        .rep_method = NULL,
        .dtor_method = NULL,
        .ctor_method = NULL
    };

    WASMResourceHandle* handle = createTestResource(123, true);

    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->rep, 123u);
    EXPECT_EQ(wasm_resource_handle_get_rep_i32(handle), 123u);

    wasm_destroy_resource_handle(handle);

    // Test guard conditions
    EXPECT_EQ(wasm_create_resource_handle(nullptr, 42, true), nullptr); // null rt
    EXPECT_EQ(wasm_create_resource_handle(&dummy_rt, 0, true), nullptr); // zero rep (invalid)

    // Test wrapper with null handle
    EXPECT_EQ(wasm_resource_handle_get_rep_i32(nullptr), 0u);
}

TEST_F(ResourceTableTest, Table_InitDestroy)
{
    table_ = wasm_component_table_init(4, 50);
    ASSERT_NE(table_, nullptr);

    EXPECT_EQ(table_->array_size, 4u);
    EXPECT_EQ(table_->free_count, 0u);
    EXPECT_EQ(table_->next_index, 1u); // Index 0 is reserved
    EXPECT_EQ(table_->resize_percent, 50u);
    EXPECT_NE(table_->array, nullptr);
    EXPECT_NE(table_->free_list, nullptr);
}

// Test adding and removing a single resource
TEST_F(ResourceTableTest, Table_AddRemoveSingle)
{
    table_ = wasm_component_table_init(4, 50);
    ASSERT_NE(table_, nullptr);

    // Create and add a resource
    WASMResourceHandle* handle = createTestResource();
    ASSERT_NE(handle, nullptr);

    uint32_t index;
    bool result = wasm_component_table_add(table_, handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, &index);

    EXPECT_TRUE(result);
    EXPECT_EQ(index, 1u); // First valid index should be 1
    EXPECT_EQ(table_->next_index, 2u);

    // Retrieve the resource
    WASMResourceHandle* retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    EXPECT_EQ(retrieved, handle);

    // Remove the resource
    result = wasm_component_table_remove(table_, index);
    EXPECT_TRUE(result);
    EXPECT_EQ(table_->free_count, 1u);

    // Verify it's gone
    retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    EXPECT_EQ(retrieved, nullptr);
}

// Test free list functionality: add 3, remove 1, check free list, add 1 more
TEST_F(ResourceTableTest, Table_FreeListReuse)
{
    table_ = wasm_component_table_init(4, 50);
    ASSERT_NE(table_, nullptr);

    // Add 3 resources
    std::vector<uint32_t> indices;

    for (int i = 0; i < 3; i++) {
        WASMResourceHandle* handle = createTestResource();
        ASSERT_NE(handle, nullptr);

        uint32_t index;
        bool result = wasm_component_table_add(table_, handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, &index);
        EXPECT_TRUE(result);
        EXPECT_EQ(index, static_cast<uint32_t>(i + 1)); // Should be 1, 2, 3
        indices.push_back(index);
    }

    EXPECT_EQ(table_->next_index, 4u);
    EXPECT_EQ(table_->free_count, 0u);

    // Remove the middle resource (index 2)
    uint32_t removed_index = indices[1]; // index 2
    bool result = wasm_component_table_remove(table_, removed_index);
    EXPECT_TRUE(result);
    EXPECT_EQ(table_->free_count, 1u);

    // Check that the free list contains the removed index
    EXPECT_EQ(table_->free_list[0], removed_index);

    // Add a new resource - it should reuse the freed index
    WASMResourceHandle* new_handle = createTestResource();
    ASSERT_NE(new_handle, nullptr);

    uint32_t new_index;
    result = wasm_component_table_add(table_, new_handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, &new_index);
    EXPECT_TRUE(result);
    EXPECT_EQ(new_index, removed_index); // Should reuse the freed index
    EXPECT_EQ(table_->free_count, 0u); // Free list should be empty again

    // Verify the new resource is accessible
    WASMResourceHandle* retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, new_index, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    EXPECT_EQ(retrieved, new_handle);
}

// Test table resizing
TEST_F(ResourceTableTest, Table_Resize)
{
    table_ = wasm_component_table_init(2, 100); // 2 initial size, 100% growth
    ASSERT_NE(table_, nullptr);

    EXPECT_EQ(table_->array_size, 2u);

    std::vector<uint32_t> indices;

    // Add resources until we exceed initial capacity and trigger resize
    // index 0 is reserved, so it can initially fit 1 resource
    // Adding 2+ resources should trigger resize
    for (int i = 0; i < 4; i++) {
        WASMResourceHandle* handle = createTestResource();
        ASSERT_NE(handle, nullptr);

        uint32_t index;
        bool result = wasm_component_table_add(table_, handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, &index);
        EXPECT_TRUE(result);
        indices.push_back(index);

        // After adding the 2nd resource, table should have grown
        if (i == 1) {
            EXPECT_GT(table_->array_size, 2u); // Should have grown
        }
    }

    EXPECT_GT(table_->array_size, 2u); // Final size should be larger than initial
    EXPECT_EQ(table_->next_index, 5u); // Should have 4 resources at indices 1,2,3,4

    // Verify all resources are still accessible after resize
    for (size_t i = 0; i < indices.size(); i++) {
        WASMResourceHandle* retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, indices[i], WASM_TABLE_ELEM_RESOURCE_HANDLE);
        EXPECT_NE(retrieved, nullptr); // Just check it exists
    }

    // Test that free_list is also resized properly by removing and re-adding
    uint32_t removed_index = indices[1]; // Remove second resource
    bool result = wasm_component_table_remove(table_, removed_index);
    EXPECT_TRUE(result);
    EXPECT_EQ(table_->free_count, 1u);

    // Add a new resource - should reuse the freed slot
    WASMResourceHandle* new_handle = createTestResource();
    uint32_t new_index;
    result = wasm_component_table_add(table_, new_handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, &new_index);
    EXPECT_TRUE(result);
    EXPECT_EQ(new_index, removed_index); // Should reuse freed index
}

// Test edge cases and error conditions
TEST_F(ResourceTableTest, Table_EdgeCases)
{
    table_ = wasm_component_table_init(4, 50);
    ASSERT_NE(table_, nullptr);

    // Test adding NULL handle
    uint32_t index;
    bool result = wasm_component_table_add(table_, nullptr, WASM_TABLE_ELEM_RESOURCE_HANDLE, &index);
    EXPECT_FALSE(result);

    // Test adding with NULL out_index
    WASMResourceHandle* handle = createTestResource();
    ASSERT_NE(handle, nullptr);
    result = wasm_component_table_add(table_, handle, WASM_TABLE_ELEM_RESOURCE_HANDLE, nullptr);
    EXPECT_FALSE(result);
    wasm_destroy_resource_handle(handle); // Clean up since add failed

    // Test removing invalid indices
    result = wasm_component_table_remove(table_, 0); // Reserved index
    EXPECT_FALSE(result);

    result = wasm_component_table_remove(table_, 999); // Out of bounds
    EXPECT_FALSE(result);

    // Test getting from invalid indices
    WASMResourceHandle* retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, 0, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    EXPECT_EQ(retrieved, nullptr);

    retrieved = (WASMResourceHandle*)wasm_component_table_get(table_, 999, WASM_TABLE_ELEM_RESOURCE_HANDLE);
    EXPECT_EQ(retrieved, nullptr);

    // Test initialization with invalid parameters
    WASMComponentResourceTable* bad_table = wasm_component_table_init(0, 50);
    EXPECT_EQ(bad_table, nullptr);

    bad_table = wasm_component_table_init(4, 0);
    EXPECT_EQ(bad_table, nullptr);
}

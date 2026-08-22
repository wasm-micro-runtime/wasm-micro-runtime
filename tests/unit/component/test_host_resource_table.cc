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
#include "component-model/wasm_component_host_resource.h"
#include "wasm_runtime_common.h"
}

// Helper functions for testing ID encoding/decoding
#define HOST_RESOURCE_TYPE_SHIFT 24
#define HOST_RESOURCE_ID_MASK 0x00FFFFFF

static inline HostResourceType
test_get_type_from_id(int32_t id)
{
    return (HostResourceType)((id >> HOST_RESOURCE_TYPE_SHIFT) & 0xFF);
}

static inline uint32_t
test_get_counter_from_id(int32_t id)
{
    return (uint32_t)(id & HOST_RESOURCE_ID_MASK);
}

class HostResourceTableTest : public testing::Test
{
  public:
    HostResourceTableTest() : table_(nullptr) {}
    ~HostResourceTableTest() {}

    virtual void SetUp() {
        wasm_runtime_init();
        bool success = instantiate_host_resource_table();
        ASSERT_TRUE(success);
        table_ = get_global_host_resource_table();
        ASSERT_NE(table_, nullptr);
    }

    virtual void TearDown() {
        if (table_) {
            destroy_host_resource_table();
            table_ = nullptr;
        }
        wasm_runtime_destroy();
    }

  protected:
    HostResourceTable *table_;

    // Helper function to create a test host resource
    HostResource* createTestResource(HostResourceType type, void *data = nullptr) {
        HostResource *hr = (HostResource *)wasm_runtime_malloc(sizeof(HostResource));
        if (!hr) return nullptr;

        hr->type = type;
        hr->data = data;
        hr->dtor = NULL;
        return hr;
    }

    // Helper to create test data
    void* createTestData(int value) {
        int *data = (int *)wasm_runtime_malloc(sizeof(int));
        if (data) *data = value;
        return data;
    }
};

// Test table creation and destruction
TEST_F(HostResourceTableTest, Table_CreateDestroy)
{
    // Table is created in SetUp and destroyed in TearDown
    EXPECT_NE(table_, nullptr);
}

// Test adding a single resource and getting it back
TEST_F(HostResourceTableTest, Table_AddGetSingle)
{
    void *data = createTestData(42);
    HostResource *hr = createTestResource(WASI_P2_TCP_SOCKET, data);
    ASSERT_NE(hr, nullptr);

    // Add resource to table
    int32_t id = host_resource_table_add(table_, hr);
    EXPECT_GT(id, 0);

    // Get resource back
    HostResource *retrieved = host_resource_table_get(table_, id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved, hr);
    EXPECT_EQ(retrieved->type, WASI_P2_TCP_SOCKET);
    EXPECT_EQ(retrieved->data, data);
    EXPECT_EQ(*(int*)retrieved->data, 42);
}

// Test adding multiple resources
TEST_F(HostResourceTableTest, Table_AddMultiple)
{
    std::vector<int32_t> ids;

    // Add 5 TCP sockets
    for (int i = 0; i < 5; i++) {
        void *data = createTestData(100 + i);
        HostResource *hr = createTestResource(WASI_P2_TCP_SOCKET, data);
        ASSERT_NE(hr, nullptr);

        int32_t id = host_resource_table_add(table_, hr);
        EXPECT_NE(id, 0);
        ids.push_back(id);
    }

    // Verify all IDs are unique
    for (size_t i = 0; i < ids.size(); i++) {
        for (size_t j = i + 1; j < ids.size(); j++) {
            EXPECT_NE(ids[i], ids[j]);
        }
    }

    // Verify all resources can be retrieved
    for (size_t i = 0; i < ids.size(); i++) {
        HostResource *retrieved = host_resource_table_get(table_, ids[i]);
        ASSERT_NE(retrieved, nullptr);
        EXPECT_EQ(retrieved->type, WASI_P2_TCP_SOCKET);
        EXPECT_EQ(*(int*)retrieved->data, 100 + (int)i);
    }
}

// Test adding resources with different types
TEST_F(HostResourceTableTest, Table_DifferentTypes)
{
    void *data1 = createTestData(1);
    void *data2 = createTestData(2);
    void *data3 = createTestData(3);

    HostResource *tcp = createTestResource(WASI_P2_TCP_SOCKET, data1);
    HostResource *udp = createTestResource(WASI_P2_UDP_SOCKET, data2);
    HostResource *net = createTestResource(WASI_P2_NETWORK, data3);

    int32_t tcp_id = host_resource_table_add(table_, tcp);
    int32_t udp_id = host_resource_table_add(table_, udp);
    int32_t net_id = host_resource_table_add(table_, net);

    EXPECT_NE(tcp_id, 0);
    EXPECT_NE(udp_id, 0);
    EXPECT_NE(net_id, 0);

    // IDs should be different
    EXPECT_NE(tcp_id, udp_id);
    EXPECT_NE(tcp_id, net_id);
    EXPECT_NE(udp_id, net_id);

    // Verify types are encoded correctly (upper 8 bits)
    EXPECT_EQ(test_get_type_from_id(tcp_id), WASI_P2_TCP_SOCKET);
    EXPECT_EQ(test_get_type_from_id(udp_id), WASI_P2_UDP_SOCKET);
    EXPECT_EQ(test_get_type_from_id(net_id), WASI_P2_NETWORK);

    // Verify resources can be retrieved with correct types
    HostResource *retrieved_tcp = host_resource_table_get(table_, tcp_id);
    HostResource *retrieved_udp = host_resource_table_get(table_, udp_id);
    HostResource *retrieved_net = host_resource_table_get(table_, net_id);

    ASSERT_NE(retrieved_tcp, nullptr);
    ASSERT_NE(retrieved_udp, nullptr);
    ASSERT_NE(retrieved_net, nullptr);

    EXPECT_EQ(retrieved_tcp->type, WASI_P2_TCP_SOCKET);
    EXPECT_EQ(retrieved_udp->type, WASI_P2_UDP_SOCKET);
    EXPECT_EQ(retrieved_net->type, WASI_P2_NETWORK);
}

// Test deleting a resource
TEST_F(HostResourceTableTest, Table_Delete)
{
    void *data = createTestData(99);
    HostResource *hr = createTestResource(WASI_P2_TCP_SOCKET, data);

    int32_t id = host_resource_table_add(table_, hr);
    EXPECT_NE(id, 0);

    // Verify it exists
    HostResource *retrieved = host_resource_table_get(table_, id);
    ASSERT_NE(retrieved, nullptr);

    // Delete it
    int32_t result = host_resource_table_delete(table_, id);
    EXPECT_EQ(result, 1);

    // Verify it's gone
    retrieved = host_resource_table_get(table_, id);
    EXPECT_EQ(retrieved, nullptr);

    // Try to delete again - should fail
    result = host_resource_table_delete(table_, id);
    EXPECT_EQ(result, 0); // Not found
}

// Test get_next_id function
TEST_F(HostResourceTableTest, Table_GetNextId)
{
    // Get IDs for different types
    int32_t id1 = host_resource_table_get_next_id(WASI_P2_TCP_SOCKET);
    int32_t id2 = host_resource_table_get_next_id(WASI_P2_UDP_SOCKET);
    int32_t id3 = host_resource_table_get_next_id(WASI_P2_TCP_SOCKET);

    EXPECT_NE(id1, 0);
    EXPECT_NE(id2, 0);
    EXPECT_NE(id3, 0);

    // All IDs should be unique
    EXPECT_NE(id1, id2);
    EXPECT_NE(id1, id3);
    EXPECT_NE(id2, id3);

    // Verify types are encoded correctly
    EXPECT_EQ(test_get_type_from_id(id1), WASI_P2_TCP_SOCKET);
    EXPECT_EQ(test_get_type_from_id(id2), WASI_P2_UDP_SOCKET);
    EXPECT_EQ(test_get_type_from_id(id3), WASI_P2_TCP_SOCKET);

    // Counters should increment
    EXPECT_EQ(test_get_counter_from_id(id1), 1u);
    EXPECT_EQ(test_get_counter_from_id(id2), 2u);
    EXPECT_EQ(test_get_counter_from_id(id3), 3u);
}

// Test error conditions
TEST_F(HostResourceTableTest, Table_ErrorConditions)
{
    // Test add with NULL resource
    int32_t id = host_resource_table_add(table_, nullptr);
    EXPECT_EQ(id, 0); // Should fail

    // Test get with invalid ID
    HostResource *retrieved = host_resource_table_get(table_, 0);
    EXPECT_EQ(retrieved, nullptr);

    retrieved = host_resource_table_get(table_, 99999);
    EXPECT_EQ(retrieved, nullptr);

    // Test delete with invalid ID
    int32_t result = host_resource_table_delete(table_, 0);
    EXPECT_EQ(result, 0);

    result = host_resource_table_delete(table_, 99999);
    EXPECT_EQ(result, 0);

    // Test with NULL table
    retrieved = host_resource_table_get(nullptr, 1);
    EXPECT_EQ(retrieved, nullptr);

    result = host_resource_table_delete(nullptr, 1);
    EXPECT_EQ(result, 0);

    void *data = createTestData(1);
    HostResource *hr = createTestResource(WASI_P2_TCP_SOCKET, data);
    id = host_resource_table_add(nullptr, hr);
    EXPECT_EQ(id, 0);

    // Clean up since add failed
    wasm_runtime_free(data);
    wasm_runtime_free(hr);
}

// Test ID encoding/decoding helpers
TEST_F(HostResourceTableTest, IDEncoding)
{
    // Manually create IDs with known type/counter values
    int32_t id = (WASI_P2_TCP_SOCKET << 24) | 42;

    EXPECT_EQ(test_get_type_from_id(id), WASI_P2_TCP_SOCKET);
    EXPECT_EQ(test_get_counter_from_id(id), 42u);

    id = (WASI_P2_UDP_SOCKET << 24) | 12345;
    EXPECT_EQ(test_get_type_from_id(id), WASI_P2_UDP_SOCKET);
    EXPECT_EQ(test_get_counter_from_id(id), 12345u);
}

// Test that table properly cleans up on destroy
TEST_F(HostResourceTableTest, Table_CleanupOnDestroy)
{
    // Add several resources
    for (int i = 0; i < 10; i++) {
        void *data = createTestData(i);
        HostResource *hr = createTestResource(WASI_P2_TCP_SOCKET, data);
        int32_t id = host_resource_table_add(table_, hr);
        EXPECT_NE(id, 0);
    }
}

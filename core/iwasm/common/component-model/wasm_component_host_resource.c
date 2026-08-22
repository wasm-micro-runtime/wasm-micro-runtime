/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "component-model/wasm_component_host_resource.h"
#include "bh_platform.h"
#include "wasm_export.h"

HostResourceTable *g_host_resource_table = NULL;

// ID encoding:
// Upper 8 bits: HostResourceType
// Lower 24 bits: Counter value

#define HOST_RESOURCE_TYPE_SHIFT 24
#define HOST_RESOURCE_ID_MASK 0x00FFFFFF
#define HOST_RESOURCE_TABLE_SIZE 256
#define HOST_RESOURCE_INSERT_MAX_RETRIES 3

/* Convert an integer to a void* hash map key without triggering
   performance-no-int-to-ptr. The intermediate uintptr_t cast is
   required by the C standard. */
#define INT_TO_PTR(i) \
    ((void *)(uintptr_t)(i)) /* NOLINT(performance-no-int-to-ptr) */

// Counter starts at 1
static uint32_t next_id_counter = 1;

// Hash function for uint32_t keys
static uint32
hash_u32_hash_map(const void *key)
{
    return (uint32)(uintptr_t)key;
}

// Key equality function for uint32_t keys
static bool
u32_equal_hash_map(void *key1, void *key2)
{
    return (uint32_t)(uintptr_t)key1 == (uint32_t)(uintptr_t)key2;
}

// Key destroy function
static void
destroy_u32_key_hash_map(void *key)
{
    (void)key;
}

// Value destroy function for HostResource
static void
destroy_host_resource_hash_map(void *value)
{
    HostResource *hr = (HostResource *)value;
    if (hr) {
        if (hr->data) {
            if (hr->dtor) {
                hr->dtor(hr->data);
            }
            BH_FREE(hr->data);
        }
        BH_FREE(hr);
    }
}

bool
instantiate_host_resource_table()
{
    if (g_host_resource_table != NULL) {
        LOG_WARNING("Global host resource table already initialized\n");
        return false;
    }

    // Create HashMap: size, with lock for thread safety, hash_func, key_equal,
    // key_destroy, value_destroy
    g_host_resource_table = bh_hash_map_create(
        HOST_RESOURCE_TABLE_SIZE, true, hash_u32_hash_map, u32_equal_hash_map,
        destroy_u32_key_hash_map, destroy_host_resource_hash_map);

    if (!g_host_resource_table) {
        LOG_ERROR("Failed to create host resource table.\n");
        return false;
    }

    return true;
}

void
destroy_host_resource_table()
{
    if (!g_host_resource_table) {
        return;
    }

    // HashMap destroy will call destroy functions for all keys/values
    bh_hash_map_destroy(g_host_resource_table);
    g_host_resource_table = NULL;
}

HostResourceTable *
get_global_host_resource_table()
{
    return g_host_resource_table;
}

uint32_t
host_resource_table_get_next_id(HostResourceType type)
{
    uint32_t id = 0;

    // Check if counter would overflow the 24-bit space
    if (next_id_counter > HOST_RESOURCE_ID_MASK) {
        LOG_ERROR("Host resource ID counter overflow.\n");
        return 0;
    }

    // Encode ID (type << 24) | counter
    id = (type << HOST_RESOURCE_TYPE_SHIFT) | next_id_counter;

    next_id_counter++;

    return id;
}

HostResource *
host_resource_create(HostResourceType type, uint32_t data_size)
{
    HostResource *hr =
        (HostResource *)wasm_runtime_malloc(sizeof(HostResource));
    if (!hr) {
        LOG_ERROR("Failed to allocate HostResource\n");
        return NULL;
    }

    hr->data = wasm_runtime_malloc(data_size);
    if (!hr->data) {
        LOG_ERROR("Failed to allocate HostResource data\n");
        wasm_runtime_free(hr);
        return NULL;
    }

    hr->type = type;
    hr->dtor = NULL;

    return hr;
}

void
host_resource_set_dtor(HostResource *hr, host_resource_dtor_t dtor)
{
    if (hr) {
        hr->dtor = dtor;
    }
}

void
destroy_host_resource(HostResource *hr)
{
    if (hr) {
        if (hr->data) {
            if (hr->dtor) {
                hr->dtor(hr->data);
            }
            wasm_runtime_free(hr->data);
        }
        wasm_runtime_free(hr);
    }
}

uint32_t
host_resource_table_add(HostResourceTable *table, HostResource *hr)
{
    uint32_t id = 0;
    void *key = NULL;
    int retry = 0;

    if (!table || !hr) {
        LOG_ERROR(
            "Host resource table add failed: table or resource is NULL.\n");
        return 0;
    }

    // Try to insert with retry on collision
    for (retry = 0; retry < HOST_RESOURCE_INSERT_MAX_RETRIES; retry++) {
        id = host_resource_table_get_next_id(hr->type);
        if (id == 0) {
            continue;
        }

        key = INT_TO_PTR(id);

        if (bh_hash_map_insert(table, key, hr)) {
            return id;
        }
    }

    // Insert failed
    LOG_ERROR("Host resource table add failed: exhausted %d retries.\n",
              HOST_RESOURCE_INSERT_MAX_RETRIES);
    return 0;
}

HostResource *
host_resource_table_get(HostResourceTable *table, uint32_t id)
{
    void *key = NULL;

    if (!table) {
        LOG_ERROR("Host resource table get failed: table is NULL.\n");
        return NULL;
    }

    if (id == 0) {
        LOG_ERROR("Host resource table get failed: invalid ID 0.\n");
        return NULL;
    }

    key = INT_TO_PTR(id);

    return (HostResource *)bh_hash_map_find(table, key);
}

uint32_t
host_resource_table_delete(HostResourceTable *table, uint32_t id)
{
    void *key = NULL;
    void *old_key = NULL;
    void *old_value = NULL;

    if (!table) {
        LOG_ERROR("Host resource table delete failed: table is NULL.\n");
        return 0;
    }

    if (id == 0) {
        LOG_ERROR("Host resource table delete failed: invalid ID 0.\n");
        return 0;
    }

    key = INT_TO_PTR(id);

    if (!bh_hash_map_remove(table, key, &old_key, &old_value)) {
        return 0;
    }

    if (old_value) {
        destroy_host_resource_hash_map(old_value);
    }

    return 1;
}

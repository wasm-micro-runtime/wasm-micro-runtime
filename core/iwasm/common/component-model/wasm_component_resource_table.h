/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_RESOURCE_TABLE_H
#define WASM_COMPONENT_RESOURCE_TABLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WASM_COMPONENT_TABLE_MAX_LENGTH ((1u << 28) - 1)

typedef struct WASMResourceHandle WASMResourceHandle;

typedef enum WASMTableElementType {
    WASM_TABLE_ELEM_RESOURCE_HANDLE = 1,
    WASM_TABLE_ELEM_ERROR_CONTEXT = 2,
} WASMTableElementType;

typedef struct WASMTableElement {
    WASMTableElementType type;
    void *ptr;
} WASMTableElement;

typedef struct WASMComponentResourceTable {
    WASMTableElement **array;
    uint32_t *free_list;
    uint32_t array_size;
    uint32_t free_count;
    uint32_t next_index; // Should start at 1
    uint32_t resize_percent;
} WASMComponentResourceTable;

bool
wasm_component_table_add(WASMComponentResourceTable *table, void *ptr,
                         WASMTableElementType type, uint32_t *out);
bool
wasm_component_table_remove(WASMComponentResourceTable *table, uint32_t index);

void *
wasm_component_table_get(WASMComponentResourceTable *table, uint32_t index,
                         WASMTableElementType expected_type);

WASMComponentResourceTable *
wasm_component_table_init(uint32_t initial_size, uint32_t resize_percent);
void
wasm_component_table_destroy(WASMComponentResourceTable *table);

#ifdef __cplusplus
}
#endif

#endif

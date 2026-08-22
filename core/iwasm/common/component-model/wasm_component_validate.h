/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_VALIDATE_H
#define WASM_COMPONENT_VALIDATE_H

#include "bh_hashmap.h"
#include "wasm_component.h"

typedef struct WASMComponentValidationContext {
    // Index spaces
    uint32_t type_count;
    uint32_t func_count;
    uint32_t instance_count;
    uint32_t component_count;
    uint32_t value_count;
    uint32_t core_type_count;
    uint32_t core_module_count;
    uint32_t core_instance_count;
    uint32_t core_table_count;
    uint32_t core_memory_count; // needed for canon memory opts
    uint32_t core_global_count;
    uint32_t core_func_count; // needed for canon realloc/post-return checks

    struct WASMComponentValidationContext *parent;

    // name uniqueness
    HashMap *import_names;
    HashMap *export_names;

    // Flat type lookup array: types[i] is the WASMComponentTypes* for type
    // index i NULL for types introduced via import or alias type_is_local[i] is
    // true if the type was defined in a local type section
    WASMComponentTypes **types;
    bool *type_is_local;
    uint32_t types_capacity;

    // Consumption tracking: value_consumed[i] is true once value i has been
    // consumed exactly once
    bool *value_consumed;
    uint32_t value_consumed_capacity;

    // Func-to-type tracking: func_type_indexes[i] is the type index for
    // function i UINT32_MAX for functions whose type is unknown (e.g. aliased)
    uint32_t *func_type_indexes;
    uint32_t func_type_indexes_capacity;

    // Resource type names: tracks import/export names that introduce resource
    // types Used for [static] annotation validation
    HashMap *resource_type_names;
} WASMComponentValidationContext;

bool
wasm_component_validate(WASMComponent *comp,
                        WASMComponentValidationContext *parent, char *error_buf,
                        uint32_t error_buf_size);

#endif

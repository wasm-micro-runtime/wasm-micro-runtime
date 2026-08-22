/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include "wasm_component_runtime.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wasm_loader_common.h"
#include "wasm_runtime_common.h"
#include "wasm_export.h"
#include <stdio.h>

bool
parse_single_alias(const uint8_t **payload, const uint8_t *end,
                   WASMComponentAliasDefinition *out, char *error_buf,
                   uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    out->sort = wasm_runtime_malloc(sizeof(WASMComponentSort));
    if (!out->sort) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for alias sort");
        return false;
    }

    // Parse the sort using the reusable parse_sort method
    if (!parse_sort(&p, end, out->sort, error_buf, error_buf_size, false)) {
        return false;
    }

    // Read tag
    uint8_t tag = *p++;

    // Parse alias target using switch
    switch (tag) {
        case WASM_COMP_ALIAS_TARGET_EXPORT:
        {
            uint64_t instance_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &instance_idx,
                          error_buf, error_buf_size)) {
                return false;
            }
            WASMComponentCoreName *name = NULL;
            if (!parse_core_name(&p, end, &name, error_buf, error_buf_size)) {
                return false;
            }
            out->alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT;
            out->target.exported.instance_idx = (uint32_t)instance_idx;
            out->target.exported.name = name;
            break;
        }
        case WASM_COMP_ALIAS_TARGET_CORE_EXPORT:
        {
            uint64_t core_instance_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &core_instance_idx,
                          error_buf, error_buf_size)) {
                return false;
            }
            WASMComponentCoreName *core_name = NULL;
            if (!parse_core_name(&p, end, &core_name, error_buf,
                                 error_buf_size)) {
                return false;
            }
            out->alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT;
            out->target.core_exported.instance_idx =
                (uint32_t)core_instance_idx;
            out->target.core_exported.name = core_name;
            break;
        }
        case WASM_COMP_ALIAS_TARGET_OUTER:
        {
            uint64_t outer_ct = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &outer_ct, error_buf,
                          error_buf_size)) {
                return false;
            }
            uint64_t outer_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &outer_idx, error_buf,
                          error_buf_size)) {
                return false;
            }
            out->alias_target_type = WASM_COMP_ALIAS_TARGET_OUTER;
            out->target.outer.ct = (uint32_t)outer_ct;
            out->target.outer.idx = (uint32_t)outer_idx;

            bool valid_outer_sort =
                (out->sort->sort == WASM_COMP_SORT_TYPE)
                || (out->sort->sort == WASM_COMP_SORT_COMPONENT)
                || (out->sort->sort == WASM_COMP_SORT_CORE_SORT
                    && out->sort->core_sort == WASM_COMP_CORE_SORT_MODULE);
            if (!valid_outer_sort) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Outer alias sort must be type, component, or core module");
                return false;
            }
            break;
        }
        default:
            snprintf(error_buf, error_buf_size,
                     "Unknown alias target type: 0x%02X", tag);
            return false;
    }

    *payload = p;
    return true;
}

// Section 6: alias section
bool
wasm_component_parse_alias_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentAliasSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len)
{
    if (!payload || !*payload || payload_len == 0 || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        if (consumed_len)
            *consumed_len = 0;
        return false;
    }

    const uint8_t *p = *payload;
    const uint8_t *end = *payload + payload_len;
    uint32_t alias_count = 0;

    // Read alias count
    uint64_t alias_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &alias_count_leb, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    alias_count = (uint32_t)alias_count_leb;

    out->count = alias_count;
    if (alias_count > 0) {
        out->aliases = wasm_runtime_malloc(sizeof(WASMComponentAliasDefinition)
                                           * alias_count);
        if (!out->aliases) {
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }
        // Zero-initialize the aliases array
        memset(out->aliases, 0,
               sizeof(WASMComponentAliasDefinition) * alias_count);

        for (uint32_t i = 0; i < alias_count; ++i) {
            // Allocate memory for the sort field
            if (!parse_single_alias(&p, end, &out->aliases[i], error_buf,
                                    error_buf_size)) {
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse alias %d", i);
                return false;
            }
        }
    }

    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);

    // If binaries use alias ids, this parser will need to be extended.
    return true;
}

// Individual section free functions
void
wasm_component_free_alias_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.alias_section)
        return;

    WASMComponentAliasSection *alias_sec = section->parsed.alias_section;
    if (alias_sec->aliases) {
        for (uint32_t j = 0; j < alias_sec->count; ++j) {
            WASMComponentAliasDefinition *alias = &alias_sec->aliases[j];

            // Free sort
            if (alias->sort) {
                wasm_runtime_free(alias->sort);
                alias->sort = NULL;
            }

            // Free target-specific data
            switch (alias->alias_target_type) {
                case WASM_COMP_ALIAS_TARGET_EXPORT:
                    if (alias->target.exported.name) {
                        free_core_name(alias->target.exported.name);
                        wasm_runtime_free(alias->target.exported.name);
                        alias->target.exported.name = NULL;
                    }
                    break;
                case WASM_COMP_ALIAS_TARGET_CORE_EXPORT:
                    if (alias->target.core_exported.name) {
                        free_core_name(alias->target.core_exported.name);
                        wasm_runtime_free(alias->target.core_exported.name);
                        alias->target.core_exported.name = NULL;
                    }
                    break;
                case WASM_COMP_ALIAS_TARGET_OUTER:
                    // No dynamic allocations for outer aliases
                    break;
            }
        }
        wasm_runtime_free(alias_sec->aliases);
        alias_sec->aliases = NULL;
    }
    wasm_runtime_free(alias_sec);
    section->parsed.alias_section = NULL;
}

/// @brief Retrieve the aliased sort from the target instance
/// @param target Component instance / Core instance / Outer component instance
/// @return Aliased sort / core sort reference passed as void*
void *
get_alias(WASMComponentAliasTarget *target, char *error_buf,
          uint32 error_buf_size)
{

    if (!target) {
        set_error_buf_ex(error_buf, error_buf_size, "Invalid alias target\n");
        return NULL;
    }
    uint32 idx = 0;
    // Outer aliases
    if (target->type == WASM_COMP_ALIAS_TARGET_OUTER) {
        WASMComponentInstance *inst = target->target.instance;
        idx = target->ref.idx;
        switch (target->sort->sort) {
            case WASM_COMP_SORT_CORE_SORT:
                switch (target->sort->core_sort) {
                    case WASM_COMP_CORE_SORT_FUNC:
                        if (inst->core_functions_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core function index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_functions[idx];
                    case WASM_COMP_CORE_SORT_TABLE:
                        if (inst->core_tables_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core tables index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_tables[idx];
                    case WASM_COMP_CORE_SORT_MEMORY:
                        if (inst->core_memories_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core memories index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_memories[idx];
                    case WASM_COMP_CORE_SORT_GLOBAL:
                        if (inst->core_globals_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core globals index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_globals[idx];
                    case WASM_COMP_CORE_SORT_TYPE:
                        if (inst->core_types_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core types index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_types[idx];
                    case WASM_COMP_CORE_SORT_MODULE:
                        if (inst->core_modules_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core modules index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_modules[idx];
                    case WASM_COMP_CORE_SORT_INSTANCE:
                        if (inst->core_module_instances_count <= idx) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core instances index exceeded\n");
                            return NULL;
                        }
                        return (void *)inst->core_module_instances[idx];
                    default:
                        break;
                }
                break;
            case WASM_COMP_SORT_FUNC:
                if (inst->functions_count <= idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Functions index exceeded\n");
                    return NULL;
                }
                return (void *)inst->functions[idx];
                break;
            case WASM_COMP_SORT_VALUE:
                if (inst->values_count <= idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Values index exceeded\n");
                    return NULL;
                }
                break;
            case WASM_COMP_SORT_TYPE:
                if (inst->types_count <= idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Types index exceeded\n");
                    return NULL;
                }
                return (void *)inst->types[idx];
            case WASM_COMP_SORT_COMPONENT:
                if (inst->components_count <= idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Components index exceeded\n");
                    return NULL;
                }
                return (void *)inst->components[idx];
            case WASM_COMP_SORT_INSTANCE:
                if (inst->component_instances_count <= idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Instances index exceeded\n");
                    return NULL;
                }
                return (void *)inst->component_instances[idx];
            default:
                break;
        }
    }
    else if (target->type == WASM_COMP_ALIAS_TARGET_CORE_EXPORT) {
        WASMModuleInstance *core_inst = target->target.core_instance;
        if (!target->target.core_instance) {
            return NULL;
        }
        if (target->sort->sort) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "ERROR: Component level sort not supported for "
                             "core export alias\n");
            return NULL;
        }
        if (target->sort->core_sort > 3) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "ERROR: Unsupported sort index %d for core alias (Core modules "
                "only support function, table, memory, or global exports)\n",
                target->sort->core_sort);
            return NULL;
        }
        // Perform name and type check for the respective export
        char *name = target->ref.name->name;
        const char *target_name = NULL;
        LOG_DEBUG("  Alias core export name : %s", name);
        // Retrieve core export
        switch (target->sort->core_sort) {
            case WASM_COMP_CORE_SORT_FUNC:
                for (idx = 0; idx < core_inst->export_func_count; idx++) {
                    target_name = core_inst->export_functions[idx].name;
                    if (!strcmp(name, target_name)) {
                        return (void *)core_inst->export_functions[idx]
                            .function;
                    }
                };
                LOG_WARNING("Export func not found in core exports\n");
                return NULL;

            case WASM_COMP_CORE_SORT_TABLE:
                for (idx = 0; idx < core_inst->export_table_count; idx++) {
                    target_name = core_inst->export_tables[idx].name;
                    if (!strcmp(name, target_name)) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "Found table \n");
                        return (void *)core_inst->export_tables[idx].table;
                    }
                };
                LOG_WARNING("Export table not found in core exports\n");
                return NULL;
            case WASM_COMP_CORE_SORT_MEMORY:
                for (idx = 0; idx < core_inst->export_memory_count; idx++) {
                    target_name = core_inst->export_memories[idx].name;
                    if (!strcmp(name, target_name)) {
                        return (void *)core_inst->export_memories[idx].memory;
                    }
                };
                LOG_WARNING("Export memory not found in core exports\n");
                return NULL;
            case WASM_COMP_CORE_SORT_GLOBAL:
                for (idx = 0; idx < core_inst->export_global_count; idx++) {
                    target_name = core_inst->export_globals[idx].name;
                    if (!strcmp(name, target_name)) {
                        return (void *)core_inst->export_globals[idx].global;
                    }
                };
                LOG_WARNING("Export global not found in core exports\n");
                return NULL;
            default:
                break;
        }
    }
    else if (target->type == WASM_COMP_ALIAS_TARGET_EXPORT) {
        WASMComponentInstance *inst = target->target.instance;
        char *name = target->ref.name->name;
        const char *target_name = NULL;
        bool is_found = false;
        WASMComponentExportInstance *export = NULL;
        LOG_DEBUG("  Alias export name : %s", name);
        for (idx = 0; idx < inst->exports_count; idx++) {
            export = &inst->exports[idx];
            if (export->export_name->tag == WASM_COMP_IMPORTNAME_SIMPLE) {
                target_name = export->export_name->exported.simple.name->name;
            }
            else if (export->export_name->tag
                     == WASM_COMP_IMPORTNAME_VERSIONED) {
                target_name =
                    export->export_name->exported.versioned.name->name;
            }
            else {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "ERROR: Component Export tag %d not supported\n",
                    export->export_name->tag);
                return NULL;
            }
            if (!strcmp(name, target_name)) {
                if ((target->sort->sort
                     && (target->sort->sort != export->type))) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "ERROR: Type of desired export \"%s\" doesn't match",
                        name);
                    return NULL;
                }
                is_found = true;
                break;
            }
        }
        if (!is_found) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Export NOT found in targte exports\n");
            return NULL;
        }
        // Export was found
        switch (target->sort->sort) {
            // TODO: type checking for the export
            case WASM_COMP_SORT_CORE_SORT:
                if (target->sort->core_sort != 0x11) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Component export disallows core "
                                     "sorts other than core module\n");
                    return NULL;
                }
                return (void *)export->exp.core_module;
            case WASM_COMP_SORT_FUNC:
                return (void *)export->exp.function;
            case WASM_COMP_SORT_VALUE:
                // TODO: not in scope for now
                return NULL;
            case WASM_COMP_SORT_TYPE:
                return (void *)export->exp.type;
            case WASM_COMP_SORT_COMPONENT:
                return (void *)export->exp.component;
            case WASM_COMP_SORT_INSTANCE:
                return (void *)export->exp.instance;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Invalid sort %d\n",
                                 target->sort->sort);
                return NULL;
        }
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: Alias type not recognised");
        return NULL;
    }
    return NULL;
}

bool
wasm_resolve_alias(WASMComponentAliasSection *alias_section,
                   WASMComponentInstance *comp_instance, char *error_buf,
                   uint32 error_buf_size)
{
    uint32 idx = 0;
    WASMComponentAliasDefinition *alias = NULL;
    for (idx = 0; idx < alias_section->count; idx++) {
        alias = &alias_section->aliases[idx];
        WASMComponentAliasTarget target_instance;
        target_instance.type = alias->alias_target_type;
        target_instance.sort = alias->sort;

        if (alias->alias_target_type == WASM_COMP_ALIAS_TARGET_OUTER) {
            target_instance.target.instance = comp_instance;
            for (uint32 ct = 0; ct < alias->target.outer.ct; ct++) {
                if (!target_instance.target.instance->parent) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "ERROR: Outer alias level %d not reachable, current "
                        "instance has %d parent levels\n",
                        alias->target.outer.ct, ct);
                    return false;
                }
                target_instance.target.instance =
                    target_instance.target.instance->parent;
                target_instance.ref.idx = alias->target.outer.idx;
            }
        }
        else if (alias->alias_target_type == WASM_COMP_ALIAS_TARGET_EXPORT) {
            if (alias->target.exported.instance_idx
                >= comp_instance->component_instances_count) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Component instances index exceeded");
                return false;
            }
            target_instance.target.instance =
                comp_instance
                    ->component_instances[alias->target.exported.instance_idx];
            target_instance.ref.name = alias->target.exported.name;
        }
        else if (alias->alias_target_type
                 == WASM_COMP_ALIAS_TARGET_CORE_EXPORT) {
            if (alias->target.core_exported.instance_idx
                > comp_instance->core_module_instances_count) {
                return false;
            }
            target_instance.target.core_instance =
                comp_instance->core_module_instances[alias->target.core_exported
                                                         .instance_idx];
            target_instance.ref.name = alias->target.core_exported.name;
        }
        else {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Alias target type %d not recognised",
                             alias->alias_target_type);
        }
        void *alias_ptr =
            get_alias(&target_instance, error_buf, error_buf_size);
        if (!alias_ptr) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "ERROR: Failed to retirieve alias\n");
            return false;
        }
        switch (alias->sort->sort) {
            case WASM_COMP_SORT_CORE_SORT:
                switch (alias->sort->core_sort) {
                    case WASM_COMP_CORE_SORT_FUNC:
                        LOG_DEBUG("Added aliased core function");
                        comp_instance->core_functions
                            [comp_instance->core_functions_count] =
                            (WASMFunctionInstance *)alias_ptr;
                        comp_instance
                            ->core_functions[comp_instance
                                                 ->core_functions_count]
                            ->module_instance =
                            target_instance.target.core_instance;
                        comp_instance->core_functions_count++;
                        break;
                    case WASM_COMP_CORE_SORT_TABLE:
                        LOG_DEBUG("Added aliased core table");
                        comp_instance
                            ->core_tables[comp_instance->core_tables_count] =
                            (WASMTableInstance *)alias_ptr;
                        comp_instance->core_tables_count++;
                        break;
                    case WASM_COMP_CORE_SORT_MEMORY:
                        LOG_DEBUG("Added aliased core memory");
                        comp_instance->core_memories
                            [comp_instance->core_memories_count] =
                            (WASMMemoryInstance *)alias_ptr;
                        comp_instance->core_memories_count++;
                        break;
                    case WASM_COMP_CORE_SORT_GLOBAL:
                        LOG_DEBUG("Added aliased core global");
                        comp_instance
                            ->core_globals[comp_instance->core_globals_count] =
                            (WASMGlobalInstance *)alias_ptr;
                        comp_instance->core_globals_count++;
                        break;
                    case WASM_COMP_CORE_SORT_TYPE:
                        LOG_DEBUG("Added aliased core type");
                        comp_instance
                            ->core_types[comp_instance->core_types_count] =
                            (WASMType *)alias_ptr;
                        comp_instance->core_types_count++;
                        break;
                    case WASM_COMP_CORE_SORT_MODULE:
                        LOG_DEBUG("Added aliased core module");
                        comp_instance
                            ->core_modules[comp_instance->core_modules_count] =
                            (WASMModule *)alias_ptr;
                        comp_instance->core_modules_count++;
                        break;
                    case WASM_COMP_CORE_SORT_INSTANCE:
                        LOG_DEBUG("Added aliased core instance");
                        comp_instance->core_module_instances
                            [comp_instance->core_module_instances_count] =
                            (WASMModuleInstance *)alias_ptr;
                        comp_instance->core_module_instances_count++;
                        break;
                    default:
                        break;
                }
                break;
            case WASM_COMP_SORT_FUNC:
                comp_instance->functions[comp_instance->functions_count] =
                    (WASMComponentFunctionInstance *)alias_ptr;
                comp_instance->functions_count++;
                break;
            case WASM_COMP_SORT_VALUE:
                LOG_DEBUG("Added aliased value");
                comp_instance->values[comp_instance->values_count] =
                    (WASMComponentValue *)alias_ptr;
                comp_instance->values_count++;
                break;
            case WASM_COMP_SORT_TYPE:
                LOG_DEBUG("Added aliased type");
                comp_instance->types[comp_instance->types_count] =
                    (WASMComponentTypeInstance *)alias_ptr;
                comp_instance->types_count++;
                break;
            case WASM_COMP_SORT_COMPONENT:
                LOG_DEBUG("Added aliased component");
                comp_instance->components[comp_instance->components_count] =
                    (WASMComponent *)alias_ptr;
                comp_instance->components_count++;
                break;
            case WASM_COMP_SORT_INSTANCE:
                LOG_DEBUG("Added aliased instance");
                comp_instance->component_instances
                    [comp_instance->component_instances_count] =
                    (WASMComponentInstance *)alias_ptr;
                comp_instance->component_instances_count++;
                break;
            default:
                break;
        }
    }
    return true;
}

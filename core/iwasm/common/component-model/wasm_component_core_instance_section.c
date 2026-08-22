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
#include "wasm_component_canonical.h"
#include "wasm_memory.h"
#include "mem_alloc.h"
#include <stdio.h>

// Section 2: core:instance ::= ie:<core:instanceexpr> => (instance ie)
// core:instanceexpr ::= 0x00 m:<moduleidx> arg*:vec(<core:instantiatearg>) =>
// (instantiate m arg*) core:instantiatearg ::= n:<core:name> 0x12
// i:<instanceidx> => (with n (instance i))
bool
wasm_component_parse_core_instance_section(const uint8_t **payload,
                                           uint32_t payload_len,
                                           WASMComponentCoreInstSection *out,
                                           char *error_buf,
                                           uint32_t error_buf_size,
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

    uint64_t instance_count = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &instance_count, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }
    out->count = (uint32_t)instance_count;

    if (instance_count > 0) {
        out->instances =
            wasm_runtime_malloc(sizeof(WASMComponentCoreInst) * instance_count);
        if (!out->instances) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for core instances");
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }

        // Initialize all instances to zero to avoid garbage data
        memset(out->instances, 0,
               sizeof(WASMComponentCoreInst) * instance_count);

        for (uint32_t i = 0; i < instance_count; ++i) {
            // Check bounds before reading tag
            if (p >= end) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Buffer overflow when reading instance tag");
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }

            uint8_t tag = *p++;
            out->instances[i].instance_expression_tag = tag;
            switch (tag) {
                case WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS:
                {
                    // 0x00 m:<moduleidx> arg*:vec(<core:instantiatearg>)
                    uint64_t module_idx = 0;
                    if (!read_leb((uint8_t **)&p, end, 32, false, &module_idx,
                                  error_buf, error_buf_size)) {
                        if (consumed_len)
                            *consumed_len = (uint32_t)(p - *payload);
                        return false;
                    }
                    out->instances[i].expression.with_args.idx =
                        (uint32_t)module_idx;

                    uint64_t arg_len = 0;
                    if (!read_leb((uint8_t **)&p, end, 32, false, &arg_len,
                                  error_buf, error_buf_size)) {
                        if (consumed_len)
                            *consumed_len = (uint32_t)(p - *payload);
                        return false;
                    }
                    out->instances[i].expression.with_args.arg_len =
                        (uint32_t)arg_len;

                    if (arg_len > 0) {
                        out->instances[i].expression.with_args.args =
                            wasm_runtime_malloc(sizeof(WASMComponentInstArg)
                                                * arg_len);
                        if (!out->instances[i].expression.with_args.args) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "Failed to allocate memory for "
                                             "core instantiate args");
                            if (consumed_len)
                                *consumed_len = (uint32_t)(p - *payload);
                            return false;
                        }

                        // Initialize args to zero
                        memset(out->instances[i].expression.with_args.args, 0,
                               sizeof(WASMComponentInstArg) * arg_len);

                        for (uint32_t j = 0; j < arg_len; ++j) {
                            // core:instantiatearg ::= n:<core:name> 0x12
                            // i:<instanceidx> Parse core:name (LEB128 length +
                            // UTF-8 bytes)

                            // Check bounds before parsing name
                            if (p >= end) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Buffer overflow when parsing core name");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            WASMComponentCoreName *core_name = NULL;
                            if (!parse_core_name(&p, end, &core_name, error_buf,
                                                 error_buf_size)) {
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // Store the name in the instantiate arg structure
                            out->instances[i]
                                .expression.with_args.args[j]
                                .name = core_name;

                            // Check bounds before reading 0x12
                            if (p >= end) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Buffer overflow when reading 0x12 flag");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // Verify 0x12 for core:instantiatearg
                            if (*p++ != 0x12) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Failed to read 0x12 flag identifier for "
                                    "core instantiatearg field");
                                free_core_name(core_name);
                                wasm_runtime_free(core_name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // i:<instanceidx> - this is a core instance index
                            uint64_t instance_idx = 0;
                            if (!read_leb((uint8_t **)&p, end, 32, false,
                                          &instance_idx, error_buf,
                                          error_buf_size)) {
                                free_core_name(core_name);
                                wasm_runtime_free(core_name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }
                            out->instances[i]
                                .expression.with_args.args[j]
                                .idx.instance_idx = (uint32_t)instance_idx;
                        }
                    }
                    else {
                        out->instances[i].expression.with_args.args = NULL;
                    }
                    break;
                }
                case WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS:
                {
                    // 0x01 e*:vec(<core:inlineexport>) => e*
                    uint64_t inline_expr_len = 0;
                    if (!read_leb((uint8_t **)&p, end, 32, false,
                                  &inline_expr_len, error_buf,
                                  error_buf_size)) {
                        if (consumed_len)
                            *consumed_len = (uint32_t)(p - *payload);
                        return false;
                    }

                    out->instances[i].expression.without_args.inline_expr_len =
                        (uint32_t)inline_expr_len;

                    if (inline_expr_len > 0) {
                        out->instances[i].expression.without_args.inline_expr =
                            wasm_runtime_malloc(
                                sizeof(WASMComponentInlineExport)
                                * inline_expr_len);
                        if (!out->instances[i]
                                 .expression.without_args.inline_expr) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "Failed to allocate memory for "
                                             "core inline exports");
                            if (consumed_len)
                                *consumed_len = (uint32_t)(p - *payload);
                            return false;
                        }

                        // Initialize inline exports to zero
                        memset(out->instances[i]
                                   .expression.without_args.inline_expr,
                               0,
                               sizeof(WASMComponentInlineExport)
                                   * inline_expr_len);

                        for (uint32_t j = 0; j < inline_expr_len; j++) {
                            // core:inlineexport ::= n:<core:name>
                            // si:<core:sortidx>
                            WASMComponentCoreName *name = NULL;

                            // Debug: Check if we're about to go out of bounds
                            if (p >= end) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Buffer overflow in inline "
                                                 "exports parsing");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // Parse core:name using the existing
                            // parse_core_name function
                            bool name_parse_success = parse_core_name(
                                &p, end, &name, error_buf, error_buf_size);
                            if (!name_parse_success) {
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            out->instances[i]
                                .expression.without_args.inline_expr[j]
                                .name = name;

                            // Check bounds before parsing sort index
                            if (p >= end) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Buffer overflow when parsing "
                                                 "core sort idx");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // Parse core:sortidx (must use is_core=true for
                            // core instances)
                            WASMComponentSortIdx *sort_idx =
                                wasm_runtime_malloc(
                                    sizeof(WASMComponentSortIdx));
                            if (!sort_idx) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Failed to allocate memory "
                                                 "for core sort idx");
                                free_core_name(name);
                                wasm_runtime_free(name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }
                            // Zero-initialize sort_idx
                            memset(sort_idx, 0, sizeof(WASMComponentSortIdx));

                            bool status =
                                parse_sort_idx(&p, end, sort_idx, error_buf,
                                               error_buf_size, true);
                            if (!status) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Failed to parse core sort idx");
                                wasm_runtime_free(sort_idx);
                                free_core_name(name);
                                wasm_runtime_free(name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            out->instances[i]
                                .expression.without_args.inline_expr[j]
                                .sort_idx = sort_idx;
                        }
                    }
                    else {
                        out->instances[i].expression.without_args.inline_expr =
                            NULL;
                    }

                    break;
                }
                default:
                {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Unknown core instance expression tag: 0x%02X", tag);
                    if (consumed_len)
                        *consumed_len = (uint32_t)(p - *payload);
                    return false;
                }
            }
        }
    }
    if (consumed_len)
        *consumed_len = payload_len;
    return true;
}

// Individual section free functions
void
wasm_component_free_core_instance_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.core_instance_section)
        return;

    WASMComponentCoreInstSection *core_instance_sec =
        section->parsed.core_instance_section;
    if (core_instance_sec->instances) {
        for (uint32_t j = 0; j < core_instance_sec->count; ++j) {
            WASMComponentCoreInst *instance = &core_instance_sec->instances[j];

            switch (instance->instance_expression_tag) {
                case WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS:
                    if (instance->expression.with_args.args) {
                        for (uint32_t k = 0;
                             k < instance->expression.with_args.arg_len; ++k) {
                            WASMComponentInstArg *arg =
                                &instance->expression.with_args.args[k];

                            // Free core name
                            if (arg->name) {
                                free_core_name(arg->name);
                                wasm_runtime_free(arg->name);
                                arg->name = NULL;
                            }
                        }
                        wasm_runtime_free(instance->expression.with_args.args);
                        instance->expression.with_args.args = NULL;
                    }
                    break;

                case WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS:
                    if (instance->expression.without_args.inline_expr) {
                        for (uint32_t k = 0;
                             k < instance->expression.without_args
                                     .inline_expr_len;
                             ++k) {
                            WASMComponentInlineExport *inline_export =
                                &instance->expression.without_args
                                     .inline_expr[k];

                            // Free core export name
                            if (inline_export->name) {
                                free_core_name(inline_export->name);
                                wasm_runtime_free(inline_export->name);
                                inline_export->name = NULL;
                            }

                            // Free core sort index
                            if (inline_export->sort_idx) {
                                if (inline_export->sort_idx->sort) {
                                    wasm_runtime_free(
                                        inline_export->sort_idx->sort);
                                    inline_export->sort_idx->sort = NULL;
                                }
                                wasm_runtime_free(inline_export->sort_idx);
                                inline_export->sort_idx = NULL;
                            }
                        }
                        wasm_runtime_free(
                            instance->expression.without_args.inline_expr);
                        instance->expression.without_args.inline_expr = NULL;
                    }
                    break;
            }
        }
        wasm_runtime_free(core_instance_sec->instances);
        core_instance_sec->instances = NULL;
    }
    wasm_runtime_free(core_instance_sec);
    section->parsed.core_instance_section = NULL;
}

bool
wasm_create_core_inst_from_expression(WASMComponentCoreInst *core_inst,
                                      WASMComponentInstance *comp_instance,
                                      char *error_buf, uint32 error_buf_size)
{
    uint32 total_size = 0, idx = 0;
    WASMComponentInlineExport *inline_expr = NULL;
    if (!core_inst) {
        set_error_buf_ex(error_buf, error_buf_size, "Invalid core instance\n");
        return false;
    }
    WASMInstExpr *instance_expression = &core_inst->expression;
    uint32 func_count = 0, table_count = 0, mem_count = 0, global_count = 0;

    for (idx = 0; idx < instance_expression->without_args.inline_expr_len;
         idx++) {
        inline_expr = &instance_expression->without_args.inline_expr[idx];
        switch (inline_expr->sort_idx->sort->core_sort) {
            case WASM_COMP_CORE_SORT_FUNC:
                func_count++;
                break;
            case WASM_COMP_CORE_SORT_TABLE:
                table_count++;
                break;
            case WASM_COMP_CORE_SORT_MEMORY:
                mem_count++;
                break;
            case WASM_COMP_CORE_SORT_GLOBAL:
                global_count++;
                break;
            default:
                break;
        }
    }

    total_size = sizeof(WASMModuleInstance)
                 + sizeof(WASMExportFuncInstance) * func_count
                 + sizeof(WASMExportGlobInstance) * global_count
                 + sizeof(WASMExportTabInstance) * table_count
                 + sizeof(WASMExportMemInstance) * mem_count;
    WASMModuleInstance *new_inst =
        (WASMModuleInstance *)wasm_runtime_malloc(total_size);
    memset(new_inst, 0, total_size);

    new_inst->export_functions =
        (WASMExportFuncInstance *)((uint8_t *)new_inst
                                   + sizeof(WASMModuleInstance));
    new_inst->export_globals =
        (WASMExportGlobInstance *)((uint8_t *)new_inst->export_functions
                                   + (sizeof(WASMExportFuncInstance)
                                      * func_count));
    new_inst->export_tables =
        (WASMExportTabInstance *)((uint8_t *)new_inst->export_globals
                                  + (sizeof(WASMExportGlobInstance)
                                     * global_count));
    new_inst->export_memories =
        (WASMExportMemInstance *)((uint8_t *)new_inst->export_tables
                                  + (sizeof(WASMExportTabInstance)
                                     * table_count));
    new_inst->module_type = Wasm_Module_Bytecode;

    for (idx = 0; idx < instance_expression->without_args.inline_expr_len;
         idx++) {
        inline_expr = &instance_expression->without_args.inline_expr[idx];

        switch (inline_expr->sort_idx->sort->core_sort) {
            case WASM_COMP_CORE_SORT_FUNC:
                LOG_DEBUG("Add core func");
                new_inst->export_functions[new_inst->export_func_count].name =
                    inline_expr->name->name;
                new_inst->export_functions[new_inst->export_func_count]
                    .function =
                    comp_instance->core_functions[inline_expr->sort_idx->idx];
                new_inst->export_func_count++;
                break;
            case WASM_COMP_CORE_SORT_TABLE:
                LOG_DEBUG("Add core table");
                new_inst->export_tables[new_inst->export_table_count].name =
                    inline_expr->name->name;
                new_inst->export_tables[new_inst->export_table_count].table =
                    comp_instance->core_tables[inline_expr->sort_idx->idx];
                new_inst->export_table_count++;
                break;
            case WASM_COMP_CORE_SORT_MEMORY:
                LOG_DEBUG("Add core memory");
                new_inst->export_memories[new_inst->export_memory_count].name =
                    inline_expr->name->name;
                new_inst->export_memories[new_inst->export_memory_count]
                    .memory =
                    comp_instance->core_memories[inline_expr->sort_idx->idx];
                new_inst->export_memory_count++;
                break;
            case WASM_COMP_CORE_SORT_GLOBAL:
                LOG_DEBUG("Add core global");
                new_inst->export_globals[new_inst->export_global_count].name =
                    inline_expr->name->name;
                new_inst->export_globals[new_inst->export_global_count].global =
                    comp_instance->core_globals[inline_expr->sort_idx->idx];
                new_inst->export_global_count++;
                break;
            default:
                break;
        }
    }

    if ((new_inst->export_global_count != global_count)
        || (new_inst->export_func_count != func_count)
        || (new_inst->export_memory_count != mem_count)
        || (new_inst->export_table_count != table_count)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Index count exceeded for core exports\n");
        wasm_runtime_free(new_inst);
        return false;
    }
    comp_instance
        ->core_module_instances[comp_instance->core_module_instances_count] =
        new_inst;
    new_inst->core_instance_idx = comp_instance->core_module_instances_count;
    comp_instance->core_module_instances_count++;
    comp_instance
        ->defined_core_instances[comp_instance->defined_core_instances_count] =
        new_inst;
    comp_instance->defined_core_instances_count++;
    return true;
}

/// @brief Resolve core instance section of WASM binary: Either instantiate a
/// core module, with a supplied list of imports OR generate a core instance
/// filled with the supplied exports
/// @param instance_section instance section to be resolved
/// @param comp_instance current component instance
/// @param error_buf
/// @param error_buf_size
/// @return
bool
wasm_resolve_core_instance(WASMComponentCoreInstSection *instance_section,
                           WASMComponentInstance *comp_instance,
                           char *error_buf, uint32 error_buf_size)
{
    uint32 idx = 0;
    WASMComponentCoreInst *core_inst = NULL;
    if (!comp_instance || !instance_section->instances) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: Invalid component instance\n");
        return false;
    }
    for (idx = 0; idx < instance_section->count; idx++) {
        core_inst = &instance_section->instances[idx];
        if (core_inst->instance_expression_tag
            == WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS) {
            if (core_inst->expression.with_args.idx
                >= comp_instance->core_modules_count) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Invalid core module index %d\n",
                                 core_inst->expression.with_args.idx);
                return false;
            }

            if (core_inst->expression.with_args.idx
                >= comp_instance->core_modules_count) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid core module index %d\n",
                                 core_inst->expression.with_args.idx);
                return false;
            }

            WASMModule *target_module =
                comp_instance
                    ->core_modules[core_inst->expression.with_args.idx];
            if (!target_module) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Empty core module\n");
                return false;
            }
            WASMCoreImports found_imports;
            found_imports.func_count = 0;
            found_imports.globals_count = 0;
            found_imports.tables_count = 0;
            found_imports.mem_count = 0;
            found_imports.func_instance = NULL;
            found_imports.table_instance = NULL;
            found_imports.mem_instance = NULL;
            found_imports.global_instance = NULL;
            bool imports_ok = true;

            if (target_module->import_function_count)
                found_imports.func_instance =
                    (WASMFunctionInstance **)wasm_runtime_malloc(
                        target_module->import_function_count
                        * sizeof(WASMFunctionInstance *));
            if (target_module->import_table_count)
                found_imports.table_instance =
                    (WASMTableInstance **)wasm_runtime_malloc(
                        target_module->import_table_count
                        * sizeof(WASMTableInstance *));
            if (target_module->import_memory_count)
                found_imports.mem_instance =
                    (WASMMemoryInstance **)wasm_runtime_malloc(
                        target_module->import_memory_count
                        * sizeof(WASMMemoryInstance *));
            if (target_module->import_global_count)
                found_imports.global_instance =
                    (WASMGlobalInstance **)wasm_runtime_malloc(
                        target_module->import_global_count
                        * sizeof(WASMGlobalInstance *));

            // Match provided instantiation arguments with the imports required
            // by the core_module
            if (core_inst->expression.with_args.arg_len
                && !wasm_resolve_core_imports(
                    &core_inst->expression, target_module, comp_instance,
                    &found_imports, error_buf, error_buf_size)) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Imports could not be resolved for core instance\n");
                goto fail_imports;
            }
            WASMModuleInstance *core_instance = NULL;
            struct InstantiationArgs2 inst_args;
            wasm_runtime_instantiation_args_set_defaults(&inst_args);
            wasm_runtime_instantiation_args_set_default_stack_size(&inst_args,
                                                                   STACK_SIZE);
            wasm_runtime_instantiation_args_set_host_managed_heap_size(
                &inst_args, HEAP_SIZE);
            core_instance =
                wasm_instantiate(target_module, NULL, NULL, &inst_args,
                                 error_buf, sizeof(error_buf));
            if (!core_instance) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Core module instantiation failed\n");
                goto fail_imports;
            }

            uint32 import_idx = 0, func_idx = 0;
            for (func_idx = 0; func_idx < core_instance->e->function_count;
                 func_idx++) {
                core_instance->e->functions[func_idx].module_instance =
                    core_instance;
                core_instance->e->functions[func_idx].func_idx = func_idx;
            }
            for (import_idx = 0; import_idx < found_imports.func_count;
                 import_idx++) {
                if (import_idx > core_instance->e->function_count
                    || !core_instance->e->functions[import_idx]
                            .is_import_func) {
                    goto fail_imports;
                }
                if (!found_imports.func_instance) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "function not found correctly\n");
                    goto fail_imports;
                }
                if (found_imports.func_instance[import_idx]->is_import_func) {
                    core_instance->e->functions[import_idx]
                        .u.func_import->func_ptr_linked =
                        found_imports.func_instance[import_idx]
                            ->u.func_import->func_ptr_linked;
                    core_instance->e->functions[import_idx]
                        .u.func_import->signature =
                        found_imports.func_instance[import_idx]
                            ->u.func_import->signature;
                    core_instance->e->functions[import_idx]
                        .u.func_import->attachment =
                        found_imports.func_instance[import_idx]
                            ->u.func_import->attachment;
                    core_instance->e->functions[import_idx]
                        .u.func_import->call_conv_raw =
                        found_imports.func_instance[import_idx]
                            ->u.func_import->call_conv_raw;
                    core_instance->import_func_ptrs[import_idx] =
                        found_imports.func_instance[import_idx]
                            ->u.func_import->func_ptr_linked;
                    core_instance->e->functions[import_idx].import_func_inst =
                        found_imports.func_instance[import_idx]
                            ->import_func_inst;
                }
                else {
                    core_instance->e->functions[import_idx].local_types =
                        found_imports.func_instance[import_idx]->local_types;
                    core_instance->e->functions[import_idx].local_offsets =
                        found_imports.func_instance[import_idx]->local_offsets;
                    core_instance->e->functions[import_idx].u.func =
                        found_imports.func_instance[import_idx]->u.func;
                    core_instance->e->functions[import_idx].import_module_inst =
                        found_imports.func_instance[import_idx]
                            ->module_instance;
                    core_instance->e->functions[import_idx].import_func_inst =
                        found_imports.func_instance[import_idx];
                }
#if WASM_ENABLE_SIMD != 0 && WASM_ENABLE_FAST_INTERP != 0
                /* Copy const_cell_num so fast interp sets outs_area->lp at the
                 * correct offset when dispatching to this function. Without
                 * this, params written at operand+0 but callee reads from
                 * operand+const_cell_num. */
                core_instance->e->functions[import_idx].const_cell_num =
                    found_imports.func_instance[import_idx]->const_cell_num;
#endif
                core_instance->e->functions[import_idx].is_canon_func =
                    found_imports.func_instance[import_idx]->is_canon_func;
                core_instance->e->functions[import_idx].canon_type =
                    found_imports.func_instance[import_idx]->canon_type;
                core_instance->e->functions[import_idx].resource =
                    found_imports.func_instance[import_idx]->resource;
                core_instance->e->functions[import_idx].canon_options =
                    found_imports.func_instance[import_idx]->canon_options;
                core_instance->e->functions[import_idx].component_function =
                    found_imports.func_instance[import_idx]->component_function;
                core_instance->e->functions[import_idx].canon_options =
                    found_imports.func_instance[import_idx]->canon_options;
            }
            for (import_idx = 0; import_idx < found_imports.tables_count;
                 import_idx++) {
                if (import_idx > core_instance->table_count) {
                    goto fail_imports;
                }
                uint32 elem_idx = 0;
                for (elem_idx = 0;
                     elem_idx < core_instance->tables[import_idx]->max_size;
                     elem_idx++) {
                    uint32 table_func_idx =
                        core_instance->tables[import_idx]->elems[elem_idx];
                    if (table_func_idx >= core_instance->e->function_count) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "Invalid function index passed from table\n");
                        goto fail_imports;
                    }
                    WASMFunctionInstance *table_func_instance =
                        &core_instance->e->functions[table_func_idx];
                    found_imports.table_instance[import_idx]->elems[elem_idx] =
                        (table_elem_type_t)table_func_instance;
                }
                found_imports.table_instance[import_idx]->elem_type =
                    VALUE_TYPE_EXTERNREF;
            }
            for (import_idx = 0; import_idx < found_imports.mem_count;
                 import_idx++) {
                if (import_idx > core_instance->memory_count) {
                    goto fail_imports;
                }
                /* Free resources of the memory allocated by wasm_instantiate
                   before replacing with the imported memory */
                WASMMemoryInstance *orig_mem =
                    core_instance->memories[import_idx];
                if (orig_mem) {
                    if (orig_mem->heap_handle) {
                        mem_allocator_destroy(orig_mem->heap_handle);
                        wasm_runtime_free(orig_mem->heap_handle);
                        orig_mem->heap_handle = NULL;
                    }
                    if (orig_mem->memory_data) {
                        wasm_deallocate_linear_memory(orig_mem);
                        orig_mem->memory_data = NULL;
                    }
                }
                core_instance->memories[import_idx] =
                    found_imports.mem_instance[import_idx];
            }
            for (import_idx = 0; import_idx < found_imports.globals_count;
                 import_idx++) {
                if (import_idx > core_instance->e->global_count) {
                    goto fail_imports;
                }
                memcpy(&core_instance->e->globals[import_idx],
                       found_imports.global_instance[import_idx],
                       sizeof(WASMGlobalInstance));
            }
            core_instance->comp_instance = comp_instance;
            comp_instance->core_module_instances
                [comp_instance->core_module_instances_count] = core_instance;
            core_instance->core_instance_idx =
                comp_instance->core_module_instances_count;
            comp_instance->core_module_instances_count++;
            comp_instance->defined_core_instances
                [comp_instance->defined_core_instances_count] = core_instance;
            comp_instance->defined_core_instances_count++;

            WASMComponentInstance *root_comp_inst = comp_instance;
            while (root_comp_inst->parent)
                root_comp_inst = root_comp_inst->parent;
            WASMComponent *root_comp = root_comp_inst->component;

#if WASM_ENABLE_LIBC_WASI != 0
            wasm_runtime_set_wasi_ctx((WASMModuleInstanceCommon *)core_instance,
                                      comp_instance->wasi_ctx);
#if WASM_ENABLE_UVWASI == 0
            /* The uvwasi WASIContext variant has no wasi_options member;
               only the built-in libc-wasi implementation carries the
               component-model options through the context. */
            WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx(
                (WASMModuleInstanceCommon *)core_instance);
            if (wasi_ctx) {
                wasi_ctx->wasi_options = root_comp->wasi_args.wasi_options;
            }
#endif
#endif
            goto done_imports;
        fail_imports:
            imports_ok = false;
        done_imports:
            if (found_imports.func_instance)
                wasm_runtime_free((void *)found_imports.func_instance);
            if (found_imports.table_instance)
                wasm_runtime_free((void *)found_imports.table_instance);
            if (found_imports.mem_instance)
                wasm_runtime_free((void *)found_imports.mem_instance);
            if (found_imports.global_instance)
                wasm_runtime_free((void *)found_imports.global_instance);
            if (!imports_ok)
                return false;
        }
        else if (core_inst->instance_expression_tag
                 == WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS) {
            if (!wasm_create_core_inst_from_expression(
                    core_inst, comp_instance, error_buf, error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Failed to create core instance from "
                                 "instance expression\n");
                return false;
            }
        }
        else {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "ERROR: Core instance expression tag %d not recognised\n",
                core_inst->instance_expression_tag);
            return false;
        }
    }
    return true;
}

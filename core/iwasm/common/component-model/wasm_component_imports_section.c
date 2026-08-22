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
#include "../../libraries/libc-wasi-p2/libc_wasi_p2_wrapper.h"

// Section 10: imports section
bool
wasm_component_parse_imports_section(const uint8_t **payload,
                                     uint32_t payload_len,
                                     WASMComponentImportSection *out,
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

    // import ::= in:<importname'> ed:<externdesc> => (import in ed)
    // Read the count of imports (LEB128-encoded)
    uint64_t import_count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &import_count_leb, error_buf,
                  error_buf_size)) {
        if (consumed_len)
            *consumed_len = (uint32_t)(p - *payload);
        return false;
    }

    uint32_t import_count = (uint32_t)import_count_leb;
    out->count = import_count;

    if (import_count > 0) {
        out->imports =
            wasm_runtime_malloc(sizeof(WASMComponentImport) * import_count);
        if (!out->imports) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for imports");
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }

        // Initialize all imports to zero to avoid garbage data
        memset(out->imports, 0, sizeof(WASMComponentImport) * import_count);

        for (uint32_t i = 0; i < import_count; ++i) {
            // importname' ::= 0x00 len:<u32> in:<importname> => in (if len =
            // |in|)
            //             | 0x01 len:<u32> in:<importname> vs:<versionsuffix'>
            //             => in vs (if len = |in|)
            // Parse import name (simple or versioned)
            WASMComponentImportName *import_name =
                wasm_runtime_malloc(sizeof(WASMComponentImportName));
            if (!import_name) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate memory for import_name");
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }
            // Initialize the struct to zero to avoid garbage data
            memset(import_name, 0, sizeof(WASMComponentImportName));

            bool status = parse_component_import_name(
                &p, end, import_name, error_buf, error_buf_size);
            if (!status) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse component name for import %u",
                                 i);
                wasm_runtime_free(import_name);
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }

            // externdesc ::= 0x00 0x11 i:<core:typeidx> => (core module (type
            // i))
            //            | 0x01 i:<typeidx> => (func (type i))
            //            | 0x02 b:<valuebound> => (value b)
            //            | 0x03 b:<typebound> => (type b)
            //            | 0x04 i:<typeidx> => (component (type i))
            //            | 0x05 i:<typeidx> => (instance (type i))
            // Parse externdesc (core module, func, value, type, component,
            // instance)
            WASMComponentExternDesc *extern_desc =
                wasm_runtime_malloc(sizeof(WASMComponentExternDesc));
            if (!extern_desc) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate memory for extern_desc");
                wasm_runtime_free(import_name);
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }
            // Initialize the struct to zero to avoid garbage data
            memset(extern_desc, 0, sizeof(WASMComponentExternDesc));

            status = parse_extern_desc(&p, end, extern_desc, error_buf,
                                       error_buf_size);
            if (!status) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse extern_desc for import %u",
                                 i);
                wasm_runtime_free(extern_desc);
                wasm_runtime_free(import_name);
                if (consumed_len)
                    *consumed_len = (uint32_t)(p - *payload);
                return false;
            }

            // Store the parsed import (importname' + externdesc)
            out->imports[i].import_name = import_name;
            out->imports[i].extern_desc = extern_desc;
        }
    }

    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);
    return true;
}

// Individual section free functions
void
wasm_component_free_imports_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.import_section)
        return;

    WASMComponentImportSection *import_sec = section->parsed.import_section;
    if (import_sec->imports) {
        for (uint32_t j = 0; j < import_sec->count; ++j) {
            WASMComponentImport *import = &import_sec->imports[j];

            // Free import name
            if (import->import_name) {
                free_component_import_name(import->import_name);
                wasm_runtime_free(import->import_name);
                import->import_name = NULL;
            }

            // Free extern desc
            if (import->extern_desc) {
                free_extern_desc(import->extern_desc);
                wasm_runtime_free(import->extern_desc);
                import->extern_desc = NULL;
            }
        }
        wasm_runtime_free(import_sec->imports);
        import_sec->imports = NULL;
    }
    wasm_runtime_free(import_sec);
    section->parsed.import_section = NULL;
}

WASMCoreExport *
wasm_import_find_in_args(WASMImport *import, WASMInstExpr *expression,
                         WASMComponentInstance *comp_instance, char *error_buf,
                         uint32 error_buf_size)
{

    if (!import || !expression || !comp_instance) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "ERROR: invalid function call\n");
        return NULL;
    }
    uint32 arg_idx = 0, export_idx = 0;
    const char *module_name = NULL, *field_name = NULL;
    WASMComponentInstArg *arg = NULL;
    WASMModuleInstance *arg_instance = NULL;

    module_name = import->u.names.module_name;
    field_name = import->u.names.field_name;
    if (!module_name) {
        return NULL;
    }

    WASMCoreExport *found_export = wasm_runtime_malloc(sizeof(WASMCoreExport));
    found_export->kind = import->kind;

    for (arg_idx = 0; arg_idx < expression->with_args.arg_len; arg_idx++) {
        arg = &expression->with_args.args[arg_idx];
        // Find Module instance by name
        if (!strcmp(module_name, arg->name->name)) {
            if (arg->idx.instance_idx
                > comp_instance->core_module_instances_count) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Instantiation argument Core instance "
                                 "%d not yet defined\n",
                                 arg->idx.instance_idx);
                return NULL;
            }
            arg_instance =
                comp_instance->core_module_instances[arg->idx.instance_idx];
            // Search thought the respective export list of the provided core
            // instance
            switch (import->kind) {
                case IMPORT_KIND_FUNC:
                    for (export_idx = 0;
                         export_idx < arg_instance->export_func_count;
                         export_idx++) {
                        if (!strcmp(field_name,
                                    arg_instance->export_functions[export_idx]
                                        .name)) {
                            // TODO: Will need to implement a check for function
                            // signature as well
                            LOG_DEBUG("Func import %s found", field_name);
                            found_export->exp.func_instance =
                                arg_instance->export_functions[export_idx]
                                    .function;
                            return found_export;
                        }
                    }
                    break;
                case IMPORT_KIND_TABLE:
                    for (export_idx = 0;
                         export_idx < arg_instance->export_table_count;
                         export_idx++) {
                        if (!strcmp(
                                field_name,
                                arg_instance->export_tables[export_idx].name)) {
                            LOG_DEBUG("table import %s found", field_name);
                            found_export->exp.table_instance =
                                arg_instance->export_tables[export_idx].table;
                            return found_export;
                        }
                    }
                    break;
                case IMPORT_KIND_MEMORY:
                    for (export_idx = 0;
                         export_idx < arg_instance->export_memory_count;
                         export_idx++) {
                        if (!strcmp(field_name,
                                    arg_instance->export_memories[export_idx]
                                        .name)) {
                            LOG_DEBUG("mem import %s found", field_name);
                            found_export->exp.mem_instance =
                                arg_instance->export_memories[export_idx]
                                    .memory;
                            return found_export;
                        }
                    }
                    break;
                case IMPORT_KIND_GLOBAL:
                    for (export_idx = 0;
                         export_idx < arg_instance->export_global_count;
                         export_idx++) {
                        if (!strcmp(field_name,
                                    arg_instance->export_globals[export_idx]
                                        .name)) {
                            LOG_DEBUG("global import %s found", field_name);
                            found_export->exp.global_instance =
                                arg_instance->export_globals[export_idx].global;
                            return found_export;
                        }
                    }
                    break;
                default:
                    LOG_WARNING("Import kind not supported\n");
                    break;
            }
        }
    }
    wasm_runtime_free(found_export);
    return NULL;
}

bool
wasm_resolve_core_imports(WASMInstExpr *expression, WASMModule *target,
                          WASMComponentInstance *comp_instance,
                          WASMCoreImports *found_imports, char *error_buf,
                          uint32 error_buf_size)
{
    // Check if the arguments provided in the instantion expression resolve all
    // the imports required by the target core module
    uint32 import_idx = 0;
    WASMImport *import = NULL;
    WASMCoreExport *imported_instance = NULL;

    if (!target->imports) {
        return false;
    }
    for (import_idx = 0; import_idx < target->import_count; import_idx++) {
        import = &target->imports[import_idx];
        // For each import required by the target core module, check if it is
        // satisfied by one of the exports of the provided core instance
        // arguments
        imported_instance = wasm_import_find_in_args(
            import, expression, comp_instance, error_buf, error_buf_size);

        if (!imported_instance) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Import %d of core instance not found in arguments\n",
                import_idx);
            return false;
        }
        else {
            switch (imported_instance->kind) {
                case IMPORT_KIND_FUNC:
                    found_imports->func_instance[found_imports->func_count] =
                        imported_instance->exp.func_instance;
                    found_imports->func_count++;
                    break;
                case IMPORT_KIND_TABLE:
                    found_imports->table_instance[found_imports->tables_count] =
                        imported_instance->exp.table_instance;
                    found_imports->tables_count++;
                    break;
                case IMPORT_KIND_MEMORY:
                    found_imports->mem_instance[found_imports->mem_count] =
                        imported_instance->exp.mem_instance;
                    found_imports->mem_count++;
                    break;
                case IMPORT_KIND_GLOBAL:
                    found_imports
                        ->global_instance[found_imports->globals_count] =
                        imported_instance->exp.global_instance;
                    found_imports->globals_count++;
                    break;
                default:
                    break;
            }
        }

        wasm_runtime_free(imported_instance);
    }
    return true;
}

bool
wasm_resolve_imports_WASI(WASMComponentImportSection *import_section,
                          WASMComponentInstance *comp_instance, char *error_buf,
                          uint32 error_buf_size)
{
    char *interface_name = NULL;
    uint32 idx = 0;
    uint32 func_export_count = 0;
    uint32 resource_count = 0;
    WASMComponentImport *import = NULL;
    WASMComponentInstTypeInstance *instance_type = NULL;
    WASMComponentInstance *new_inst = NULL;
    WASMFunctionImport *func_import = NULL;
    void *func_ptr = NULL;
    char *field_name = NULL;

    for (idx = 0; idx < import_section->count; idx++) {
        import = &import_section->imports[idx];
        if (!import->extern_desc
            || import->extern_desc->type != WASM_COMP_EXTERN_INSTANCE) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "ERROR: Root component import not a component instance\n");
            return false;
        }
        if (import->import_name->tag == WASM_COMP_IMPORTNAME_SIMPLE) {
            interface_name = import->import_name->imported.simple.name->name;
        }
        else if (import->import_name->tag == WASM_COMP_IMPORTNAME_VERSIONED) {
            interface_name = import->import_name->imported.versioned.name->name;
        }
        else {
            set_error_buf_ex(error_buf, error_buf_size,
                             "ERROR: Component import tag %d not supported\n",
                             import->import_name->tag);
            return false;
        }
        if (!wasm_check_wasi_p2_version(interface_name)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "ERROR: Incompatible WASI version for %s",
                             interface_name);
            return false;
        }

        LOG_DEBUG("  WASI Import : %s", interface_name);

        if (import->extern_desc->extern_desc.instance.type_idx
                >= comp_instance->types_count
            || comp_instance
                       ->types[import->extern_desc->extern_desc.instance
                                   .type_idx]
                       ->type
                   != COMPONENT_VAL_TYPE_INSTANCE) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "ERROR: Invalid instance type index %d\n",
                import->extern_desc->extern_desc.instance.type_idx);
            return false;
        }
        instance_type =
            comp_instance
                ->types[import->extern_desc->extern_desc.instance.type_idx]
                ->type_specific.instance;
        WASMComponentIndexCount index_count = { 0 };
        index_count.types = instance_type->types_count;
        index_count.functions = instance_type->func_count;
        index_count.exports = instance_type->exports_count;
        index_count.defined_functions = instance_type->func_count;
        new_inst = wasm_component_instance_allocate(&index_count, error_buf,
                                                    error_buf_size);
        for (idx = 0; idx < instance_type->types_count; idx++) {
            new_inst->types[new_inst->types_count] = instance_type->types[idx];
            if (new_inst->types[new_inst->types_count]->type
                    == COMPONENT_VAL_TYPE_RESOURCE_SYNC
                || new_inst->types[new_inst->types_count]->type
                       == COMPONENT_VAL_TYPE_RESOURCE_ASYNC) {
                if (new_inst->types[new_inst->types_count]
                        ->type_specific.resource->interface_name) {
                    // Not a local sub resource
                    continue;
                }
                new_inst->types[new_inst->types_count]
                    ->type_specific.resource->is_wasi = true;
                new_inst->types[new_inst->types_count]
                    ->type_specific.resource->interface_name = interface_name;
                resource_count++;
            }
            new_inst->types_count++;
        }
        for (idx = 0; idx < instance_type->func_count; idx++) {
            new_inst->defined_functions[new_inst->defined_functions_count]
                .func_type = instance_type->funcs[idx];
            new_inst->defined_functions[new_inst->defined_functions_count]
                .core_func = NULL;
            new_inst->functions[new_inst->functions_count] =
                &new_inst->defined_functions[new_inst->defined_functions_count];
            new_inst->defined_functions_count++;
            new_inst->functions_count++;
        }
        for (idx = 0; idx < instance_type->exports_count; idx++) {
            new_inst->exports[idx].export_name =
                instance_type->exports[idx].export_name;
            new_inst->exports[idx].type = instance_type->exports[idx].type;
            func_import = instance_type->defined_core_funcs[func_export_count]
                              .u.func_import;
            if (instance_type->exports[idx].type == WASM_COMP_EXTERN_FUNC) {
                if (func_export_count > new_inst->functions_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: Import function index exceeded\n");
                    return false;
                }
                field_name = instance_type->exports[idx]
                                 .export_name->exported.simple.name->name;
                func_ptr = wasm_native_resolve_symbol(
                    interface_name, field_name, NULL, &func_import->signature,
                    &func_import->attachment, &func_import->call_conv_raw);

                if (!func_ptr) {
                    wasm_native_register_wasi_p2_module_func(interface_name,
                                                             field_name);
                    func_ptr = wasm_native_resolve_symbol(
                        interface_name, field_name, NULL,
                        &func_import->signature, &func_import->attachment,
                        &func_import->call_conv_raw);
                }

                if (!func_ptr) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "ERROR: Function %s not found in native symbols\n",
                        field_name);
                    return false;
                }
                LOG_DEBUG(
                    "  WASI function : %s %s found successfully, %d params",
                    interface_name, field_name,
                    instance_type->defined_core_funcs[func_export_count]
                        .param_count);
                new_inst->defined_functions[func_export_count].core_func =
                    &instance_type->defined_core_funcs[func_export_count];
                new_inst->defined_functions[func_export_count]
                    .core_func->is_import_func = true;
                new_inst->defined_functions[func_export_count]
                    .core_func->u.func_import->func_ptr_linked = func_ptr;
                new_inst->exports[idx].exp.function =
                    &new_inst->defined_functions[func_export_count];
                func_export_count++;
                new_inst->exports_count++;
            }
            else if (instance_type->exports[idx].type
                     == WASM_COMP_EXTERN_TYPE) {
                new_inst->exports[idx].exp.type =
                    instance_type->exports[idx].exp.type;
                new_inst->exports_count++;
            }
            else {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "ERROR: Import type %d not supported for WASI imports\n",
                    instance_type->exports[idx].type);
                return false;
            }
        }
    }
    comp_instance->resources_count += resource_count;
    comp_instance
        ->component_instances[comp_instance->component_instances_count] =
        new_inst;
    comp_instance->component_instances_count++;
    comp_instance->defined_instances[comp_instance->defined_instances_count] =
        new_inst;
    comp_instance->defined_instances_count++;
    return true;
}

bool
wasm_resolve_imports(WASMComponentImportSection *import_section,
                     WASMComponentInstance *comp_instance,
                     WASMComponentInstArgInstances *instance_expression,
                     char *error_buf, uint32 error_buf_size)
{
    const char *import_name = NULL;
    const char *arg_name = NULL;
    uint32 arg_idx = 0;
    uint32 idx = 0;
    WASMComponentImport *import = NULL;

    for (idx = 0; idx < import_section->count; idx++) {
        import = &import_section->imports[idx];

        if (import->import_name->tag == WASM_COMP_IMPORTNAME_SIMPLE) {
            import_name = import->import_name->imported.simple.name->name;
        }
        else if (import->import_name->tag == WASM_COMP_IMPORTNAME_VERSIONED) {
            import_name = import->import_name->imported.versioned.name->name;
        }
        else {
            set_error_buf_ex(error_buf, error_buf_size,
                             "ERROR: Component import tag %d not supported\n",
                             import->import_name->tag);
            return false;
        }
        LOG_DEBUG("  Import : %s", import_name);
        WASMComponentInstArgInstance *inst_arg = NULL, *curr_inst_arg = NULL;
        for (arg_idx = 0; arg_idx < instance_expression->arg_len; arg_idx++) {
            curr_inst_arg = &instance_expression->args[arg_idx];
            if (!curr_inst_arg) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "ERROR: Invaid argument list\n");
                return false;
            }
            arg_name = curr_inst_arg->name->name;
            if (!strcmp(import_name, arg_name)) {
                if (import->extern_desc->type
                    != curr_inst_arg->sort_idx->sort->sort) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Import <%s> sort doesn't match",
                                     import_name);
                    return false;
                }
                inst_arg = curr_inst_arg;
                break;
            }
        }
        if (!inst_arg) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Import not found in arguments list\n");
            return false;
        }
        switch (inst_arg->sort_idx->sort->sort) {
            case WASM_COMP_SORT_CORE_SORT:
                if (inst_arg->sort_idx->sort->core_sort != 0x11) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "ERROR: The only allowed core sort is "
                                     "core module, sort %d not allowed",
                                     inst_arg->sort_idx->sort->core_sort);
                    return false;
                }
                LOG_DEBUG("Core module import added");
                comp_instance->core_modules[comp_instance->core_modules_count] =
                    inst_arg->arg.core_module;
                comp_instance->core_modules_count++;
                break;
            case WASM_COMP_SORT_FUNC:
                comp_instance->functions[comp_instance->functions_count] =
                    inst_arg->arg.function;
                comp_instance->functions_count++;
                LOG_DEBUG("Function import added");
                break;
            case WASM_COMP_SORT_VALUE:
                LOG_DEBUG("Value import added");
                comp_instance->values[comp_instance->values_count] =
                    inst_arg->arg.value;
                comp_instance->values_count++;
                break;
            case WASM_COMP_SORT_TYPE:
                LOG_DEBUG("Type import added");
                // Validate type bound: if the import declares (type (sub
                // resource)), verify the provided type is actually a resource
                // type
                if (import->extern_desc->type == WASM_COMP_EXTERN_TYPE) {
                    const WASMComponentTypeBound *tb =
                        import->extern_desc->extern_desc.type.type_bound;
                    if (tb && tb->tag == WASM_COMP_TYPEBOUND_TYPE) {
                        if (!inst_arg->arg.type
                            || (inst_arg->arg.type->type
                                    != COMPONENT_VAL_TYPE_RESOURCE_SYNC
                                && inst_arg->arg.type->type
                                       != COMPONENT_VAL_TYPE_RESOURCE_ASYNC)) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "Type import <%s> has (sub resource) bound but "
                                "argument is not a resource type",
                                import_name);
                            return false;
                        }
                    }
                }
                comp_instance->types[comp_instance->types_count] =
                    inst_arg->arg.type;
                comp_instance->types_count++;
                break;
            case WASM_COMP_SORT_COMPONENT:
                LOG_DEBUG("Component import added");
                comp_instance->components[comp_instance->components_count] =
                    inst_arg->arg.component;
                comp_instance->components_count++;
                break;
            case WASM_COMP_SORT_INSTANCE:
                LOG_DEBUG("Instance import added");
                comp_instance->component_instances
                    [comp_instance->component_instances_count] =
                    inst_arg->arg.instance;
                comp_instance->component_instances_count++;
                break;
            default:
                break;
        }
    }
    return true;
}

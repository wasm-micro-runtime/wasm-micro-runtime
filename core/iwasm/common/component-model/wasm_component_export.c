/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_export.h"
#include "wasm_component.h"
#include "wasm_component_runtime.h"

static bool is_component = false;

bool
is_component_runtime()
{
    return is_component;
}

void
set_component_runtime(bool type)
{
    is_component = type;
}

bool
wasm_resolve_exports(WASMComponentExportSection *export_section,
                     WASMComponentInstance *comp_instance, char *error_buf,
                     uint32 error_buf_size)
{
    uint32 idx = 0;
    uint32 exports_count = export_section->count;
    WASMComponentExport *export = NULL;

    for (idx = 0; idx < exports_count; idx++) {
        export = &export_section->exports[idx];
        comp_instance->exports[comp_instance->exports_count].type =
            (WASMComponentExternDescType) export->sort_idx->sort->sort;
        comp_instance->exports[comp_instance->exports_count].export_name =
            export->export_name;
        if (export->export_name->tag == WASM_COMP_IMPORTNAME_SIMPLE)
            LOG_DEBUG("  Export name: %s",
                      export->export_name->exported.simple.name->name);
        else if (export->export_name->tag == WASM_COMP_IMPORTNAME_VERSIONED)
            LOG_DEBUG("  Export name: %s",
                      export->export_name->exported.versioned.name->name);

        switch (export->sort_idx->sort->sort) {
            case WASM_COMP_EXTERN_CORE_MODULE:
                // TODO: no example found
                break;
            case WASM_COMP_EXTERN_FUNC:
                if (!export->extern_desc && !export->sort_idx) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "WARNING: Empty Extern desc\n");
                    return false;
                }
                if (export->extern_desc
                    && comp_instance
                               ->types[export->extern_desc->extern_desc.func
                                           .type_idx]
                               ->type
                           != COMPONENT_VAL_TYPE_FUNCTION) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "Invalid type %d for function export declaration, "
                        "expected func type\n",
                        comp_instance
                            ->types[export->extern_desc->extern_desc.func
                                        .type_idx]
                            ->type);
                    break;
                }
                comp_instance->functions[comp_instance->functions_count] =
                    comp_instance->functions[export->sort_idx->idx];
                comp_instance->functions_count++;
                comp_instance->exports[comp_instance->exports_count]
                    .exp.function =
                    comp_instance
                        ->functions[comp_instance->functions_count - 1];
                break;
            case WASM_COMP_EXTERN_VALUE:
                LOG_WARNING("Value type export not yet supported\n");
                break;
            case WASM_COMP_EXTERN_TYPE:
                if (!export->extern_desc
                    || export->extern_desc->extern_desc.type.type_bound->tag
                           == WASM_COMP_TYPEBOUND_EQ) {
                    comp_instance->types[comp_instance->types_count] =
                        comp_instance->types[export->sort_idx->idx];
                    comp_instance->types_count++;
                }
                else {
                    // TODO: Sub resource creation -> might be necessary for
                    // user defined resources
                    LOG_WARNING(
                        "User defined resource definition not yet implemented");
                    break;
                }
                comp_instance->exports[comp_instance->exports_count].exp.type =
                    comp_instance->types[comp_instance->types_count - 1];
                break;
            case WASM_COMP_EXTERN_COMPONENT:
                // TODO: no example found
                break;
            case WASM_COMP_EXTERN_INSTANCE:
                comp_instance->component_instances
                    [comp_instance->component_instances_count] =
                    comp_instance->component_instances[export->sort_idx->idx];
                comp_instance->component_instances_count++;
                comp_instance->exports[comp_instance->exports_count]
                    .exp.instance =
                    comp_instance->component_instances
                        [comp_instance->component_instances_count - 1];
                break;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Unsupported export type %d\n",
                                 export->sort_idx->sort->sort);
                break;
        }
        comp_instance->exports_count++;
    }

    return true;
}

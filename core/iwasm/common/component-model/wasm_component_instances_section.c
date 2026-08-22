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

// Section 5: instances section
// Binary.md: instance ::= ie:<instanceexpr> => (instance ie)
// instanceexpr ::= 0x00 c:<componentidx> arg*:vec(<instantiatearg>) =>
// (instantiate c arg*) instantiatearg ::= n:<name> si:<sortidx> => (with n si)
bool
wasm_component_parse_instances_section(const uint8_t **payload,
                                       uint32_t payload_len,
                                       WASMComponentInstSection *out,
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
            wasm_runtime_malloc(sizeof(WASMComponentInst) * instance_count);
        if (!out->instances) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for instances");
            if (consumed_len)
                *consumed_len = (uint32_t)(p - *payload);
            return false;
        }

        // Initialize all instances to zero to avoid garbage data
        memset(out->instances, 0, sizeof(WASMComponentInst) * instance_count);

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
                    // 0x00 c:<componentidx> arg*:vec(<instantiatearg>)
                    uint64_t component_idx = 0;
                    if (!read_leb((uint8_t **)&p, end, 32, false,
                                  &component_idx, error_buf, error_buf_size)) {
                        if (consumed_len)
                            *consumed_len = (uint32_t)(p - *payload);
                        return false;
                    }
                    out->instances[i].expression.with_args.idx =
                        (uint32_t)component_idx;

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
                                             "component instantiate args");
                            if (consumed_len)
                                *consumed_len = (uint32_t)(p - *payload);
                            return false;
                        }

                        // Initialize args to zero
                        memset(out->instances[i].expression.with_args.args, 0,
                               sizeof(WASMComponentInstArg) * arg_len);

                        for (uint32_t j = 0; j < arg_len; ++j) {
                            // Parse core:name (LEB128 length + UTF-8 bytes)
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

                            // si:<sortidx> - this is a component-level sort
                            // index (non-core)
                            out->instances[i]
                                .expression.with_args.args[j]
                                .idx.sort_idx = wasm_runtime_malloc(
                                sizeof(WASMComponentSortIdx));
                            if (!out->instances[i]
                                     .expression.with_args.args[j]
                                     .idx.sort_idx) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Failed to allocate memory "
                                                 "for component arg sort idx");
                                free_core_name(core_name);
                                wasm_runtime_free(core_name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }
                            // Zero-initialize sort_idx
                            memset(out->instances[i]
                                       .expression.with_args.args[j]
                                       .idx.sort_idx,
                                   0, sizeof(WASMComponentSortIdx));

                            // Parse component sort index
                            bool status = parse_sort_idx(
                                &p, end,
                                out->instances[i]
                                    .expression.with_args.args[j]
                                    .idx.sort_idx,
                                error_buf, error_buf_size, false);
                            if (!status) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Failed to parse component arg sort idx");
                                free_core_name(core_name);
                                wasm_runtime_free(core_name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }
                        }
                    }
                    else {
                        out->instances[i].expression.with_args.args = NULL;
                    }
                    break;
                }
                case WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS:
                {
                    // 0x01 e*:vec(<inlineexport>) => e*
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
                                             "component inline exports");
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
                            // inlineexport ::= n:<exportname> si:<sortidx>
                            WASMComponentCoreName *name = wasm_runtime_malloc(
                                sizeof(WASMComponentCoreName));
                            if (!name) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Failed to allocate memory "
                                                 "for component export name");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            // Parse export name (component-level name)
                            bool name_parse_success = parse_core_name(
                                &p, end, &name, error_buf, error_buf_size);
                            if (!name_parse_success) {
                                free_core_name(name);
                                wasm_runtime_free(name);
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }

                            out->instances[i]
                                .expression.without_args.inline_expr[j]
                                .name = name;

                            // Parse component sort index
                            WASMComponentSortIdx *sort_idx =
                                wasm_runtime_malloc(
                                    sizeof(WASMComponentSortIdx));
                            if (!sort_idx) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "Failed to allocate memory "
                                                 "for component sort idx");
                                if (consumed_len)
                                    *consumed_len = (uint32_t)(p - *payload);
                                return false;
                            }
                            // Zero-initialize sort_idx
                            memset(sort_idx, 0, sizeof(WASMComponentSortIdx));

                            bool status =
                                parse_sort_idx(&p, end, sort_idx, error_buf,
                                               error_buf_size, false);
                            if (!status) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "Failed to parse component sort idx");
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
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Unknown instance expression tag: 0x%02X",
                                     tag);
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
wasm_component_free_instances_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.instance_section)
        return;

    WASMComponentInstSection *instance_sec = section->parsed.instance_section;
    if (instance_sec->instances) {
        for (uint32_t j = 0; j < instance_sec->count; ++j) {
            WASMComponentInst *instance = &instance_sec->instances[j];

            switch (instance->instance_expression_tag) {
                case WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS:
                    if (instance->expression.with_args.args) {
                        for (uint32_t k = 0;
                             k < instance->expression.with_args.arg_len; ++k) {
                            WASMComponentInstArg *arg =
                                &instance->expression.with_args.args[k];

                            // Free component name
                            if (arg->name) {
                                free_core_name(arg->name);
                                wasm_runtime_free(arg->name);
                                arg->name = NULL;
                            }

                            // Free component sort index
                            if (arg->idx.sort_idx) {
                                if (arg->idx.sort_idx->sort) {
                                    wasm_runtime_free(arg->idx.sort_idx->sort);
                                    arg->idx.sort_idx->sort = NULL;
                                }
                                wasm_runtime_free(arg->idx.sort_idx);
                                arg->idx.sort_idx = NULL;
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

                            // Free component export name
                            if (inline_export->name) {
                                free_core_name(inline_export->name);
                                wasm_runtime_free(inline_export->name);
                                inline_export->name = NULL;
                            }

                            // Free component sort index
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
        wasm_runtime_free(instance_sec->instances);
        instance_sec->instances = NULL;
    }
    wasm_runtime_free(instance_sec);
    section->parsed.instance_section = NULL;
}

bool
wasm_resolve_instance(struct WASMComponentInstSection *instance_section,
                      WASMComponentInstance *comp_instance, char *error_buf,
                      uint32 error_buf_size)
{
    uint32 idx = 0, arg_idx = 0;
    WASMComponentInst *instance = NULL;
    WASMComponent *target = NULL;
    WASMComponentInstArg *arg = NULL;
    for (idx = 0; idx < instance_section->count; idx++) {
        instance = &instance_section->instances[idx];
        if (instance->instance_expression_tag
            == WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS) {
            if (instance->expression.with_args.idx
                > comp_instance->components_count) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Component index exceeded, component %d not yet defined\n",
                    instance->expression.with_args.idx);
                return false;
            }
            target =
                comp_instance->components[instance->expression.with_args.idx];
            WASMComponentInstArgInstances instance_expression;
            instance_expression.parent = comp_instance;
            instance_expression.arg_len =
                instance->expression.with_args.arg_len;
            instance_expression.args = NULL;
            bool inst_ok = true;

            if (instance_expression.arg_len) {
                instance_expression.args =
                    (WASMComponentInstArgInstance *)wasm_runtime_malloc(
                        instance_expression.arg_len
                        * sizeof(WASMComponentInstArgInstance));
                if (!instance_expression.args) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "ERROR: Failed to allocate instance expression args");
                    goto fail_inst;
                }
            }

            for (arg_idx = 0; arg_idx < instance->expression.with_args.arg_len;
                 arg_idx++) {
                arg = &instance->expression.with_args.args[arg_idx];
                instance_expression.args[arg_idx].name = arg->name;
                instance_expression.args[arg_idx].sort_idx = arg->idx.sort_idx;

                switch (arg->idx.sort_idx->sort->sort) {
                    case WASM_COMP_SORT_CORE_SORT:
                        if (arg->idx.sort_idx->sort->core_sort != 0x11) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: The only allowed core sort is core "
                                "module, sort %d not allowed",
                                arg->idx.sort_idx->sort->core_sort);
                            goto fail_inst;
                        }
                        if (arg->idx.sort_idx->idx
                            > comp_instance->core_modules_count) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Core module %d not yet defined",
                                arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        LOG_DEBUG("Added core module expression argument");
                        instance_expression.args[arg_idx].arg.core_module =
                            comp_instance->core_modules[arg->idx.sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_FUNC:
                        if (arg->idx.sort_idx->idx
                            > comp_instance->functions_count) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Function %d not yet defined",
                                arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        instance_expression.args[arg_idx].arg.function =
                            comp_instance->functions[arg->idx.sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_VALUE:
                        if (arg->idx.sort_idx->idx
                            > comp_instance->values_count) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "ERROR: Value %d not yet defined",
                                             arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        instance_expression.args[arg_idx].arg.value =
                            comp_instance->values[arg->idx.sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_TYPE:
                        if (arg->idx.sort_idx->idx
                            > comp_instance->types_count) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Function %d not yet defined",
                                arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        instance_expression.args[arg_idx].arg.type =
                            comp_instance->types[arg->idx.sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_COMPONENT:
                        if (arg->idx.sort_idx->idx
                            > comp_instance->components_count) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Components %d not yet defined",
                                arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        instance_expression.args[arg_idx].arg.component =
                            comp_instance->components[arg->idx.sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_INSTANCE:
                        if (arg->idx.sort_idx->idx
                            > comp_instance->component_instances_count) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Component instance %d not yet defined",
                                arg->idx.sort_idx->idx);
                            goto fail_inst;
                        }
                        instance_expression.args[arg_idx].arg.instance =
                            comp_instance
                                ->component_instances[arg->idx.sort_idx->idx];
                        break;
                    default:
                        break;
                }
            }

            WASMComponentInstance *new_inst =
                wasm_component_instantiate_internal(
                    target, &instance_expression, error_buf, error_buf_size);
            if (!new_inst) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "ERROR: Instantiation %d of inner component %d failed\n",
                    comp_instance->component_instances_count,
                    instance->expression.with_args.idx);
                goto fail_inst;
            }
            comp_instance->component_instances
                [comp_instance->component_instances_count] = new_inst;
            comp_instance->component_instances_count++;
            comp_instance
                ->defined_instances[comp_instance->defined_instances_count] =
                new_inst;
            comp_instance->defined_instances_count++;
            goto done_inst;
        fail_inst:
            inst_ok = false;
        done_inst:
            if (instance_expression.args) wasm_runtime_free(instance_expression.args);
            instance_expression.args = NULL;
            if (!inst_ok)
                return false;
        }
        else if (instance->instance_expression_tag
                 == WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS) {
            WASMComponentIndexCount index_count = { 0 };
            WASMComponentInlineExport *inst_expression = NULL;
            for (idx = 0;
                 idx < instance->expression.without_args.inline_expr_len;
                 idx++) {
                inst_expression =
                    &instance->expression.without_args.inline_expr[idx];
                switch (inst_expression->sort_idx->sort->sort) {
                    case WASM_COMP_SORT_CORE_SORT:
                        if (inst_expression->sort_idx->sort->core_sort
                            != 0x11) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: The only allowed core sort is core "
                                "module, sort %d not allowed",
                                inst_expression->sort_idx->sort->core_sort);
                            return false;
                        }
                        break;
                    case WASM_COMP_SORT_FUNC:
                        index_count.functions++;
                        break;
                    case WASM_COMP_SORT_VALUE:
                        index_count.values++;
                        break;
                    case WASM_COMP_SORT_TYPE:
                        index_count.types++;
                        break;
                    case WASM_COMP_SORT_COMPONENT:
                        index_count.components++;
                        break;
                    case WASM_COMP_SORT_INSTANCE:
                        index_count.instances++;
                        break;
                    default:
                        break;
                }
            }
            WASMComponentInstance *new_inst = wasm_component_instance_allocate(
                &index_count, error_buf, error_buf_size);
            new_inst->parent = comp_instance;

            for (idx = 0;
                 idx < instance->expression.without_args.inline_expr_len;
                 idx++) {
                inst_expression =
                    &instance->expression.without_args.inline_expr[idx];

                WASMComponentExportInstance *inline_export =
                    &new_inst->exports[new_inst->exports_count];
                inline_export->export_name->exported.simple.name =
                    inst_expression->name;
                inline_export->export_name->tag =
                    strstr(inst_expression->name->name, "@")
                        ? WASM_COMP_IMPORTNAME_VERSIONED
                        : WASM_COMP_IMPORTNAME_SIMPLE;
                inline_export->type = inst_expression->sort_idx->sort->sort;

                // Validation requires any exported sortidx to have a valid
                // externdesc (which disallows core sorts other than core
                // module)
                switch (inst_expression->sort_idx->sort->sort) {
                    case WASM_COMP_SORT_CORE_SORT:
                        if (inst_expression->sort_idx->sort->core_sort
                            != 0x11) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "ERROR: Export definition only suport module "
                                "core sort, sort %d not supported",
                                inst_expression->sort_idx->sort->core_sort);
                            free(new_inst);
                            return false;
                        }
                        new_inst->core_modules[new_inst->core_modules_count] =
                            comp_instance
                                ->core_modules[inst_expression->sort_idx->idx];
                        new_inst->core_modules_count++;
                        inline_export->exp.core_module =
                            comp_instance
                                ->core_modules[inst_expression->sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_FUNC:
                        new_inst->functions[new_inst->functions_count] =
                            comp_instance
                                ->functions[inst_expression->sort_idx->idx];
                        new_inst->functions_count++;
                        inline_export->exp.function =
                            comp_instance
                                ->functions[inst_expression->sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_VALUE:
                        new_inst->values[new_inst->values_count] =
                            comp_instance
                                ->values[inst_expression->sort_idx->idx];
                        new_inst->values_count++;
                        inline_export->exp.value =
                            comp_instance
                                ->values[inst_expression->sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_TYPE:
                        new_inst->types[new_inst->types_count] =
                            comp_instance
                                ->types[inst_expression->sort_idx->idx];
                        new_inst->types_count++;
                        inline_export->exp.type =
                            comp_instance
                                ->types[inst_expression->sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_COMPONENT:
                        new_inst->components[new_inst->components_count] =
                            comp_instance
                                ->components[inst_expression->sort_idx->idx];
                        new_inst->components_count++;
                        inline_export->exp.component =
                            comp_instance
                                ->components[inst_expression->sort_idx->idx];
                        break;
                    case WASM_COMP_SORT_INSTANCE:
                        new_inst->component_instances
                            [new_inst->component_instances_count] =
                            comp_instance->component_instances
                                [inst_expression->sort_idx->idx];
                        new_inst->component_instances_count++;
                        inline_export->exp.instance =
                            comp_instance->component_instances
                                [inst_expression->sort_idx->idx];
                        break;
                    default:
                        break;
                }
                new_inst->exports_count++;
            }
            comp_instance->component_instances
                [comp_instance->component_instances_count] = new_inst;
            comp_instance
                ->defined_instances[comp_instance->defined_instances_count] =
                new_inst;
            comp_instance->defined_instances_count++;
        }
        else {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Instance expression type <%d> not supported\n",
                             instance->instance_expression_tag);
            return false;
        }
    }
    return true;
}

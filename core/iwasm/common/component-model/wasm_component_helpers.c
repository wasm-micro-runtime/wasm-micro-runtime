/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wasm_loader_common.h"
#include "wasm_runtime_common.h"
#include "../interpreter/wasm_runtime.h"
#include "wasm_export.h"
#include <stdio.h>

// Set an error message in the provided buffer, supporting printf-style
// formatting
void
set_error_buf_ex(char *error_buf, uint32_t error_buf_size, const char *format,
                 ...)
{
    // Standard error buffer formatting for all parsing errors
    if (error_buf && error_buf_size > 0 && format) {
        va_list args;
        va_start(args, format);
        vsnprintf(error_buf, error_buf_size, format, args);
        va_end(args);
    }
}

bool
parse_result_list(const uint8_t **payload, const uint8_t *end,
                  WASMComponentResultList **out, char *error_buf,
                  uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    // Allocate memory for the result list structure
    *out = wasm_runtime_malloc(sizeof(WASMComponentResultList));
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for result list");
        return false;
    }
    memset(*out, 0, sizeof(WASMComponentResultList));

    uint8_t tag = *p;
    (*out)->tag = tag;
    p++;

    switch (tag) {
        case WASM_COMP_RESULT_LIST_WITH_TYPE:
        {
            // Allocate memory for the single result value type
            (*out)->results =
                wasm_runtime_malloc(sizeof(WASMComponentValueType));
            if (!(*out)->results) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "Failed to allocate memory for result value type");
                return false;
            }
            memset((*out)->results, 0, sizeof(WASMComponentValueType));

            if (!parse_valtype(&p, end, (*out)->results, error_buf,
                               error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse result");
                return false;
            }
            break;
        }
        case WASM_COMP_RESULT_LIST_EMPTY:
        {
            (*out)->results = NULL;
            // Binary.md encodes empty resultlist as 0x01 0x00
            uint8_t terminator = *p++;
            if (terminator != 0x00) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid empty resultlist terminator: 0x%02x",
                                 terminator);
                return false;
            }
            break;
        }
        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid result list tag: 0x%02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

bool
parse_sort(const uint8_t **payload, const uint8_t *end, WASMComponentSort *out,
           char *error_buf, uint32_t error_buf_size, bool is_core)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }
    const uint8_t *p = *payload;

    if (!is_core) {
        // Read the first byte, which is the main sort
        out->sort = *p++;
        out->core_sort = INVALID_VALUE;

        switch (out->sort) {
            case WASM_COMP_SORT_CORE_SORT: // 0x00
            case WASM_COMP_SORT_FUNC:      // 0x01
            case WASM_COMP_SORT_VALUE:     // 0x02
            case WASM_COMP_SORT_TYPE:      // 0x03
            case WASM_COMP_SORT_COMPONENT: // 0x04
            case WASM_COMP_SORT_INSTANCE:  // 0x05
                break;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid sort value: 0x%02x", out->sort);
                *payload = p;
                return false;
        }
    }
    else {
        // Read the first byte, which is the core sort
        out->sort = 0x00;
        out->core_sort = *p++;
    }

    // If the main sort is 0x00 (core), we must also read the specific core sort
    if (out->sort == 0x00) {
        // Check again if we've reached the end of the buffer
        if (p >= end) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unexpected end of buffer when parsing core_sort");
            *payload = p; // Update pointer even on error
            return false;
        }
        // Read the second byte if is not core:sort
        if (!is_core) {
            out->core_sort = *p++;
        }
    }

    if (out->core_sort != INVALID_VALUE) {
        switch (out->core_sort) {
            case WASM_COMP_CORE_SORT_FUNC:     // 0x00
            case WASM_COMP_CORE_SORT_TABLE:    // 0x01
            case WASM_COMP_CORE_SORT_MEMORY:   // 0x02
            case WASM_COMP_CORE_SORT_GLOBAL:   // 0x03
            case WASM_COMP_CORE_SORT_TYPE:     // 0x10
            case WASM_COMP_CORE_SORT_MODULE:   // 0x11
            case WASM_COMP_CORE_SORT_INSTANCE: // 0x12
                break;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid core sort value: 0x%02x",
                                 out->core_sort);
                *payload = p;
                return false;
        }
    }
    // For any other value of s->sort, the s->core_sort field remains 0 and is
    // ignored.

    // Update the original pointer
    *payload = p;
    return true;
}

bool
parse_sort_idx(const uint8_t **payload, const uint8_t *end,
               WASMComponentSortIdx *out, char *error_buf,
               uint32_t error_buf_size, bool is_core)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    // Allocate memory for the sort field using WAMR memory management
    out->sort = wasm_runtime_malloc(sizeof(WASMComponentSort));
    if (!out->sort) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for sort");
        return false;
    }

    // Parse the sort first
    bool status =
        parse_sort(payload, end, out->sort, error_buf, error_buf_size, is_core);
    if (!status) {
        wasm_runtime_free(out->sort);
        return false;
    }

    // Parse the idx as LEB128-encoded value
    uint64_t idx_leb = 0;
    if (!read_leb((uint8_t **)payload, end, 32, false, &idx_leb, error_buf,
                  error_buf_size)) {
        wasm_runtime_free(out->sort);
        return false;
    }

    out->idx = (uint32_t)idx_leb;
    return true;
}

bool
parse_extern_desc(const uint8_t **payload, const uint8_t *end,
                  WASMComponentExternDesc *out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    out->type = *p++;

    switch (out->type) {
        case WASM_COMP_EXTERN_CORE_MODULE:
        {
            // 0x00 0x11 i:<core:typeidx> => (core module (type i))
            // Read type_specific byte (should be 0x11)
            uint8_t type_specific = *p++;
            if (type_specific != 0x11) {
                *payload = p; // Update pointer even on error
                return false;
            }
            // Read core type index (LEB128-encoded)
            uint64_t type_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx, error_buf,
                          error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->extern_desc.core_module.type_specific = type_specific;
            out->extern_desc.core_module.type_idx = (uint32_t)type_idx;
            break;
        }
        case WASM_COMP_EXTERN_FUNC:
        {
            // 0x01 i:<typeidx> => (func (type i))
            // Read function type index (LEB128-encoded)
            uint64_t type_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx, error_buf,
                          error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->extern_desc.func.type_idx = (uint32_t)type_idx;
            break;
        }
        case WASM_COMP_EXTERN_VALUE:
        {
            // 0x02 b:<valuebound> => (value b)
            // valuebound ::= 0x00 i:<valueidx> => (eq i)
            //             | 0x01 t:<valtype> => t
            // Read value bound tag (0x00 = eq, 0x01 = type)
            uint8_t value_bound_tag = *p++;
            out->extern_desc.value.value_bound =
                wasm_runtime_malloc(sizeof(WASMComponentValueBound));
            if (!out->extern_desc.value.value_bound) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate memory for value_bound");
                *payload = p; // Update pointer even on error
                return false;
            }
            out->extern_desc.value.value_bound->tag = value_bound_tag;
            switch (value_bound_tag) {
                case WASM_COMP_VALUEBOUND_EQ:
                {
                    // 0x00 i:<valueidx> => (eq i)
                    // Read value index (LEB128-encoded)
                    uint64_t value_idx = 0;
                    if (!read_leb((uint8_t **)&p, end, 32, false, &value_idx,
                                  error_buf, error_buf_size)) {
                        wasm_runtime_free(out->extern_desc.value.value_bound);
                        *payload = p; // Update pointer even on error
                        return false;
                    }
                    out->extern_desc.value.value_bound->bound.value_idx =
                        (uint32_t)value_idx;
                    break;
                }
                case WASM_COMP_VALUEBOUND_TYPE:
                {
                    // 0x01 t:<valtype> => t
                    // Allocate memory for value_type
                    out->extern_desc.value.value_bound->bound.value_type =
                        wasm_runtime_malloc(sizeof(WASMComponentValueType));
                    if (!out->extern_desc.value.value_bound->bound.value_type) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "Failed to allocate memory for value_type");
                        wasm_runtime_free(out->extern_desc.value.value_bound);
                        *payload = p; // Update pointer even on error
                        return false;
                    }

                    // Use the refactored parse_valtype function
                    if (!parse_valtype(&p, end,
                                       out->extern_desc.value.value_bound->bound
                                           .value_type,
                                       error_buf, error_buf_size)) {
                        wasm_runtime_free(out->extern_desc.value.value_bound
                                              ->bound.value_type);
                        wasm_runtime_free(out->extern_desc.value.value_bound);
                        *payload = p; // Update pointer even on error
                        return false;
                    }
                    break;
                }
                default:
                {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "Unknown value bound tag: 0x%02X",
                                     value_bound_tag);
                    wasm_runtime_free(out->extern_desc.value.value_bound);
                    *payload = p; // Update pointer even on error
                    return false;
                }
            }
            break;
        }
        case WASM_COMP_EXTERN_TYPE:
        {
            // Parse typebound for extern_desc type
            // typebound ::= 0x00 i:<typeidx> => (eq i)
            //            | 0x01            => (sub resource)
            uint8_t type_bound_tag = *p++;
            out->extern_desc.type.type_bound =
                wasm_runtime_malloc(sizeof(WASMComponentTypeBound));
            if (!out->extern_desc.type.type_bound) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate memory for type_bound");
                *payload = p; // Update pointer even on error
                return false;
            }
            // Always set the tag field
            out->extern_desc.type.type_bound->tag = type_bound_tag;

            if (type_bound_tag == WASM_COMP_TYPEBOUND_EQ) {
                // 0x00 i:<typeidx> => (eq i)
                // Read a type index (LEB128-encoded)
                uint64_t type_idx = 0;
                if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx,
                              error_buf, error_buf_size)) {
                    wasm_runtime_free(out->extern_desc.type.type_bound);
                    *payload = p; // Update pointer even on error
                    return false;
                }
                out->extern_desc.type.type_bound->type_idx = (uint32_t)type_idx;
            }
            else if (type_bound_tag != WASM_COMP_TYPEBOUND_TYPE) {
                // 0x01 => (sub resource) — valid but no extra data
                // anything else is malformed
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid typebound tag: 0x%02x",
                                 type_bound_tag);
                wasm_runtime_free(out->extern_desc.type.type_bound);
                *payload = p;
                return false;
            }
            break;
        }
        case WASM_COMP_EXTERN_COMPONENT:
        {
            // 0x04 i:<typeidx> => (component (type i))
            // Read component type index (LEB128-encoded)
            uint64_t type_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx, error_buf,
                          error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->extern_desc.component.type_idx = (uint32_t)type_idx;
            break;
        }
        case WASM_COMP_EXTERN_INSTANCE:
        {
            // 0x05 i:<typeidx> => (instance (type i))
            // Read instance type index (LEB128-encoded)
            uint64_t type_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &type_idx, error_buf,
                          error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->extern_desc.instance.type_idx = (uint32_t)type_idx;
            break;
        }
        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unknown extern_desc type: 0x%02X", out->type);
            *payload = p; // Update pointer even on error
            return false;
        }
    }

    // Update the original pointer
    *payload = p;
    return true;
}

void
free_extern_desc(WASMComponentExternDesc *desc)
{
    if (!desc) {
        return;
    }

    switch (desc->type) {
        case WASM_COMP_EXTERN_VALUE:
        {
            WASMComponentValueBound *vb = desc->extern_desc.value.value_bound;
            if (vb) {
                if (vb->tag == WASM_COMP_VALUEBOUND_TYPE
                    && vb->bound.value_type) {
                    wasm_runtime_free(vb->bound.value_type);
                    vb->bound.value_type = NULL;
                }
                wasm_runtime_free(vb);
                desc->extern_desc.value.value_bound = NULL;
            }
            break;
        }
        case WASM_COMP_EXTERN_TYPE:
        {
            if (desc->extern_desc.type.type_bound) {
                wasm_runtime_free(desc->extern_desc.type.type_bound);
                desc->extern_desc.type.type_bound = NULL;
            }
            break;
        }
        default:
            // Other extern types don't have nested allocations
            break;
    }

    // Note: Do NOT free desc itself - it's part of a larger structure
    // that will be freed by the parent
}

// Parse a simple core:name (LEB128 length + UTF-8 bytes)
bool
parse_core_name(const uint8_t **payload, const uint8_t *end,
                WASMComponentCoreName **out, char *error_buf,
                uint32_t error_buf_size)
{
    if (!payload || !*payload || !end || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;
    uint64_t name_len_leb = 0;

    if (!read_leb((uint8_t **)&p, end, 32, false, &name_len_leb, error_buf,
                  error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to read core name length");
        *payload = p; // Update pointer even on error
        return false;
    }

    // Handle zero-length names (empty strings)
    if (name_len_leb == 0) {
        WASMComponentCoreName *result =
            wasm_runtime_malloc(sizeof(WASMComponentCoreName));
        if (!result) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "Failed to allocate memory for core name structure");
            *payload = p; // Update pointer even on error
            return false;
        }

        result->name_len = 0;
        result->name =
            wasm_runtime_malloc(1); // Allocate 1 byte for null terminator
        if (!result->name) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for core name string");
            wasm_runtime_free(result);
            *payload = p; // Update pointer even on error
            return false;
        }

        result->name[0] = '\0'; // Empty string
        *payload = p;
        *out = result;
        return true;
    }

    // Check bounds before proceeding
    if (p + name_len_leb > end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Core name extends beyond buffer");
        *payload = p; // Update pointer even on error
        return false;
    }

    // Validate UTF-8 encoding before allocating memory
    if (!wasm_check_utf8_str(p, (uint32_t)name_len_leb)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid UTF-8 encoding in core name");
        *payload = p; // Update pointer even on error
        return false;
    }

    WASMComponentCoreName *result =
        wasm_runtime_malloc(sizeof(WASMComponentCoreName));
    if (!result) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for core name structure");
        *payload = p; // Update pointer even on error
        return false;
    }

    result->name_len = (uint32_t)name_len_leb;
    result->name = wasm_runtime_malloc(name_len_leb + 1);
    if (!result->name) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for core name string");
        wasm_runtime_free(result);
        *payload = p; // Update pointer even on error
        return false;
    }

    memcpy(result->name, p, name_len_leb);
    result->name[name_len_leb] = '\0';
    p += name_len_leb;

    // Update the original pointer
    *payload = p;
    *out = result;
    return true;
}

// Free a core name structure
void
free_core_name(WASMComponentCoreName *core_name)
{
    if (core_name && core_name->name) {
        wasm_runtime_free(core_name->name);
        core_name->name = NULL;
    }
}

bool
parse_component_import_name(const uint8_t **payload, const uint8_t *end,
                            WASMComponentImportName *out, char *error_buf,
                            uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    switch (tag) {
        case WASM_COMP_IMPORTNAME_SIMPLE:
        {
            out->tag = WASM_COMP_IMPORTNAME_SIMPLE;
            WASMComponentCoreName *name = NULL;
            if (!parse_core_name(&p, end, &name, error_buf, error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->imported.simple.name = name;
            break;
        }
        case WASM_COMP_IMPORTNAME_VERSIONED:
        {
            out->tag = WASM_COMP_IMPORTNAME_VERSIONED;
            WASMComponentCoreName *name = NULL;
            if (!parse_core_name(&p, end, &name, error_buf, error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }

            WASMComponentCoreName *version = NULL;
            if (!parse_core_name(&p, end, &version, error_buf,
                                 error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to read version name");
                free_core_name(name);
                wasm_runtime_free(name);
                *payload = p; // Update pointer even on error
                return false;
            }

            // Merge into "name@version", check first char for @
            bool version_has_at =
                (version->name_len > 0 && version->name[0] == '@');
            uint32_t sep_len = version_has_at ? 0 : 1;
            uint32_t full_len = name->name_len + sep_len + version->name_len;
            char *full = wasm_runtime_malloc(full_len + 1);
            if (!full) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate versioned name");
                free_core_name(name);
                wasm_runtime_free(name);
                free_core_name(version);
                wasm_runtime_free(version);
                *payload = p;
                return false;
            }
            memcpy(full, name->name, name->name_len);
            if (!version_has_at)
                full[name->name_len] = '@';
            memcpy(full + name->name_len + sep_len, version->name,
                   version->name_len);
            full[full_len] = '\0';

            wasm_runtime_free(name->name);
            name->name = full;
            name->name_len = full_len;

            free_core_name(version);
            wasm_runtime_free(version);

            out->imported.versioned.name = name;
            out->imported.versioned.version = NULL;
            break;
        }
        default:
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unknown import/export name tag: 0x%02X", tag);
            *payload = p; // Update pointer even on error
            return false;
    }

    // Update the original pointer
    *payload = p;
    return true;
}

bool
parse_component_export_name(const uint8_t **payload, const uint8_t *end,
                            WASMComponentExportName *out, char *error_buf,
                            uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    switch (tag) {
        case WASM_COMP_IMPORTNAME_SIMPLE:
        {
            out->tag = WASM_COMP_IMPORTNAME_SIMPLE;
            WASMComponentCoreName *name = NULL;
            if (!parse_core_name(&p, end, &name, error_buf, error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }
            out->exported.simple.name = name;
            break;
        }
        case WASM_COMP_IMPORTNAME_VERSIONED:
        {
            out->tag = WASM_COMP_IMPORTNAME_VERSIONED;
            WASMComponentCoreName *name = NULL;
            if (!parse_core_name(&p, end, &name, error_buf, error_buf_size)) {
                *payload = p; // Update pointer even on error
                return false;
            }

            WASMComponentCoreName *version = NULL;
            if (!parse_core_name(&p, end, &version, error_buf,
                                 error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to read version name");
                free_core_name(name);
                wasm_runtime_free(name);
                *payload = p; // Update pointer even on error
                return false;
            }

            // Merge into "name@version", check first char for @
            bool version_has_at =
                (version->name_len > 0 && version->name[0] == '@');
            uint32_t sep_len = version_has_at ? 0 : 1;
            uint32_t full_len = name->name_len + sep_len + version->name_len;
            char *full = wasm_runtime_malloc(full_len + 1);
            if (!full) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate versioned name");
                free_core_name(name);
                wasm_runtime_free(name);
                free_core_name(version);
                wasm_runtime_free(version);
                *payload = p;
                return false;
            }
            memcpy(full, name->name, name->name_len);
            if (!version_has_at)
                full[name->name_len] = '@';
            memcpy(full + name->name_len + sep_len, version->name,
                   version->name_len);
            full[full_len] = '\0';

            wasm_runtime_free(name->name);
            name->name = full;
            name->name_len = full_len;

            free_core_name(version);
            wasm_runtime_free(version);

            out->exported.versioned.name = name;
            out->exported.versioned.version = NULL;
            break;
        }
        default:
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unknown import/export name tag: 0x%02X", tag);
            *payload = p; // Update pointer even on error
            return false;
    }

    // Update the original pointer
    *payload = p;
    return true;
}

void
free_component_import_name(WASMComponentImportName *name_struct)
{
    if (!name_struct) {
        return;
    }

    switch (name_struct->tag) {
        case WASM_COMP_IMPORTNAME_SIMPLE:
            if (name_struct->imported.simple.name) {
                free_core_name(name_struct->imported.simple.name);
                wasm_runtime_free(name_struct->imported.simple.name);
                name_struct->imported.simple.name = NULL;
            }
            break;
        case WASM_COMP_IMPORTNAME_VERSIONED:
            if (name_struct->imported.versioned.name) {
                free_core_name(name_struct->imported.versioned.name);
                wasm_runtime_free(name_struct->imported.versioned.name);
                name_struct->imported.versioned.name = NULL;
            }
            if (name_struct->imported.versioned.version) {
                free_core_name(name_struct->imported.versioned.version);
                wasm_runtime_free(name_struct->imported.versioned.version);
                name_struct->imported.versioned.version = NULL;
            }
            break;
    }

    // Note: Do NOT free name_struct itself - it's part of a larger structure
    // that will be freed by the parent
}

void
free_component_export_name(WASMComponentExportName *name_struct)
{
    if (!name_struct) {
        return;
    }

    switch (name_struct->tag) {
        case WASM_COMP_IMPORTNAME_SIMPLE:
            if (name_struct->exported.simple.name) {
                free_core_name(name_struct->exported.simple.name);
                wasm_runtime_free(name_struct->exported.simple.name);
                name_struct->exported.simple.name = NULL;
            }
            break;
        case WASM_COMP_IMPORTNAME_VERSIONED:
            if (name_struct->exported.versioned.name) {
                free_core_name(name_struct->exported.versioned.name);
                wasm_runtime_free(name_struct->exported.versioned.name);
                name_struct->exported.versioned.name = NULL;
            }
            if (name_struct->exported.versioned.version) {
                free_core_name(name_struct->exported.versioned.version);
                wasm_runtime_free(name_struct->exported.versioned.version);
                name_struct->exported.versioned.version = NULL;
            }
            break;
    }

    // Note: Do NOT free name_struct itself - it's part of a larger structure
    // that will be freed by the parent
}

// -----------------------------------------------------------------------------
// Type Section Helper Functions
// -----------------------------------------------------------------------------

bool
is_defvaltype_tag(uint8_t byte)
{
    // Check if it's a primitive type
    if (is_primitive_type(byte)) {
        return true;
    }

    // Check if it's a defvaltype constructor
    switch (byte) {
        case 0x72: // record
        case 0x71: // variant
        case 0x70: // list
        case 0x67: // list with length
        case 0x6f: // tuple
        case 0x6e: // flags
        case 0x6d: // enum
        case 0x6b: // option
        case 0x6a: // result
        case 0x69: // own
        case 0x68: // borrow
        case 0x66: // stream
        case 0x65: // future
            return true;
        default:
            return false;
    }
}

WASMComponentTypesTag
get_type_tag(uint8_t first_byte)
{
    // Check if it's a defvaltype (primitive types or defvaltype constructors)
    if (is_defvaltype_tag(first_byte)) {
        return WASM_COMP_DEF_TYPE;
    }

    // Check other types
    switch (first_byte) {
        case 0x40: // functype
        case 0x43: // functype (async)
            return WASM_COMP_FUNC_TYPE;
        case 0x41: // componenttype
            return WASM_COMP_COMPONENT_TYPE;
        case 0x42: // instancetype
            return WASM_COMP_INSTANCE_TYPE;
        case 0x3f: // resourcetype
            return WASM_COMP_RESOURCE_TYPE_SYNC;
        case 0x3e: // resourcetype (async variant)
            return WASM_COMP_RESOURCE_TYPE_ASYNC;
        default:
            return WASM_COMP_INVALID_TYPE;
    }
}

bool
parse_valtype(const uint8_t **payload, const uint8_t *end,
              WASMComponentValueType *out, char *error_buf,
              uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    uint8_t first_byte = **payload;

    // Check if it's a primitive type
    if (is_primitive_type(first_byte)) {
        out->type = WASM_COMP_VAL_TYPE_PRIMVAL; // Indicates primitive
        out->type_specific.primval_type = first_byte;
        (*payload)++; // Advance pointer
        return true;
    }
    else {
        // Parse as type index (LEB128)
        uint64_t type_idx = 0;
        if (!read_leb((uint8_t **)payload, end, 32, false, &type_idx, error_buf,
                      error_buf_size)) {
            return false;
        }
        out->type = WASM_COMP_VAL_TYPE_IDX; // Indicates type index
        out->type_specific.type_idx = (uint32_t)type_idx;
        return true;
    }
}

bool
parse_labelvaltype(const uint8_t **payload, const uint8_t *end,
                   WASMComponentLabelValType *out, char *error_buf,
                   uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    // Parse the label (core name)
    if (!parse_core_name(payload, end, &out->label, error_buf,
                         error_buf_size)) {
        return false;
    }

    // Parse the value type (required)
    out->value_type = wasm_runtime_malloc(sizeof(WASMComponentValueType));
    if (!out->value_type) {
        free_core_name(out->label);
        wasm_runtime_free(out->label);
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for label value type");
        return false;
    }
    memset(out->value_type, 0, sizeof(WASMComponentValueType));
    if (!parse_valtype(payload, end, out->value_type, error_buf,
                       error_buf_size)) {
        wasm_runtime_free(out->value_type);
        out->value_type = NULL;
        free_core_name(out->label);
        wasm_runtime_free(out->label);
        out->label = NULL;
        return false;
    }

    return true;
}

bool
parse_case(const uint8_t **payload, const uint8_t *end,
           WASMComponentCaseValType *out, char *error_buf,
           uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    // Parse the label (core name)
    if (!parse_core_name(payload, end, &out->label, error_buf,
                         error_buf_size)) {
        return false;
    }

    // Parse optional valtype
    uint8_t optional_tag = *(*payload)++;

    if (optional_tag == WASM_COMP_OPTIONAL_TRUE) {
        // Present - parse valtype
        out->value_type = wasm_runtime_malloc(sizeof(WASMComponentValueType));
        if (!out->value_type) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for case value type");
            free_core_name(out->label);
            return false;
        }

        if (!parse_valtype(payload, end, out->value_type, error_buf,
                           error_buf_size)) {
            wasm_runtime_free(out->value_type);
            free_core_name(out->label);
            return false;
        }
    }
    else if (optional_tag == WASM_COMP_OPTIONAL_FALSE) {
        // Absent - set to NULL
        out->value_type = NULL;
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Malformed binary: invalid optional tag 0x%02x",
                         optional_tag);
        return false;
    }

    // Parse the ending 0x00
    uint8_t ending_byte = *(*payload)++;
    if (ending_byte != WASM_COMP_CASE_END) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Expected 0x00 at end of case, got 0x%02x",
                         ending_byte);
        if (out->value_type) {
            wasm_runtime_free(out->value_type);
        }
        free_core_name(out->label);
        return false;
    }

    return true;
}

// Parse a label' (length-prefixed label) for flags and enum types
bool
parse_label_prime(const uint8_t **payload, const uint8_t *end,
                  WASMComponentCoreName **out, char *error_buf,
                  uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Read the length prefix
    uint64_t len_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &len_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t len = (uint32_t)len_leb;

    // Check if we have enough bytes for the label
    if ((uint32_t)(end - p) < len) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Insufficient bytes for label: expected %u, got %zu",
                         len, (size_t)(end - p));
        return false;
    }

    // Check that the core name structure is allocated
    if (!*out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for label");
        return false;
    }

    // Allocate and copy the label string
    (*out)->name = wasm_runtime_malloc(len + 1);
    if (!(*out)->name) {
        wasm_runtime_free(*out);
        *out = NULL;
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for label string");
        return false;
    }

    memcpy((*out)->name, p, len);
    (*out)->name[len] = '\0'; // Null-terminate
    (*out)->name_len = len;

    p += len;
    *payload = p;

    return true;
}

// Helper function to fill an already-allocated label structure
static bool
fill_label_prime(const uint8_t **payload, const uint8_t *end,
                 WASMComponentCoreName *out, char *error_buf,
                 uint32_t error_buf_size)
{
    if (!payload || !*payload || !out || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Read the length prefix
    uint64_t len_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &len_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t len = (uint32_t)len_leb;

    // Check if we have enough bytes for the label
    if ((uint32_t)(end - p) < len) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Insufficient bytes for label: expected %u, got %zu",
                         len, (size_t)(end - p));
        return false;
    }

    // Allocate and copy the label string
    out->name = wasm_runtime_malloc(len + 1);
    if (!out->name) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to allocate memory for label string");
        return false;
    }

    memcpy(out->name, p, len);
    out->name[len] = '\0'; // Null-terminate
    out->name_len = len;

    p += len;
    *payload = p;

    return true;
}

// Parse vec<label'> for flags and enum types
bool
parse_label_prime_vector(const uint8_t **payload, const uint8_t *end,
                         WASMComponentCoreName **out_labels,
                         uint32_t *out_count, char *error_buf,
                         uint32_t error_buf_size)
{
    if (!payload || !*payload || !out_labels || !out_count || !end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    const uint8_t *p = *payload;

    // Read the vector length
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }
    uint32_t count = (uint32_t)count_leb;

    if (count > 0) {
        // Allocate the labels array
        *out_labels =
            wasm_runtime_malloc(sizeof(WASMComponentCoreName) * count);
        if (!*out_labels) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for labels array");
            return false;
        }

        // Initialize to zero
        memset(*out_labels, 0, sizeof(WASMComponentCoreName) * count);

        // Parse each label
        for (uint32_t i = 0; i < count; i++) {
            if (!fill_label_prime(&p, end, &((*out_labels)[i]), error_buf,
                                  error_buf_size)) {
                // Clean up already parsed labels
                for (uint32_t j = 0; j < i; j++) {
                    if ((*out_labels)[j].name) {
                        wasm_runtime_free((*out_labels)[j].name);
                    }
                }
                wasm_runtime_free(*out_labels);
                *out_labels = NULL;
                return false;
            }
        }
    }
    else {
        *out_labels = NULL;
    }

    *out_count = count;
    *payload = p;
    return true;
}

// Free a label_prime (single label')
void
free_label_prime(WASMComponentCoreName *label)
{
    if (label) {
        if (label->name) {
            wasm_runtime_free(label->name);
            label->name = NULL;
        }
    }
}

// Free a label_prime_vector (array of labels)
void
free_label_prime_vector(WASMComponentCoreName *labels, uint32_t count)
{
    if (labels) {
        for (uint32_t i = 0; i < count; i++) {
            if (labels[i].name) {
                wasm_runtime_free(labels[i].name);
            }
        }
        wasm_runtime_free(labels);
    }
}

void
free_labelvaltype(WASMComponentLabelValType *labelvaltype)
{
    if (labelvaltype) {
        if (labelvaltype->label) {
            free_core_name(labelvaltype->label);
            wasm_runtime_free(labelvaltype->label);
            labelvaltype->label = NULL;
        }
        if (labelvaltype->value_type) {
            wasm_runtime_free(labelvaltype->value_type);
            labelvaltype->value_type = NULL;
        }
    }
}

void
free_case(WASMComponentCaseValType *case_valtype)
{
    if (case_valtype) {
        if (case_valtype->label) {
            free_core_name(case_valtype->label);
            wasm_runtime_free(case_valtype->label);
            case_valtype->label = NULL;
        }
        if (case_valtype->value_type) {
            wasm_runtime_free(case_valtype->value_type);
            case_valtype->value_type = NULL;
        }
    }
}

// Validate a UTF-8 sequence for general correctness (delegates to
// wasm_check_utf8_str)
bool
wasm_component_validate_utf8(const uint8_t *bytes, uint32_t len)
{
    if (!bytes && len != 0) {
        return false;
    }
    return wasm_check_utf8_str(bytes, len);
}

// Validate that bytes encode exactly one UTF-8 scalar value
bool
wasm_component_validate_single_utf8_scalar(const uint8_t *bytes, uint32_t len)
{
    if (!bytes || len < 1 || len > 4) {
        return false;
    }

    const uint8_t b0 = bytes[0];

    if (b0 <= 0x7F) {
        // 1-byte ASCII
        return len == 1;
    }

    if (b0 >= 0xC2 && b0 <= 0xDF) {
        // 2-byte non-overlong
        if (len != 2)
            return false;
        const uint8_t b1 = bytes[1];
        return (b1 >= 0x80 && b1 <= 0xBF);
    }

    if (b0 == 0xE0) {
        // 3-byte, first continuation restricted to avoid overlongs
        if (len != 3)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2];
        return (b1 >= 0xA0 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF);
    }
    if (b0 >= 0xE1 && b0 <= 0xEC) {
        if (len != 3)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2];
        return (b1 >= 0x80 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF);
    }
    if (b0 == 0xED) {
        // 0xED leads into UTF-16 surrogate range; disallow 0x80..0x9F second
        // byte
        if (len != 3)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2];
        return (b1 >= 0x80 && b1 <= 0x9F)
                   ? false
                   : (b1 >= 0xA0 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF);
    }
    if (b0 >= 0xEE && b0 <= 0xEF) {
        if (len != 3)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2];
        return (b1 >= 0x80 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF);
    }

    if (b0 == 0xF0) {
        // 4-byte, restrict second byte to avoid overlong
        if (len != 4)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2], b3 = bytes[3];
        return (b1 >= 0x90 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF)
               && (b3 >= 0x80 && b3 <= 0xBF);
    }
    if (b0 >= 0xF1 && b0 <= 0xF3) {
        if (len != 4)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2], b3 = bytes[3];
        return (b1 >= 0x80 && b1 <= 0xBF) && (b2 >= 0x80 && b2 <= 0xBF)
               && (b3 >= 0x80 && b3 <= 0xBF);
    }
    if (b0 == 0xF4) {
        // Limit to U+10FFFF -> second byte <= 0x8F
        if (len != 4)
            return false;
        const uint8_t b1 = bytes[1], b2 = bytes[2], b3 = bytes[3];
        return (b1 >= 0x80 && b1 <= 0x8F) && (b2 >= 0x80 && b2 <= 0xBF)
               && (b3 >= 0x80 && b3 <= 0xBF);
    }

    // Invalid leading byte or overlong prefix (0xC0, 0xC1) or > 0xF4
    return false;
}

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
#include "wasm_export.h"
#include <stdio.h>

bool
parse_core_limits(const uint8_t **payload, const uint8_t *end,
                  WASMComponentCoreLimits *out, char *error_buf,
                  uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    switch (tag) {
        case WASM_CORE_LIMITS_MIN:
        {
            uint64_t min = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &min, error_buf,
                          error_buf_size)) {
                return false;
            }

            out->tag = WASM_CORE_LIMITS_MIN;
            out->lim.limits.min = (uint32_t)min;
            break;
        }
        case WASM_CORE_LIMITS_MAX:
        {
            uint64_t min = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &min, error_buf,
                          error_buf_size)) {
                return false;
            }

            uint64_t max = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &max, error_buf,
                          error_buf_size)) {
                return false;
            }

            out->tag = WASM_CORE_LIMITS_MAX;
            out->lim.limits_max.min = (uint32_t)min;
            out->lim.limits_max.max = (uint32_t)max;
            break;
        }
        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid limits tag: %02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

static bool
parse_core_heaptype(const uint8_t **payload, const uint8_t *end,
                    WASMComponentCoreHeapType *out, char *error_buf,
                    uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    if (p >= end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Unexpected end while parsing heaptype");
        return false;
    }

    uint8_t first = *p;
    // absheaptype short-form: 0x6A..0x73
    if (is_core_absheaptype(first)) {
        out->tag = WASM_CORE_HEAP_TYPE_ABSTRACT;
        out->heap_type.abstract_type = (WASMCoreAbsHeapTypeTag)first;
        p++;
        *payload = p;
        return true;
    }

    // Otherwise, parse as (positive) s33 index; we store as u32
    uint64_t idx = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &idx, error_buf,
                  error_buf_size)) {
        return false;
    }
    if (!is_valid_core_heap_type_index(idx)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid heaptype index: %llu",
                         (unsigned long long)idx);
        return false;
    }

    out->tag = WASM_CORE_HEAP_TYPE_CONCRETE;
    out->heap_type.concrete_index = (uint32_t)idx;
    *payload = p;
    return true;
}

bool
parse_core_valtype(const uint8_t **payload, const uint8_t *end,
                   WASMComponentCoreValType *out, char *error_buf,
                   uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    // numtype ::= 0x7F (i32) | 0x7E (i64) | 0x7D (f32) | 0x7C (f64)
    if (tag >= WASM_CORE_NUM_TYPE_F64 && tag <= WASM_CORE_NUM_TYPE_I32) {
        out->tag = WASM_CORE_VALTYPE_NUM;
        out->type.num_type = (WASMCoreNumTypeTag)tag;
        *payload = p;
        return true;
    }

    // vectype ::= 0x7B (v128)
    if (tag == WASM_CORE_VECTOR_TYPE_V128) {
        out->tag = WASM_CORE_VALTYPE_VECTOR;
        out->type.vector_type = WASM_CORE_VECTOR_TYPE_V128;
        *payload = p;
        return true;
    }

    // reftype encodings (Preview 1 + GC):
    // - 0x63 ht:heaptype => (ref null ht)
    // - 0x64 ht:heaptype => (ref ht)
    // - ht:absheaptype (0x6A..0x73) => short-form (ref null ht)
    if (tag == 0x63 || tag == 0x64) {
        WASMComponentCoreHeapType heap_type;
        if (!parse_core_heaptype(&p, end, &heap_type, error_buf,
                                 error_buf_size)) {
            return false;
        }
        if (heap_type.tag == WASM_CORE_HEAP_TYPE_ABSTRACT) {
            if (heap_type.heap_type.abstract_type
                == WASM_CORE_ABS_HEAP_TYPE_FUNC) {
                out->tag = WASM_CORE_VALTYPE_REF;
                out->type.ref_type = WASM_CORE_REFTYPE_FUNC_REF;
                *payload = p;
                return true;
            }
            if (heap_type.heap_type.abstract_type
                == WASM_CORE_ABS_HEAP_TYPE_EXTERN) {
                out->tag = WASM_CORE_VALTYPE_REF;
                out->type.ref_type = WASM_CORE_REFTYPE_EXTERN_REF;
                *payload = p;
                return true;
            }
        }
        set_error_buf_ex(error_buf, error_buf_size,
                         "Unsupported reftype heaptype for core valtype");
        return false;
    }

    // Short-form absheaptype for (ref null ht)
    if (tag >= WASM_CORE_ABS_HEAP_TYPE_ARRAY
        && tag <= WASM_CORE_ABS_HEAP_TYPE_NOFUNC) {
        if (tag == WASM_CORE_ABS_HEAP_TYPE_FUNC) {
            out->tag = WASM_CORE_VALTYPE_REF;
            out->type.ref_type = WASM_CORE_REFTYPE_FUNC_REF;
            *payload = p;
            return true;
        }
        if (tag == WASM_CORE_ABS_HEAP_TYPE_EXTERN) {
            out->tag = WASM_CORE_VALTYPE_REF;
            out->type.ref_type = WASM_CORE_REFTYPE_EXTERN_REF;
            *payload = p;
            return true;
        }
        set_error_buf_ex(
            error_buf, error_buf_size,
            "Unsupported short-form absheaptype %02x for core valtype", tag);
        return false;
    }

    set_error_buf_ex(error_buf, error_buf_size,
                     "Invalid core valtype tag: %02x", tag);
    return false;
}

bool
parse_core_import_desc(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreImportDesc *out, char *error_buf,
                       uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    switch (tag) {
        case WASM_CORE_IMPORTDESC_FUNC:
        {
            uint64_t func_type_idx = 0;
            if (!read_leb((uint8_t **)&p, end, 32, false, &func_type_idx,
                          error_buf, error_buf_size)) {
                return false;
            }

            out->type = WASM_CORE_IMPORTDESC_FUNC;
            out->desc.func_type_idx = (uint32_t)func_type_idx;
            break;
        }

        case WASM_CORE_IMPORTDESC_TABLE:
        {
            uint8_t ref_type_tag = *p++;
            if (ref_type_tag == WASM_CORE_REFTYPE_FUNC_REF) {
                out->type = WASM_CORE_IMPORTDESC_TABLE;
                out->desc.table_type.ref_type = WASM_CORE_REFTYPE_FUNC_REF;
            }
            else if (ref_type_tag == WASM_CORE_REFTYPE_EXTERN_REF) {
                out->type = WASM_CORE_IMPORTDESC_TABLE;
                out->desc.table_type.ref_type = WASM_CORE_REFTYPE_EXTERN_REF;
            }
            else {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid reference type tag: %02x",
                                 ref_type_tag);
                return false;
            }

            WASMComponentCoreLimits *limits =
                wasm_runtime_malloc(sizeof(WASMComponentCoreLimits));
            if (!limits) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating table limits");
                return false;
            }
            if (!parse_core_limits(&p, end, limits, error_buf,
                                   error_buf_size)) {
                wasm_runtime_free(limits);
                return false;
            }

            out->type = WASM_CORE_IMPORTDESC_TABLE;
            out->desc.table_type.limits = limits;
            break;
        }

        case WASM_CORE_IMPORTDESC_MEMORY:
        {
            WASMComponentCoreLimits *limits =
                wasm_runtime_malloc(sizeof(WASMComponentCoreLimits));
            if (!limits) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating memory limits");
                return false;
            }
            if (!parse_core_limits(&p, end, limits, error_buf,
                                   error_buf_size)) {
                wasm_runtime_free(limits);
                return false;
            }

            out->type = WASM_CORE_IMPORTDESC_MEMORY;
            out->desc.memory_type.limits = limits;
            break;
        }

        case WASM_CORE_IMPORTDESC_GLOBAL:
        {
            if (!parse_core_valtype(&p, end, &out->desc.global_type.val_type,
                                    error_buf, error_buf_size)) {
                return false;
            }

            // mut ::= 0x00 => const, 0x01 => var (spec)
            uint8_t mutable_tag = *p++;
            if (mutable_tag == WASM_CORE_GLOBAL_MUTABLE) {
                out->desc.global_type.is_mutable = true;
            }
            else if (mutable_tag == WASM_CORE_GLOBAL_IMMUTABLE) {
                out->desc.global_type.is_mutable = false;
            }
            else {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Invalid mutable tag: %02x", mutable_tag);
                return false;
            }
            break;
        }

        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid import descriptor tag: %02x", tag);
            return false;
        }
    }

    *payload = p;
    return true;
}

bool
parse_core_import(const uint8_t **payload, const uint8_t *end,
                  WASMComponentCoreImport *out, char *error_buf,
                  uint32_t error_buf_size)
{
    const uint8_t *p = *payload;

    WASMComponentCoreName *mod_name = NULL;
    if (!parse_core_name(&p, end, &mod_name, error_buf, error_buf_size)) {
        return false;
    }

    out->mod_name = mod_name;

    WASMComponentCoreName *nm_name = NULL;
    if (!parse_core_name(&p, end, &nm_name, error_buf, error_buf_size)) {
        return false;
    }

    out->nm = nm_name;

    WASMComponentCoreImportDesc *import_desc =
        wasm_runtime_malloc(sizeof(WASMComponentCoreImportDesc));
    if (!import_desc) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "OOM allocating import desc");
        return false;
    }
    if (!parse_core_import_desc(&p, end, import_desc, error_buf,
                                error_buf_size)) {
        wasm_runtime_free(import_desc);
        return false;
    }

    out->import_desc = import_desc;

    *payload = p;
    return true;
}

bool
parse_alias_target(const uint8_t **payload, const uint8_t *end,
                   WASMComponentCoreAliasTarget *out, char *error_buf,
                   uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p++;

    if (tag == 0x01) {
        uint64_t ct_leb = 0;
        if (!read_leb((uint8_t **)&p, end, 32, false, &ct_leb, error_buf,
                      error_buf_size)) {
            return false;
        }

        out->ct = (uint32_t)ct_leb;

        uint64_t index_leb = 0;
        if (!read_leb((uint8_t **)&p, end, 32, false, &index_leb, error_buf,
                      error_buf_size)) {
            return false;
        }

        out->index = (uint32_t)index_leb;
    }
    else {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid alias target tag: %02x", tag);
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_core_alias(const uint8_t **payload, const uint8_t *end,
                 WASMComponentCoreAlias *out, char *error_buf,
                 uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    /* no leading tag here; core:alias is parsed inside moduledecl */

    if (!parse_sort(&p, end, &out->sort, error_buf, error_buf_size, true)) {
        return false;
    }

    if (!parse_alias_target(&p, end, &out->alias_target, error_buf,
                            error_buf_size)) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Failed to parse alias target");
        return false;
    }

    *payload = p;
    return true;
}

bool
parse_core_export_decl(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreExportDecl *out, char *error_buf,
                       uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    /* core:exportdecl ::= nm:<core:name> d:<core:exportdesc> */
    WASMComponentCoreName *mod_name = NULL;
    if (!parse_core_name(&p, end, &mod_name, error_buf, error_buf_size)) {
        return false;
    }
    out->name = mod_name;

    WASMComponentCoreImportDesc *export_desc =
        wasm_runtime_malloc(sizeof(WASMComponentCoreImportDesc));
    if (!export_desc) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "OOM allocating export desc");
        return false;
    }
    if (!parse_core_import_desc(&p, end, export_desc, error_buf,
                                error_buf_size)) {
        wasm_runtime_free(export_desc);
        return false;
    }

    out->export_desc = export_desc;

    *payload = p;
    return true;
}

bool
parse_core_module_decl(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreModuleDecl *out, char *error_buf,
                       uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    uint8_t tag = *p++;
    out->tag = tag;
    switch (tag) {
        case WASM_CORE_MODULEDECL_IMPORT:
        {
            out->decl.import_decl.import =
                wasm_runtime_malloc(sizeof(WASMComponentCoreImport));
            if (!out->decl.import_decl.import) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating module import decl");
                return false;
            }
            if (!parse_core_import(&p, end, out->decl.import_decl.import,
                                   error_buf, error_buf_size)) {
                return false;
            }
            break;
        }

        case WASM_CORE_MODULEDECL_TYPE:
        {
            out->decl.type_decl.type =
                wasm_runtime_malloc(sizeof(WASMComponentCoreType));
            if (!out->decl.type_decl.type) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating module type decl");
                return false;
            }
            out->decl.type_decl.type->deftype =
                wasm_runtime_malloc(sizeof(WASMComponentCoreDefType));
            if (!out->decl.type_decl.type->deftype) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating core deftype");
                return false;
            }
            if (!parse_single_core_type(&p, end,
                                        out->decl.type_decl.type->deftype,
                                        error_buf, error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse type declaration");
                return false;
            }
            break;
        }

        case WASM_CORE_MODULEDECL_ALIAS:
        {
            out->decl.alias_decl.alias =
                wasm_runtime_malloc(sizeof(WASMComponentCoreAlias));
            if (!out->decl.alias_decl.alias) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating module alias decl");
                return false;
            }
            if (!parse_core_alias(&p, end, out->decl.alias_decl.alias,
                                  error_buf, error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse alias declaration");
                return false;
            }
            break;
        }

        case WASM_CORE_MODULEDECL_EXPORT:
        {
            out->decl.export_decl.export_decl =
                wasm_runtime_malloc(sizeof(WASMComponentCoreExportDecl));
            if (!out->decl.export_decl.export_decl) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "OOM allocating module export decl");
                return false;
            }
            if (!parse_core_export_decl(&p, end,
                                        out->decl.export_decl.export_decl,
                                        error_buf, error_buf_size)) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to parse export declaration");
                return false;
            }
            break;
        }

        default:
        {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Invalid module declaration tag: %02x", tag);
            return false;
        }
    }
    *payload = p;
    return true;
}

bool
parse_core_moduletype(const uint8_t **payload, const uint8_t *end,
                      WASMComponentCoreModuleType *out, char *error_buf,
                      uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    // Expect vec(moduledecl): count then that many moduledecl
    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }

    uint32_t count = (uint32_t)count_leb;
    out->decl_count = count;

    if (count > 0) {
        out->declarations =
            wasm_runtime_malloc(sizeof(WASMComponentCoreModuleDecl) * count);
        if (!out->declarations) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for declarations");
            return false;
        }
        // Zero-initialize declarations array
        memset(out->declarations, 0,
               sizeof(WASMComponentCoreModuleDecl) * count);

        for (uint32_t i = 0; i < count; i++) {
            if (!parse_core_module_decl(&p, end, &out->declarations[i],
                                        error_buf, error_buf_size)) {
                return false;
            }
        }
    }

    *payload = p;
    return true;
}

bool
parse_single_core_type(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreDefType *out, char *error_buf,
                       uint32_t error_buf_size)
{
    const uint8_t *p = *payload;
    if (p >= end) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Unexpected end while parsing core:deftype");
        return false;
    }

    uint8_t b0 = *p; // peek only

    // 1) moduletype ::= 0x50 md*:vec(moduledecl)
    if (b0 == 0x50) {
        p++; // consume 0x50
        out->tag = WASM_CORE_DEFTYPE_MODULETYPE;
        out->type.moduletype =
            wasm_runtime_malloc(sizeof(WASMComponentCoreModuleType));
        if (!out->type.moduletype) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "OOM allocating core moduletype");
            return false;
        }
        if (!parse_core_moduletype(&p, end, out->type.moduletype, error_buf,
                                   error_buf_size)) {
            return false;
        }
        *payload = p;
        return true;
    }

    // 2) rectype (GC): 0x4E ...
    if (b0 == 0x4E) {
        set_error_buf_ex(
            error_buf, error_buf_size,
            "WebAssembly 3.0 core:rectype (0x4E ...) not supported");
        return false;
    }

    // 3) subtype (GC): 0x00 followed by {0x50,0x4F,0x5E,0x5F,0x60}
    if (b0 == 0x00) {
        if (p + 1 >= end) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Unexpected end of data after 0x00");
            return false;
        }
        uint8_t b1 = *(p + 1);
        if (b1 == 0x50 || b1 == 0x4F || b1 == 0x5E || b1 == 0x5F
            || b1 == 0x60) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "WebAssembly 3.0 core:subtype (0x00 0x%02x ...) not supported",
                b1);
            return false;
        }
    }

    // Otherwise invalid in this context
    set_error_buf_ex(error_buf, error_buf_size,
                     "Invalid core:deftype tag: %02x", b0);
    return false;
}

// Section 3: type section (component model type section, not the core wasm type
// section)
bool
wasm_component_parse_core_type_section(const uint8_t **payload,
                                       uint32_t payload_len,
                                       WASMComponentCoreTypeSection *out,
                                       char *error_buf, uint32_t error_buf_size,
                                       uint32_t *consumed_len)
{
    if (!payload || !*payload || payload_len == 0) {
        return false;
    }
    if (consumed_len)
        *consumed_len = 0;

    const uint8_t *p = *payload;
    const uint8_t *end = *payload + payload_len;

    uint64_t count_leb = 0;
    if (!read_leb((uint8_t **)&p, end, 32, false, &count_leb, error_buf,
                  error_buf_size)) {
        return false;
    }

    uint32_t count = (uint32_t)count_leb;
    out->count = count;

    if (count > 0) {
        out->types = wasm_runtime_malloc(sizeof(WASMComponentCoreType) * count);
        if (!out->types) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "Failed to allocate memory for types");
            return false;
        }

        memset(out->types, 0, sizeof(WASMComponentCoreType) * count);
        for (uint32_t i = 0; i < count; i++) {
            WASMComponentCoreDefType *dt =
                wasm_runtime_malloc(sizeof(WASMComponentCoreDefType));
            if (!dt) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "Failed to allocate memory for core deftype");
                return false;
            }
            memset(dt, 0, sizeof(WASMComponentCoreDefType));
            if (!parse_single_core_type(&p, end, dt, error_buf,
                                        error_buf_size)) {
                wasm_runtime_free(dt);
                return false;
            }
            out->types[i].deftype = dt;
        }
    }

    if (consumed_len)
        *consumed_len = (uint32_t)(p - *payload);
    return true;
}

// Individual section free functions
void
wasm_component_free_core_type_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.core_type_section) {
        return;
    }

    WASMComponentCoreTypeSection *core_type_section =
        section->parsed.core_type_section;

    if (core_type_section->types) {
        for (uint32_t i = 0; i < core_type_section->count; i++) {
            if (core_type_section->types[i].deftype) {
                free_core_deftype(core_type_section->types[i].deftype);
                wasm_runtime_free(core_type_section->types[i].deftype);
            }
        }
        wasm_runtime_free(core_type_section->types);
    }

    wasm_runtime_free(core_type_section);
    section->parsed.core_type_section = NULL;
}

// Helper functions for freeing core structures
void
free_core_import_desc(WASMComponentCoreImportDesc *import_desc)
{
    if (!import_desc) {
        return;
    }

    switch (import_desc->type) {
        case WASM_CORE_IMPORTDESC_FUNC:
            break;
        case WASM_CORE_IMPORTDESC_TABLE:
            if (import_desc->desc.table_type.limits) {
                wasm_runtime_free(import_desc->desc.table_type.limits);
                import_desc->desc.table_type.limits = NULL;
            }
            break;
        case WASM_CORE_IMPORTDESC_MEMORY:
            if (import_desc->desc.memory_type.limits) {
                wasm_runtime_free(import_desc->desc.memory_type.limits);
                import_desc->desc.memory_type.limits = NULL;
            }
            break;
        case WASM_CORE_IMPORTDESC_GLOBAL:
            // These are simple structures, no additional memory to free
            break;
    }
}

void
free_core_import(WASMComponentCoreImport *import)
{
    if (!import) {
        return;
    }

    if (import->mod_name) {
        free_core_name(import->mod_name);
        wasm_runtime_free(import->mod_name);
    }

    if (import->nm) {
        free_core_name(import->nm);
        wasm_runtime_free(import->nm);
    }

    if (import->import_desc) {
        free_core_import_desc(import->import_desc);
        wasm_runtime_free(import->import_desc);
    }
}

void
free_core_export_decl(WASMComponentCoreExportDecl *export_decl)
{
    if (!export_decl) {
        return;
    }

    if (export_decl->name) {
        free_core_name(export_decl->name);
        wasm_runtime_free(export_decl->name);
    }

    if (export_decl->export_desc) {
        free_core_import_desc(export_decl->export_desc);
        wasm_runtime_free(export_decl->export_desc);
    }
}

void
free_core_module_decl(WASMComponentCoreModuleDecl *module_decl)
{
    if (!module_decl) {
        return;
    }

    switch (module_decl->tag) {
        case WASM_CORE_MODULEDECL_IMPORT:
            if (module_decl->decl.import_decl.import) {
                free_core_import(module_decl->decl.import_decl.import);
                wasm_runtime_free(module_decl->decl.import_decl.import);
            }
            break;
        case WASM_CORE_MODULEDECL_TYPE:
            if (module_decl->decl.type_decl.type) {
                free_core_type(module_decl->decl.type_decl.type);
                wasm_runtime_free(module_decl->decl.type_decl.type);
            }
            break;
        case WASM_CORE_MODULEDECL_ALIAS:
            if (module_decl->decl.alias_decl.alias) {
                wasm_runtime_free(module_decl->decl.alias_decl.alias);
            }
            break;
        case WASM_CORE_MODULEDECL_EXPORT:
            if (module_decl->decl.export_decl.export_decl) {
                free_core_export_decl(
                    module_decl->decl.export_decl.export_decl);
                wasm_runtime_free(module_decl->decl.export_decl.export_decl);
            }
            break;
    }
}

void
free_core_moduletype(WASMComponentCoreModuleType *moduletype)
{
    if (!moduletype) {
        return;
    }

    if (moduletype->declarations) {
        for (uint32_t i = 0; i < moduletype->decl_count; i++) {
            free_core_module_decl(&moduletype->declarations[i]);
        }
        wasm_runtime_free(moduletype->declarations);
    }
}

void
free_core_deftype(WASMComponentCoreDefType *deftype)
{
    if (!deftype) {
        return;
    }

    switch (deftype->tag) {
        case WASM_CORE_DEFTYPE_RECTYPE:
            if (deftype->type.rectype) {
                free_core_rectype(deftype->type.rectype);
                wasm_runtime_free(deftype->type.rectype);
            }
            break;
        case WASM_CORE_DEFTYPE_SUBTYPE:
            if (deftype->type.subtype) {
                free_core_module_subtype(deftype->type.subtype);
                wasm_runtime_free(deftype->type.subtype);
            }
            break;
        case WASM_CORE_DEFTYPE_MODULETYPE:
            if (deftype->type.moduletype) {
                free_core_moduletype(deftype->type.moduletype);
                wasm_runtime_free(deftype->type.moduletype);
            }
            break;
    }
}

void
free_core_type(WASMComponentCoreType *type)
{
    if (!type) {
        return;
    }

    if (type->deftype) {
        free_core_deftype(type->deftype);
        wasm_runtime_free(type->deftype);
    }
}

void
free_core_type_section(WASMComponentCoreTypeSection *section)
{
    if (!section) {
        return;
    }

    if (section->types) {
        for (uint32_t i = 0; i < section->count; i++) {
            free_core_type(&section->types[i]);
        }
        wasm_runtime_free(section->types);
    }
}

// Additional helper functions for freeing core structures
void
free_core_functype(WASMComponentCoreFuncType *functype)
{
    if (!functype) {
        return;
    }

    if (functype->params.val_types) {
        wasm_runtime_free(functype->params.val_types);
    }

    if (functype->results.val_types) {
        wasm_runtime_free(functype->results.val_types);
    }
}

void
free_core_rectype(WASMComponentCoreRecType *rectype)
{
    if (!rectype) {
        return;
    }

    if (rectype->subtypes) {
        for (uint32_t i = 0; i < rectype->subtype_count; i++) {
            free_core_subtype(&rectype->subtypes[i]);
        }
        wasm_runtime_free(rectype->subtypes);
    }
}

void
free_core_resulttype(WASMComponentCoreResultType *resulttype)
{
    if (!resulttype) {
        return;
    }

    if (resulttype->val_types) {
        wasm_runtime_free(resulttype->val_types);
    }
}

void
free_core_structtype(WASMComponentCoreStructType *structtype)
{
    if (!structtype) {
        return;
    }

    if (structtype->fields) {
        wasm_runtime_free(structtype->fields);
    }
}

void
free_core_comptype(WASMComponentCoreCompType *comptype)
{
    if (!comptype) {
        return;
    }

    switch (comptype->tag) {
        case WASM_CORE_COMPTYPE_FUNC:
            free_core_functype(&comptype->type.func_type);
            break;
        case WASM_CORE_COMPTYPE_STRUCT:
            if (comptype->type.struct_type.fields) {
                wasm_runtime_free(comptype->type.struct_type.fields);
            }
            break;
        case WASM_CORE_COMPTYPE_ARRAY:
            // Array type is simple, no additional memory to free
            break;
    }
}

void
free_core_subtype(WASMComponentCoreSubType *subtype)
{
    if (!subtype) {
        return;
    }

    if (subtype->supertypes) {
        wasm_runtime_free(subtype->supertypes);
    }

    // Free the comptype
    free_core_comptype(&subtype->comptype);
}

void
free_core_module_subtype(WASMComponentCoreModuleSubType *module_subtype)
{
    if (!module_subtype) {
        return;
    }

    if (module_subtype->supertypes) {
        wasm_runtime_free(module_subtype->supertypes);
    }

    if (module_subtype->comptype) {
        free_core_comptype(module_subtype->comptype);
        wasm_runtime_free(module_subtype->comptype);
    }
}
/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component.h"
#include "../interpreter/wasm.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "wasm_loader_common.h"
#include "wasm_runtime_common.h"
#include "wasm_export.h"
#include <stdio.h>

// Parse all sections in a WASM component binary
// Each section is dispatched to its own parser and stored in the output
// structure
bool
wasm_component_parse_sections(const uint8_t *buf, uint32_t size,
                              WASMComponent *out_component, LoadArgs *args,
                              unsigned int depth)
{
    if (!buf || size < 8 || !out_component) {
        return false;
    }

    // Decode the header first
    if (!wasm_decode_header(buf, size, &out_component->header)) {
        return false;
    }

    // const uint8_t *begin = buf;
    const uint8_t *p = buf + 8;
    const uint8_t *end = buf + size;
    uint32_t section_capacity = 8;

    WASMComponentSection *sections =
        wasm_runtime_malloc(section_capacity * sizeof(WASMComponentSection));
    if (!sections) {
        return false;
    }

    uint32_t section_count = 0;
    while (p < end) {
        // Read section id (1 byte) and payload length (LEB128)
        uint8_t section_id = *p++;
        char error_buf[128] = { 0 };

        // Read payload length
        uint64 payload_len = 0;
        if (!read_leb((uint8_t **)&p, end, 32, false, &payload_len, error_buf,
                      sizeof(error_buf))) {
            wasm_runtime_free(sections);
            return false; // Error handling
        }

        if ((uint32_t)(end - p) < payload_len) {
            wasm_runtime_free(sections);
            return false;
        }

        // Store section info
        if (section_count == section_capacity) {
            section_capacity *= 2;
            WASMComponentSection *new_sections = wasm_runtime_realloc(
                sections, section_capacity * sizeof(WASMComponentSection));
            if (!new_sections) {
                wasm_runtime_free(sections);
                return false;
            }
            sections = new_sections;
        }

        const uint8_t *payload_start = p;
        uint32_t current_section_index = section_count;
        sections[current_section_index].payload = payload_start;
        sections[current_section_index].payload_len = (uint32_t)payload_len;
        sections[current_section_index].id = section_id;
        sections[current_section_index].parsed.any =
            NULL; // Initialize parsed union to NULL
        section_count++;

        uint32_t consumed_len = 0;
        bool parse_success = false;

        // LOG_DEBUG("Parsing section: %d | Section size: %d | payload_start:
        // %d\n", section_id, payload_len, payload_start-begin);
        switch (section_id) {
            // Section 0: custom section
            case WASM_COMP_SECTION_CORE_CUSTOM:
            {
                // Parse custom section (name + data)
                WASMComponentCoreCustomSection *custom =
                    wasm_runtime_malloc(sizeof(WASMComponentCoreCustomSection));
                if (custom)
                    memset(custom, 0, sizeof(WASMComponentCoreCustomSection));

                parse_success = wasm_component_parse_core_custom_section(
                    &p, (uint32_t)payload_len, custom, error_buf,
                    sizeof(error_buf), &consumed_len);
                if (!parse_success || consumed_len != payload_len) {
                    if (custom) {
                        wasm_runtime_free(custom);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing custom section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Custom section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                else {
                    sections[current_section_index].parsed.core_custom = custom;
                }
                break;
            }
            // Section 1: module section
            case WASM_COMP_SECTION_CORE_MODULE:
            {
                // Parse and load the embedded core wasm module
                WASMComponentCoreModuleWrapper *module =
                    wasm_runtime_malloc(sizeof(WASMComponentCoreModuleWrapper));
                if (module)
                    memset(module, 0, sizeof(WASMComponentCoreModuleWrapper));
                parse_success = wasm_component_parse_core_module_section(
                    &payload_start, (uint32_t)payload_len, module, args,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (!parse_success || consumed_len != payload_len) {
                    if (module) {
                        wasm_runtime_free(module);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing module section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Module section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                else {
                    sections[current_section_index].parsed.core_module = module;
                }
                break;
            }
            // Section 2: instance section
            case WASM_COMP_SECTION_CORE_INSTANCE:
            {
                WASMComponentCoreInstSection *core_instance_section =
                    wasm_runtime_malloc(sizeof(WASMComponentCoreInstSection));
                if (core_instance_section)
                    memset(core_instance_section, 0,
                           sizeof(WASMComponentCoreInstSection));
                parse_success = wasm_component_parse_core_instance_section(
                    &payload_start, (uint32_t)payload_len,
                    core_instance_section, error_buf, sizeof(error_buf),
                    &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index]
                        .parsed.core_instance_section = core_instance_section;
                }
                else {
                    if (core_instance_section) {
                        wasm_runtime_free(core_instance_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing core instances section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Core Instances section "
                                  "consumed %u bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 3: core types section
            case WASM_COMP_SECTION_CORE_TYPE:
            {
                WASMComponentCoreTypeSection *core_type_section =
                    wasm_runtime_malloc(sizeof(WASMComponentCoreTypeSection));
                if (core_type_section)
                    memset(core_type_section, 0,
                           sizeof(WASMComponentCoreTypeSection));
                parse_success = wasm_component_parse_core_type_section(
                    &payload_start, (uint32_t)payload_len, core_type_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.core_type_section =
                        core_type_section;
                }
                else {
                    if (core_type_section) {
                        wasm_runtime_free(core_type_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing core types section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Core types section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 4: component section
            case WASM_COMP_SECTION_COMPONENT:
            {
                // Parse and load the embedded component
                WASMComponent *component =
                    wasm_runtime_malloc(sizeof(WASMComponent));
                if (!component) {
                    LOG_DEBUG(
                        "Failed to allocate memory for nested component\n");
                    wasm_runtime_free(sections);
                    return false;
                }
                // Initialize the component structure to avoid garbage data
                memset(component, 0, sizeof(WASMComponent));

                // Parse the nested component sections directly from the payload
                parse_success = wasm_component_parse_sections(
                    payload_start, (uint32_t)payload_len, component, args,
                    depth + 1);
                consumed_len = payload_len; // The entire payload is consumed

                if (!parse_success) {
                    LOG_DEBUG("    Failed to parse nested component, freeing "
                              "component at %p\n",
                              component);
                    wasm_runtime_free(component);
                    LOG_DEBUG("Error parsing sub component section\n");
                    wasm_runtime_free(sections);
                    return false;
                }
                else {
                    LOG_DEBUG(
                        "    Successfully parsed nested component at %p\n",
                        component);
                    sections[current_section_index].parsed.component =
                        component;
                }
                break;
            }
            // Section 5: instances section
            case WASM_COMP_SECTION_INSTANCES:
            {
                WASMComponentInstSection *instance_section =
                    wasm_runtime_malloc(sizeof(WASMComponentInstSection));
                if (instance_section)
                    memset(instance_section, 0,
                           sizeof(WASMComponentInstSection));
                parse_success = wasm_component_parse_instances_section(
                    &payload_start, (uint32_t)payload_len, instance_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.instance_section =
                        instance_section;
                }
                else {
                    if (instance_section) {
                        wasm_runtime_free(instance_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing instances section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Instances section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 6: aliases section for imports/exports
            case WASM_COMP_SECTION_ALIASES:
            {
                // Parse alias definitions for imports/exports
                WASMComponentAliasSection *alias_section =
                    wasm_runtime_malloc(sizeof(WASMComponentAliasSection));
                if (alias_section)
                    memset(alias_section, 0, sizeof(WASMComponentAliasSection));
                parse_success = wasm_component_parse_alias_section(
                    &payload_start, (uint32_t)payload_len, alias_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.alias_section =
                        alias_section;
                }
                else {
                    if (alias_section) {
                        wasm_runtime_free(alias_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing alias section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Alias section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 7: types section
            case WASM_COMP_SECTION_TYPE:
            {
                WASMComponentTypeSection *type_section =
                    wasm_runtime_malloc(sizeof(WASMComponentTypeSection));
                if (type_section)
                    memset(type_section, 0, sizeof(WASMComponentTypeSection));
                parse_success = wasm_component_parse_types_section(
                    &payload_start, (uint32_t)payload_len, type_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.type_section =
                        type_section;
                }
                else {
                    if (type_section) {
                        wasm_runtime_free(type_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing types section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Types section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 8: canons section
            case WASM_COMP_SECTION_CANONS:
            {
                WASMComponentCanonSection *canon_section =
                    wasm_runtime_malloc(sizeof(WASMComponentCanonSection));
                if (canon_section)
                    memset(canon_section, 0, sizeof(WASMComponentCanonSection));
                parse_success = wasm_component_parse_canons_section(
                    &payload_start, (uint32_t)payload_len, canon_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.canon_section =
                        canon_section;
                }
                else {
                    if (canon_section) {
                        wasm_runtime_free(canon_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing canons section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Canons section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 9: start section
            case WASM_COMP_SECTION_START:
            {
                WASMComponentStartSection *start_section =
                    wasm_runtime_malloc(sizeof(WASMComponentStartSection));
                if (start_section)
                    memset(start_section, 0, sizeof(WASMComponentStartSection));
                parse_success = wasm_component_parse_start_section(
                    &payload_start, (uint32_t)payload_len, start_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.start_section =
                        start_section;
                }
                else {
                    if (start_section) {
                        wasm_runtime_free(start_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing start section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Start section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 10: imports section (component model imports)
            case WASM_COMP_SECTION_IMPORTS:
            {
                // Parse all imports: name (simple/versioned) and externdesc
                // (all 6 types)
                WASMComponentImportSection *import_section =
                    wasm_runtime_malloc(sizeof(WASMComponentImportSection));
                if (import_section)
                    memset(import_section, 0,
                           sizeof(WASMComponentImportSection));
                parse_success = wasm_component_parse_imports_section(
                    &payload_start, (uint32_t)payload_len, import_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.import_section =
                        import_section;
                }
                else {
                    if (import_section) {
                        wasm_runtime_free(import_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing imports section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Imports section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 11: exports section
            case WASM_COMP_SECTION_EXPORTS:
            {
                WASMComponentExportSection *export_section =
                    wasm_runtime_malloc(sizeof(WASMComponentExportSection));
                if (export_section)
                    memset(export_section, 0,
                           sizeof(WASMComponentExportSection));
                parse_success = wasm_component_parse_exports_section(
                    &payload_start, (uint32_t)payload_len, export_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.export_section =
                        export_section;
                }
                else {
                    if (export_section) {
                        wasm_runtime_free(export_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing export section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Exports section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            // Section 12: values section
            case WASM_COMP_SECTION_VALUES:
            {
                WASMComponentValueSection *value_section =
                    wasm_runtime_malloc(sizeof(WASMComponentValueSection));
                if (value_section)
                    memset(value_section, 0, sizeof(WASMComponentValueSection));
                parse_success = wasm_component_parse_values_section(
                    &payload_start, (uint32_t)payload_len, value_section,
                    error_buf, sizeof(error_buf), &consumed_len);
                if (parse_success && consumed_len == payload_len) {
                    sections[current_section_index].parsed.value_section =
                        value_section;
                }
                else {
                    if (value_section) {
                        wasm_runtime_free(value_section);
                    }
                    if (error_buf[0]) {
                        LOG_DEBUG("Error parsing values section: %s\n",
                                  error_buf);
                    }
                    if (consumed_len != payload_len) {
                        LOG_DEBUG("FATAL ERROR: Values section consumed %u "
                                  "bytes but expected %lu\n",
                                  consumed_len, payload_len);
                        wasm_runtime_free(sections);
                        return false;
                    }
                }
                break;
            }
            default:
                // Unknown/unsupported section id
                LOG_DEBUG("FATAL ERROR: Unknown/unsupported section (id=%u, "
                          "size=%lu)\n",
                          section_id, payload_len);
                wasm_runtime_free(sections);
                return false;
        }

        // Advance the main parser by the consumed amount
        p = payload_start + consumed_len;

        // Safety check to ensure we don't go past the end
        if (p > end) {
            wasm_runtime_free(sections);
            return false;
        }
    }

    out_component->sections = sections;
    out_component->section_count = section_count;
    return true;
}

// Check if Header is Component
bool
is_wasm_component(WASMHeader header)
{
    if (header.magic != WASM_MAGIC_NUMBER
        || header.version != WASM_COMPONENT_VERSION
        || header.layer != WASM_COMPONENT_LAYER) {
        return false;
    }

    return true;
}

// Main component free function
void
wasm_component_free(WASMComponent *component)
{
    if (!component || !component->sections)
        return;

    for (uint32_t i = 0; i < component->section_count; ++i) {
        WASMComponentSection *sec = &component->sections[i];

        switch (sec->id) {
            case WASM_COMP_SECTION_CORE_CUSTOM: // Section 0
                wasm_component_free_core_custom_section(sec);
                break;

            case WASM_COMP_SECTION_CORE_MODULE: // Section 1
                wasm_component_free_core_module_section(sec);
                break;

            case WASM_COMP_SECTION_CORE_INSTANCE: // Section 2
                wasm_component_free_core_instance_section(sec);
                break;

            case WASM_COMP_SECTION_CORE_TYPE: // Section 3
                wasm_component_free_core_type_section(sec);
                break;

            case WASM_COMP_SECTION_COMPONENT: // Section 4
                wasm_component_free_component_section(sec);
                break;

            case WASM_COMP_SECTION_INSTANCES: // Section 5
                wasm_component_free_instances_section(sec);
                break;

            case WASM_COMP_SECTION_ALIASES: // Section 6
                wasm_component_free_alias_section(sec);
                break;

            case WASM_COMP_SECTION_TYPE: // Section 7
                wasm_component_free_types_section(sec);
                break;

            case WASM_COMP_SECTION_CANONS: // Section 8
                wasm_component_free_canons_section(sec);
                break;

            case WASM_COMP_SECTION_START: // Section 9
                wasm_component_free_start_section(sec);
                break;

            case WASM_COMP_SECTION_IMPORTS: // Section 10
                wasm_component_free_imports_section(sec);
                break;

            case WASM_COMP_SECTION_EXPORTS: // Section 11
                wasm_component_free_exports_section(sec);
                break;

            case WASM_COMP_SECTION_VALUES: // Section 12
                wasm_component_free_values_section(sec);
                break;

            default:
                // For other sections that don't have parsed data or are stubs
                // Just free the any pointer if it exists
                if (sec->parsed.any) {
                    wasm_runtime_free(sec->parsed.any);
                    sec->parsed.any = NULL;
                }
                break;
        }
    }

    wasm_runtime_free(component->sections);
    component->sections = NULL;
    component->section_count = 0;
}
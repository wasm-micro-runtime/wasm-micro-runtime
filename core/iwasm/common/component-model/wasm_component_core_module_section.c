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

// Section 1: module section
bool
wasm_component_parse_core_module_section(const uint8_t **payload,
                                         uint32_t payload_len,
                                         WASMComponentCoreModuleWrapper *out,
                                         LoadArgs *args, char *error_buf,
                                         uint32_t error_buf_size,
                                         uint32_t *consumed_len)
{
    if (consumed_len)
        *consumed_len = 0;
    if (!payload || !*payload || payload_len == 0 || !out) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "Invalid payload or output pointer");
        return false;
    }

    LOG_DEBUG("    Module section: embedded Core WebAssembly module\n");

    // Use the core wasm loader to parse the module
    wasm_module_t mod = wasm_runtime_load_ex((uint8 *)*payload, payload_len,
                                             args, error_buf, error_buf_size);
    if (!mod) {
        LOG_DEBUG("      Failed to load embedded core wasm module: %s\n",
                  error_buf);
        return false;
    }

    // Print some basic info about the embedded module
    LOG_DEBUG("      Types: %u function types\n",
              wasm_runtime_get_import_count(mod));
    LOG_DEBUG("      Exports: %u exports\n",
              wasm_runtime_get_export_count(mod));
#if WASM_ENABLE_INTERP != 0
    LOG_DEBUG("      Functions: %u functions\n",
              wasm_runtime_get_function_count(mod));
    LOG_DEBUG("      Tables: %u tables\n", wasm_runtime_get_table_count(mod));
    LOG_DEBUG("      Memories: %u memories\n",
              wasm_runtime_get_memories_count(mod));
    LOG_DEBUG("      Globals: %u globals\n",
              wasm_runtime_get_globals_count(mod));
#endif

    // Check if the module has imports
    int32_t import_count = wasm_runtime_get_import_count(mod);
    if (import_count > 0) {
        LOG_DEBUG("      Imports: %u imports\n", import_count);
        for (int32_t i = 0; i < import_count; i++) {
            wasm_import_t import;
            wasm_runtime_get_import_type(mod, i, &import);
            LOG_DEBUG(
                "        Import %u: module=\"%s\", name=\"%s\", kind=%u\n", i,
                import.module_name ? import.module_name : "<null>",
                import.name ? import.name : "<null>", import.kind);
        }
    }

    // Check if the module has exports
    int32_t export_count = wasm_runtime_get_export_count(mod);
    if (export_count > 0) {
        LOG_DEBUG("      Exports: %u exports\n", export_count);
        for (int32_t i = 0; i < export_count; i++) {
            wasm_export_t export;
            wasm_runtime_get_export_type(mod, i, &export);
            LOG_DEBUG("        Export %u: name=\"%s\", kind= %u\n", i,
                      export.name ? export.name : "<null>", export.kind);

            // Print more details about the export
            if (export.name && strlen(export.name) == 0) {
                LOG_DEBUG("          WARNING: Empty field name - this will "
                          "cause 'unknown export' error\n");
            }
        }
    }

    // Store the module pointer directly instead of copying
    out->module_handle = (void *)mod;
    out->module = NULL; // We don't need the actual module structure for now
    if (consumed_len)
        *consumed_len = payload_len;
    return true;
}

// Individual section free functions
void
wasm_component_free_core_module_section(WASMComponentSection *section)
{
    if (!section || !section->parsed.core_module)
        return;

    // Use the proper wasm_runtime_unload function to free the module
    if (section->parsed.core_module->module_handle) {
        wasm_runtime_unload(
            (wasm_module_t)section->parsed.core_module->module_handle);
        section->parsed.core_module->module_handle = NULL;
    }
    wasm_runtime_free(section->parsed.core_module);
    section->parsed.core_module = NULL;
}
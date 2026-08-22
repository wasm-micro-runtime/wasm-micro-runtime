/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef HELPERS_H
#define HELPERS_H

#include "wasm_export.h"
#include "wasm_component.h"
#include "wasm_component_runtime.h"
#include "wasm_memory.h"
#include <string>
#include <unordered_map>
#include <cstring>
#include <vector>
#include <stdint.h>

#define HEAP_SIZE (100 * 1024 * 1024) // 100 MB

class ComponentHelper
{
  public:
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;
    WASMComponent *component = NULL;
    WASMComponentInstance *component_inst = NULL;

    uint32_t wasm_file_size = 0;
    uint32_t stack_size = 16 * 1024; // 16 KB
    uint32_t heap_size = HEAP_SIZE;  // 100 MB

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    bool component_init = false;
    bool component_instantiated = false;

    std::unordered_map<std::string, uint32_t> offsets; // Memory offsets

    ComponentHelper();

    bool read_wasm_file(const char *wasm_file);
    bool load_component();
    bool instantiate_component();

    // Helpers for tests
    uint32_t get_section_count() const;
    bool is_loaded() const;
    void reset_component();

    void do_setup();
    void do_teardown();

    std::vector<WASMComponentSection *> get_section(
        WASMComponentSectionType section_id) const;

    std::vector<WASMMemoryInstance *> get_memories() const;

    void load_memory_offsets(
        const std::string
            &filename); // Loading the memory offsets from text file
    uint32_t get_memory_offsets(
        const std::string &key); // Get memory offsets from map
};
#endif

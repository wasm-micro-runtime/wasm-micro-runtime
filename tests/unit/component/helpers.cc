/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "helpers.h"
#include "wasm_component.h"
#include "bh_read_file.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

ComponentHelper::ComponentHelper() {}

void
ComponentHelper::do_setup()
{
    printf("Starting setup\n");
    memset(&init_args, 0, sizeof(RuntimeInitArgs));

    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

    if (!wasm_runtime_full_init(&init_args)) {
        printf("Failed to initialize WAMR runtime.\n");
        runtime_init = false;
    } else {
        runtime_init = true;
    }
    
    printf("Ending setup\n");
}

void
ComponentHelper::do_teardown()
{
    printf("Starting teardown\n");

    if (component_instantiated) {
        printf("Starting to deinstantiate component\n");
        wasm_component_deinstantiate(this->component_inst);
        component_inst = NULL;
        component_instantiated = false;
    }

    if (component_init) {
        printf("Starting to unload component\n");
        if (component) {
            wasm_component_free(component);
            component = NULL;
        }
        if (component_raw) {
            BH_FREE(component_raw);
            component_raw = NULL;
        }
        component_init = false;
    }

    if (runtime_init) {
        printf("Starting to destroy runtime\n");
        wasm_runtime_destroy();
        runtime_init = false;
    }

    printf("Ending teardown\n");
}

bool
ComponentHelper::read_wasm_file(const char *wasm_file) {
    const char *file = wasm_file;

    printf("Reading wasm component\n");
    component_raw =
        (unsigned char *)bh_read_file_to_buffer(file, &wasm_file_size);
    if (!component_raw) {
        printf("Failed to read wasm component from file\n");
        return false;
    }

    printf("Loaded wasm component size: %u\n", wasm_file_size);
    return true;
}

bool
ComponentHelper::load_component()
{
    printf("Loading wasm component in memory\n");
    
    // Allocate component structure if not already allocated
    if (!component) {
        component = (WASMComponent *)wasm_runtime_malloc(sizeof(WASMComponent));
        if (!component) {
            printf("Failed to allocate component structure\n");
            return false;
        }
        memset(component, 0, sizeof(WASMComponent));
    }
    
    // First decode the header
    if (!wasm_decode_header(component_raw, wasm_file_size, &component->header)) {
        printf("Not a valid WASM component file (header mismatch).\n");
        BH_FREE(component_raw);
        component_raw = NULL;
        wasm_runtime_free(component);
        component = NULL;
        return false;
    }

    // Second check if it's a valid component
    if (!is_wasm_component(component->header)) {
        printf("Not a valid WASM component file (header mismatch).\n");
        BH_FREE(component_raw);
        component_raw = NULL;
        wasm_runtime_free(component);
        component = NULL;
        return false;
    }
    
    // Parse the component sections
    LoadArgs load_args = {0, false, false, false, false};
    char name_buf[32];
    std::memset(name_buf, 0, sizeof(name_buf));
    std::snprintf(name_buf, sizeof(name_buf), "%s", "Test Component");
    load_args.name = name_buf; // provide non-null, mutable name as required by loader
    load_args.wasm_binary_freeable = false;
    load_args.clone_wasm_binary = false;
    load_args.no_resolve = false;
    load_args.is_component = true;
    
    if (!wasm_component_parse_sections(component_raw, wasm_file_size, component, &load_args, 0)) {
        printf("Failed to parse WASM component sections.\n");
        BH_FREE(component_raw);
        component_raw = NULL;
        wasm_runtime_free(component);
        component = NULL;
        return false;
    }
    
    printf("Component loaded successfully with %u sections\n", component->section_count);
    component_init = true;

    printf("Finished to load wasm component\n");
    return true;
}

uint32_t ComponentHelper::get_section_count() const {
    if (!component) {
        return 0;
    }
    return component->section_count;
}

bool ComponentHelper::is_loaded() const {
    return component_init && component && component->section_count > 0;
}

void ComponentHelper::reset_component() {
    if (component) {
        wasm_component_free(component);
        component = NULL;
    }
    component_init = false;
}

std::vector<WASMComponentSection*> ComponentHelper::get_section(WASMComponentSectionType section_id) const {
    if (section_id < 0) return {};

    if (!component) return {};

    std::vector<WASMComponentSection*> sections;

    for(uint32_t i = 0; i < component->section_count; i++) {
        if (component->sections[i].id == section_id) {
            sections.push_back(&component->sections[i]);
        }
    }

    return sections;
}

std::vector<WASMMemoryInstance *>
ComponentHelper::get_memories() const
{
    std::vector<WASMMemoryInstance*> vec;

    for (uint32 i = 0; i < this->component_inst->core_memories_count; i++) {
        vec.push_back(this->component_inst->core_memories[i]);
    }

    return vec;
}

bool ComponentHelper::instantiate_component() {
    struct InstantiationArgs2 *inst_args;
    if (!wasm_runtime_instantiation_args_create(&inst_args)) {
        return false;
    }

    wasm_runtime_instantiation_args_set_default_stack_size(inst_args, stack_size);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(inst_args, heap_size);

    this->component_inst = wasm_component_instantiate(this->component, this->error_buf, 128);

    wasm_runtime_instantiation_args_destroy(inst_args);

    if (!this->component_inst) {
        return false;
    }

    component_instantiated = true;
    return true;
}

void ComponentHelper::load_memory_offsets(const std::string& filename){
std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open layout file: " + filename);
    }

    std::string line;
    while (std::getline(file, line)) {
        line.erase(std::remove(line.begin(), line.end(), '"'), line.end());

        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string segment;

        while (std::getline(ss, segment, ',')) {
            size_t eqPos = segment.find('=');
            if (eqPos != std::string::npos) {

                std::string key = segment.substr(0, eqPos);
                
                key.erase(0, key.find_first_not_of(" \t\n\r"));

                std::string valueStr = segment.substr(eqPos + 1);
                uint32_t value = std::stoul(valueStr);

                // Store
                offsets[key] = value;
            }
        }
    }
}

uint32_t ComponentHelper::get_memory_offsets(const std::string& key) {
    if (offsets.find(key) == offsets.end()) {
        throw std::runtime_error("Key not found in layout: " + key);
    }
    return offsets[key];
}

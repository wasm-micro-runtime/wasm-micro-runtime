/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef HELPERS_H
#define HELPERS_H

#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>

extern "C" {
#include "wasm_component.h"
#include "wasm_runtime.h"
#include "bh_read_file.h"
#include "wasm_component_runtime.h"
}

// -------- helpers to inspect real components --------

bool
component_has_core_imports(const WASMComponent *c);

bool
component_has_section(const WASMComponent *c, uint8_t id);

bool
component_is_simple_enough(const WASMComponent *c, const char **why);

WASMComponent *
load_component_from_candidates(const char *file_name);

WASMComponent *
load_component_from_candidates_internal(const char *file_name,
                                        const char *test_dir_name);

void
print_component_sections(WASMComponent *comp, uint32 level);

void
print_core_module_imports(WASMModule *core_module, uint32 level);

const char *
section_name(uint8_t id);

unsigned char *
load_wasm(uint32_t &out_size, const char *&used_path);

const char *
core_kind_name(uint8_t kind);

WASMComponent *
parse_component(const unsigned char *buf, uint32_t size);

#endif
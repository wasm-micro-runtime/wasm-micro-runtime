/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef TEST_WASM_COMPONENT_H
#define TEST_WASM_COMPONENT_H

#include "wasm_component.h"
#include "wasm_component_runtime.h"

extern WASMComponent test_component;
extern WASMComponent test_component_2;
extern WASMComponent test_component_3;
extern WASMComponent test_component_INVALID;
extern WASMComponentIndexCount test_index_count;
extern WASMComponentInstArgInstances inst_args;
extern WASMModuleInstance core_inst;
extern WASMFunctionInstance dummy_core_func;
extern WASMFunctionInstance dummy_core_func_2;
extern WASMGlobalInstance dummy_core_global;
extern WASMTableInstance dummy_core_table;
extern WASMMemoryInstance dummy_core_memory;
extern WASMModule test_core_module;

#endif // TEST_WASM_COMPONENT_H

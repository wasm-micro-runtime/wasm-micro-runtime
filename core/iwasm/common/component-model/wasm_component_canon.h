/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_CANON_H
#define WASM_COMPONENT_CANON_H

#include "wasm_component_flat.h"
#include "wasm_component_resource_table.h"
#include "wasm_component_resource.h"

bool
canon_resource_new(WASMComponentResourceInstance *rt,
                   WASMComponentInstance *inst, uint32_t rep,
                   uint32_t *out_index);

bool
canon_resource_rep(const WASMComponentResourceInstance *rt,
                   WASMComponentInstance *inst, uint32_t handle_index,
                   uint32_t *out_rep);

bool
canon_resource_drop(const WASMComponentResourceInstance *rt,
                    WASMComponentInstance *inst, uint32_t handle_index);

#endif

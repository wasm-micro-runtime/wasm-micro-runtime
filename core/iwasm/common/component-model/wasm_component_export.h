/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_EXPORT_H
#define WASM_COMPONENT_EXPORT_H

#include "stdbool.h"

bool
is_component_runtime();
void
set_component_runtime(bool type);

#endif
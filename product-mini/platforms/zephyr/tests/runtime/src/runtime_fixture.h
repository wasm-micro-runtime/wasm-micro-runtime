/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_RUNTIME_FIXTURE_H
#define WAMR_ZEPHYR_RUNTIME_FIXTURE_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#include "wasm_export.h"

#define RUNTIME_POOL_SIZE (128U * 1024U)

struct loaded_runtime {
    uint8_t pool[RUNTIME_POOL_SIZE] __aligned(8);
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;
};

bool
runtime_start(struct loaded_runtime *rt, const uint8_t *bytes, uint32_t size,
              char *error, uint32_t error_size);
void
runtime_stop(struct loaded_runtime *rt);

#endif /* WAMR_ZEPHYR_RUNTIME_FIXTURE_H */

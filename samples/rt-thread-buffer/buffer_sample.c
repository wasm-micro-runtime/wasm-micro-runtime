/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "rt_wamr.h"

/* (module (func (export "add") (param i32 i32) (result i32)
 *   local.get 0 local.get 1 i32.add)) */
static const uint8_t add_module[] = { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00,
                                      0x00, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7f,
                                      0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00,
                                      0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64,
                                      0x00, 0x00, 0x0a, 0x09, 0x01, 0x07, 0x00,
                                      0x20, 0x00, 0x20, 0x01, 0x6a, 0x0b };

/* Compile this file into an RT application and call from thread context. */
int
wamr_buffer_sample(void)
{
    struct rt_wamr_config config;
    struct rt_wamr_result status;
    wasm_val_t args[2] = { 0 }, result = { 0 };
    struct rt_wamr_request request = { 0 };
    rt_wamr_config_init(&config);
    args[0].kind = args[1].kind = WASM_I32;
    args[0].of.i32 = 20;
    args[1].of.i32 = 22;
    request.buffer = add_module;
    request.size = sizeof(add_module);
    request.function = "add";
    request.argument_count = 2;
    request.arguments = args;
    request.result_count = 1;
    request.results = &result;
    if (rt_wamr_run_buffer(&request, &config, &status) != RT_EOK) {
        rt_kprintf("WAMR: %s\n", status.error);
        return status.status;
    }
    return result.kind == WASM_I32 && result.of.i32 == 42 ? RT_EOK : -RT_ERROR;
}

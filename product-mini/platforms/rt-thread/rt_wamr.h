/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef RT_WAMR_H
#define RT_WAMR_H

#include <rtthread.h>
#include "wasm_export.h"

struct rt_wamr_config {
    uint32_t native_stack_size;
    uint32_t operand_stack_size;
    uint32_t host_heap_size;
    uint16_t priority;
    const char *native_module;
    NativeSymbol *native_symbols;
    uint32_t native_symbol_count;
};

struct rt_wamr_request {
    const uint8_t *buffer;
    uint32_t size;
    const char *function;
    uint32_t argument_count;
    wasm_val_t *arguments;
    uint32_t result_count;
    wasm_val_t *results;
};

struct rt_wamr_result {
    int status;
    char error[128];
};

/* Initialize defaults: 16 KiB native/operand stacks, no extra guest heap. */
void
rt_wamr_config_init(struct rt_wamr_config *config);

/* Synchronous, thread-context-only API. All request/config storage remains
   owned by the caller and must stay valid until return. The worker copies the
   input to writable memory, retains it until unload, and returns only after
   WAMR cleanup. One run at a time; reentrant/concurrent calls return -RT_EBUSY.
   NativeSymbol arrays must be writable. No forced thread cancellation.
   This API owns the process-wide WAMR lifecycle; do not mix it with a
   separately initialized runtime or the legacy file-based iwasm command. */
int
rt_wamr_run_buffer(const struct rt_wamr_request *request,
                   const struct rt_wamr_config *config,
                   struct rt_wamr_result *result);

#endif /* RT_WAMR_H */

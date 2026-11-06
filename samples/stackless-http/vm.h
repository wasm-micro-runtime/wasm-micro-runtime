/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_VM_H
#define STACKLESS_VM_H
#include "p3.h"
#ifdef P3_WAMR
#include "wasm_c_api.h"
#else
#include "wasm.h"
#endif
#ifndef P3_IMPORTS
#define P3_IMPORTS 48
#endif
#define P3_EXPORTS 32
typedef struct p3_vm p3_vm;
typedef struct p3_task p3_task;
typedef struct p3_binding {
    p3_vm *vm;
    enum p3_op op;
    uint8_t future, element, async_;
#ifdef P3_WASI_HTTP
    uint8_t http_op;
#endif
#ifdef P3_WASI_HTTP_HOSTED
    char unsupported[96];
#endif
} p3_binding;
typedef struct p3_export {
    wasm_func_t *func;
    uint8_t argc, result;
} p3_export;
struct p3_vm {
    p3_context async;
    wasm_engine_t *engine;
    wasm_store_t *store;
    wasm_module_t *module;
    wasm_instance_t *instance;
    wasm_extern_vec_t exports;
    wasm_exporttype_vec_t export_types;
    wasm_memory_t *memory;
    p3_binding bindings[P3_IMPORTS];
    p3_export functions[P3_EXPORTS];
    char error[256];
    p3_task *current;
    bool poisoned;
#ifdef P3_WASI_HTTP
    struct wasi_http *http;
#endif
};
struct p3_task {
    p3_vm *vm;
    const p3_export *callback;
    uint32_t set, context, body, completion;
    bool started, returned, cancel_requested, cancel_delivered, cancelled;
    bool exited, yielded, failed;
    uint32_t callbacks;
#ifdef P3_WASI_HTTP
    uint32_t http_result;
#endif
};
bool
p3_vm_open(p3_vm *vm, const uint8_t *bytes, size_t size);
bool
p3_vm_open_with_engine(p3_vm *vm, wasm_engine_t *engine, const uint8_t *bytes,
                       size_t size);
void
p3_vm_close(p3_vm *vm);
/* Synchronous core calls only. Task state, streams and futures outlive calls.
 */
bool
p3_vm_call(p3_vm *vm, const char *name, const int32_t *args, size_t argc,
           int32_t *result, bool *has_result);
const p3_export *
p3_vm_export(p3_vm *vm, const char *name);
bool
p3_vm_invoke(p3_vm *vm, const p3_export *entry, const int32_t *args,
             size_t argc, int32_t *result, bool *has_result);
void
p3_task_init(p3_task *task, p3_vm *vm, const char *callback);
bool
p3_task_start(p3_task *task, const char *start, const int32_t *args,
              size_t argc);
bool
p3_task_ready(p3_task *task);
bool
p3_task_step(p3_task *task);
bool
p3_task_request_cancel(p3_task *task);
#endif

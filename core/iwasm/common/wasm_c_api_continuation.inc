/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/* Optional C API continuation implementation; included only by wasm_c_api.c. */
#include "../include/wasm_ext.h"
#include "../interpreter/wasm_interp.h"
#if WASM_ENABLE_C_API_DIAGNOSTICS != 0
#include "../include/wasm_ext_debug.h"
#endif
#ifndef os_thread_local_attribute
#error "Experimental continuations require thread-local storage"
#endif
typedef struct ExtHost {
    struct ExtHost *next;
    wasm_store_t *store;
    wasm_functype_t *type;
    wasm_ext_callback_t callback;
    void *env;
    wasm_ext_cancel_t cancel;
} ExtHost;
struct wasm_ext_call_t {
    const wasm_func_t *function;
    WASMExecEnv *exec_env;
    uint32 *argv;
    uint32 argc;
    ExtHost *pending;
    int status;
    bool started;
    bool running;
#if WASM_ENABLE_C_API_DIAGNOSTICS != 0
    wasm_ext_debug_observer_t observer;
    void *debug_env;
    wasm_ext_debug_snapshot_t snapshot;
#endif
};
static os_thread_local_attribute wasm_ext_call_t *ext_current;
#if WASM_ENABLE_C_API_DIAGNOSTICS != 0
#include "wasm_c_api_debug.inc"
#else
#define ext_debug_event(call, event, capture) ((void)0)
#endif
static bool
ext_numeric(const wasm_functype_t *type)
{
    size_t i;
    if (type->results->num_elems > 1)
        return false;
    for (i = 0; i < type->params->num_elems; i++)
        if (type->params->data[i]->kind > WASM_F64)
            return false;
    for (i = 0; i < type->results->num_elems; i++)
        if (type->results->data[i]->kind > WASM_F64)
            return false;
    return true;
}
static bool
ext_values(const wasm_val_vec_t *values, const wasm_valtype_vec_t *types)
{
    size_t i;
    if (!values || values->size != types->num_elems
        || (values->size && !values->data))
        return false;
    for (i = 0; i < values->size; i++)
        if (values->data[i].kind != types->data[i]->kind)
            return false;
    return true;
}
/* Packed i32/i64 parameter sequences need not be naturally aligned. */
static uint32
ext_pack(const wasm_val_vec_t *values, uint32 *cells)
{
    size_t i;
    uint32 count = 0;
    for (i = 0; i < values->size; i++) {
        uint32 width =
            values->data[i].kind == WASM_I64 || values->data[i].kind == WASM_F64
                ? 8
                : 4;
        memcpy(cells + count, &values->data[i].of, width);
        count += width / 4;
    }
    return count;
}
static wasm_trap_t *
ext_trap(wasm_store_t *store, const char *text)
{
    wasm_name_t message;
    wasm_trap_t *trap;
    wasm_name_new_from_string_nt(&message, text);
    trap = wasm_trap_new(store, &message);
    wasm_byte_vec_delete(&message);
    return trap;
}
static wasm_trap_t *
ext_host(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    ExtHost *host = env;
    wasm_trap_t *trap = NULL;
    wasm_ext_status_t status;
    wasm_val_t *data = results->data;
    size_t size = results->size;
    if (!ext_current || ext_current->function->store != host->store)
        return ext_trap(host->store,
                        "async host requires an extension invocation");
    status = host->callback(host->env, args, results, &trap);
    if (results->data != data || results->size != size)
        abort(); /* C callback violated borrowed-buffer ownership. */
    if (status == WASM_EXT_PENDING && !trap) {
        ext_debug_event(ext_current, WASM_EXT_DEBUG_PENDING, false);
        ext_current->pending = host;
        ext_current->exec_env->ext_pending = true;
        return NULL;
    }
    if (status != WASM_EXT_DONE || trap)
        return trap ? trap : ext_trap(host->store, "host callback failed");
    if (!ext_values(results, host->type->results))
        return ext_trap(host->store, "host result type mismatch");
    return NULL;
}
static void
ext_host_delete(void *env)
{
    ExtHost *host = env;
    wasm_functype_delete(host->type);
    wasm_runtime_free(host);
}
static void
ext_store_delete(wasm_store_t *store)
{
    ExtHost *host = store->ext_hosts;
    while (host) {
        ExtHost *next = host->next;
        ext_host_delete(host);
        host = next;
    }
}
wasm_func_t *
wasm_ext_func_new(wasm_store_t *store, const wasm_functype_t *type,
                  wasm_ext_callback_t callback, void *env,
                  wasm_ext_cancel_t cancel)
{
    ExtHost *host;
    wasm_func_t *func;
    if (!store || store->ext_active || !type || !callback || !ext_numeric(type))
        return NULL;
    host = malloc_internal(sizeof(*host));
    if (!host)
        return NULL;
    host->next = NULL;
    host->store = store;
    host->type = wasm_functype_copy(type);
    host->callback = callback;
    host->env = env;
    host->cancel = cancel;
    if (!host->type) {
        wasm_runtime_free(host);
        return NULL;
    }
    func = wasm_func_new_with_env(store, type, ext_host, host, NULL);
    if (!func)
        ext_host_delete(host);
    else {
        host->next = store->ext_hosts;
        store->ext_hosts = host;
    }
    return func;
}
wasm_ext_call_t *
wasm_ext_call_new(const wasm_func_t *func, const wasm_val_vec_t *args)
{
    wasm_ext_call_t *call;
    size_t count;
    if (!func || !func->inst_comm_rt || func->store->ext_active
        || func->inst_comm_rt->module_type != Wasm_Module_Bytecode
        || !ext_numeric(func->type) || !ext_values(args, func->type->params))
        return NULL;
    call = malloc_internal(sizeof(*call));
    if (!call)
        return NULL;
    memset(call, 0, sizeof(*call));
    count = func->param_count > func->result_count ? func->param_count
                                                   : func->result_count;
    if (count > (UINT32_MAX / 8) - 1) {
        wasm_runtime_free(call);
        return NULL;
    }
    call->argv = malloc_internal((uint32)(count + 1) * 8);
    call->exec_env = wasm_runtime_create_exec_env(
        func->inst_comm_rt,
        ((WASMModuleInstance *)func->inst_comm_rt)->default_wasm_stack_size);
    if (!call->argv || !call->exec_env) {
        if (call->argv)
            wasm_runtime_free(call->argv);
        if (call->exec_env)
            wasm_runtime_destroy_exec_env(call->exec_env);
        wasm_runtime_free(call);
        return NULL;
    }
    call->function = wasm_func_copy(func);
    if (!call->function) {
        wasm_runtime_destroy_exec_env(call->exec_env);
        wasm_runtime_free(call->argv);
        wasm_runtime_free(call);
        return NULL;
    }
    if (args->size)
        call->argc = ext_pack(args, call->argv);
    call->status = WASM_EXT_PENDING;
    func->store->ext_active = call;
    return call;
}
void
wasm_ext_call_cancel(wasm_ext_call_t *call)
{
    if (!call || call->running || call->status == WASM_EXT_CANCELLED
        || call->function->store->ext_active != call)
        return;
    ext_debug_event(call, WASM_EXT_DEBUG_CANCEL, false);
    call->status = WASM_EXT_CANCELLED;
    call->running = true;
    if (call->pending) {
        ExtHost *host = call->pending;
        call->pending = NULL;
        if (host->cancel)
            host->cancel(host->env);
    }
    wasm_interp_ext_cancel(call->exec_env);
    call->running = false;
    call->function->store->ext_active = NULL;
}
void
wasm_ext_call_delete(wasm_ext_call_t *call)
{
    if (!call || call->running)
        return;
    wasm_ext_call_cancel(call);
    wasm_func_delete((wasm_func_t *)call->function);
    if (((WASMModuleInstance *)call->exec_env->module_inst)->cur_exec_env
        == call->exec_env)
        ((WASMModuleInstance *)call->exec_env->module_inst)->cur_exec_env =
            NULL;
    wasm_runtime_destroy_exec_env(call->exec_env);
    wasm_runtime_free(call->argv);
    wasm_runtime_free(call);
}
wasm_ext_status_t
wasm_ext_call_resume(wasm_ext_call_t *call, const wasm_val_vec_t *completion,
                     wasm_val_vec_t *results, wasm_trap_t **out_trap)
{
    const wasm_func_t *func;
    wasm_ext_call_t *previous;
    bool ok;
    uint32 values[2] = { 0 }, cells = 0;
    if (!out_trap)
        return WASM_EXT_ERROR;
    *out_trap = NULL;
    if (!call || call->running)
        return WASM_EXT_ERROR;
    if (call->status == WASM_EXT_CANCELLED)
        return WASM_EXT_CANCELLED;
    func = call->function;
    if (func->store->ext_active != call || !results
        || results->size != func->type->results->num_elems
        || (results->size && !results->data))
        return WASM_EXT_ERROR;
    if (call->started) {
        if (!call->pending)
            return WASM_EXT_ERROR;
        if (!completion)
            return WASM_EXT_PENDING;
        if (!ext_values(completion, call->pending->type->results))
            return WASM_EXT_ERROR;
        if (completion->size)
            cells = ext_pack(completion, values);
        call->pending = NULL;
    }
    else if (completion)
        return WASM_EXT_ERROR;
    call->running = true;
    ext_debug_event(
        call, call->started ? WASM_EXT_DEBUG_RESUME : WASM_EXT_DEBUG_START,
        false);
    previous = ext_current;
    ext_current = call;
    if (!call->started) {
        WASMModuleInstance *inst = (WASMModuleInstance *)func->inst_comm_rt;
        call->started = true;
        /* Match ordinary wasm_func_call: a prior trap must not poison a new
         * invocation. */
        wasm_runtime_set_exception(func->inst_comm_rt, NULL);
        ok = wasm_call_function(call->exec_env,
                                inst->e->functions + func->func_idx_rt,
                                call->argc, call->argv);
    }
    else {
        wasm_interp_ext_resume(call->exec_env, cells, values,
                               ((WASMModuleInstance *)func->inst_comm_rt)
                                   ->e->functions[func->func_idx_rt]
                                   .ret_cell_num,
                               call->argv);
        ok = !wasm_runtime_get_exception(func->inst_comm_rt);
    }
    ext_current = previous;
    call->running = false;
    if (call->pending && ok)
        return WASM_EXT_PENDING;
    if (!ok) {
        *out_trap = wasm_trap_new_internal(
            func->store, func->inst_comm_rt,
            wasm_runtime_get_exception(func->inst_comm_rt), NULL);
        wasm_interp_ext_cancel(call->exec_env);
        call->status = WASM_EXT_ERROR;
    }
    else {
        if (results->size)
            argv_to_results(call->argv, func->type->results, results);
        ext_debug_event(call, WASM_EXT_DEBUG_DONE, false);
        call->status = WASM_EXT_DONE;
    }
    func->store->ext_active = NULL;
    return call->status;
}

wasm_ext_status_t
wasm_ext_call_fail(wasm_ext_call_t *call, const wasm_name_t *message,
                   wasm_trap_t **out_trap)
{
    if (!out_trap)
        return WASM_EXT_ERROR;
    *out_trap = NULL;
    if (!call || call->running || !call->pending || !message
        || (message->size && !message->data)
        || call->function->store->ext_active != call)
        return WASM_EXT_ERROR;
    *out_trap = wasm_trap_new(call->function->store, message);
    if (!*out_trap)
        return WASM_EXT_ERROR;
    call->running = true;
    ext_debug_event(call, WASM_EXT_DEBUG_ASYNC_ERROR, false);
    call->pending = NULL;
    wasm_interp_ext_cancel(call->exec_env);
    call->status = WASM_EXT_ERROR;
    call->running = false;
    call->function->store->ext_active = NULL;
    return WASM_EXT_ERROR;
}

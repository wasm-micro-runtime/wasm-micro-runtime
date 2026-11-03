/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "vm.h"
#include <stdio.h>
#include <string.h>

static bool
name_is(const wasm_name_t *n, const char *s)
{
    size_t length = strlen(s);
    return (n->size == length || (n->size == length + 1 && !n->data[length]))
           && !memcmp(n->data, s, length);
}
static uint8_t *
memory(void *env, size_t *size)
{
    p3_vm *vm = env;
    if (!vm->memory) {
        *size = 0;
        return NULL;
    }
    *size = wasm_memory_data_size(vm->memory);
    return (uint8_t *)wasm_memory_data(vm->memory);
}
static wasm_trap_t *
trap(p3_vm *vm, const char *message)
{
    vm->poisoned = true;
    snprintf(vm->error, sizeof(vm->error), "%s", message);
    wasm_message_t text;
    wasm_byte_vec_new(&text, strlen(message), message);
    wasm_trap_t *t = wasm_trap_new(vm->store, &text);
    wasm_byte_vec_delete(&text);
    return t;
}
static void
trap_message(p3_vm *vm, wasm_trap_t *t)
{
    wasm_message_t message;
    wasm_trap_message(t, &message);
    size_t n = message.size < sizeof(vm->error) - 1 ? message.size
                                                    : sizeof(vm->error) - 1;
    memcpy(vm->error, message.data, n);
    vm->error[n] = 0;
    wasm_byte_vec_delete(&message);
    wasm_trap_delete(t);
}
static wasm_trap_t *
canonical(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    p3_binding *b = env;
    if (b->op == P3_TASK_RETURN || b->op == P3_TASK_CANCEL) {
        p3_task *task = b->vm->current;
        if (!task || results->size || task->returned || task->cancelled)
            return trap(b->vm, "invalid task resolution");
        if (b->op == P3_TASK_CANCEL) {
            if (args->size || !task->cancel_delivered)
                return trap(b->vm, "cancellation was not delivered");
            task->cancelled = true;
        }
        else {
            if (args->size == 2 && args->data[0].kind == WASM_I32
                && args->data[1].kind == WASM_I32) {
                uint32_t body = (uint32_t)args->data[0].of.i32,
                         done = (uint32_t)args->data[1].of.i32;
                if (!p3_lift_pair(&b->vm->async, body, done))
                    return trap(b->vm, b->vm->async.error);
                task->body = body;
                task->completion = done;
            }
            else if (args->size)
                return trap(b->vm, "unsupported task result type");
            task->returned = true;
        }
        return NULL;
    }
    if (b->op == P3_CONTEXT_GET || b->op == P3_CONTEXT_SET) {
        p3_task *task = b->vm->current;
        if (!task)
            return trap(b->vm, "no current stackless task");
        if (b->op == P3_CONTEXT_GET && !args->size && results->size == 1) {
            results->data[0].kind = WASM_I32;
            results->data[0].of.i32 = (int32_t)task->context;
        }
        else if (b->op == P3_CONTEXT_SET && args->size == 1 && !results->size
                 && args->data[0].kind == WASM_I32)
            task->context = (uint32_t)args->data[0].of.i32;
        else
            return trap(b->vm, "invalid context access");
        return NULL;
    }
    uint32_t a[3] = { 0, 0, 0 };
    if (args->size > 3 || results->size > 1)
        return trap(b->vm, "invalid canonical signature");
    for (size_t i = 0; i < args->size; ++i) {
        if (args->data[i].kind != WASM_I32)
            return trap(b->vm, "invalid canonical argument");
        a[i] = (uint32_t)args->data[i].of.i32;
    }
    uint64_t value;
    if (!p3_call(&b->vm->async, b->op, b->future != 0, b->element,
                 b->async_ != 0, a[0], a[1], a[2], &value))
        return trap(b->vm, b->vm->async.error);
    if (results->size) {
        if (b->op == P3_NEW) {
            results->data[0].kind = WASM_I64;
            results->data[0].of.i64 = (int64_t)value;
        }
        else {
            results->data[0].kind = WASM_I32;
            results->data[0].of.i32 = (int32_t)value;
        }
    }
    return NULL;
}
static bool
binding(p3_binding *b, const wasm_name_t *name)
{
    static const struct {
        const char *name;
        enum p3_op op;
    } names[] = { { "new", P3_NEW },
                  { "read", P3_READ },
                  { "write", P3_WRITE },
                  { "cancel-read", P3_CANCEL_READ },
                  { "cancel-write", P3_CANCEL_WRITE },
                  { "drop-readable", P3_DROP_READ },
                  { "drop-writable", P3_DROP_WRITE },
                  { "waitable-set.new", P3_SET_NEW },
                  { "waitable-set.poll", P3_SET_POLL },
                  { "waitable-set.wait", P3_SET_WAIT },
                  { "waitable-set.drop", P3_SET_DROP },
                  { "waitable.join", P3_JOIN },
                  { "task.return", P3_TASK_RETURN },
                  { "task.cancel", P3_TASK_CANCEL },
                  { "context.get", P3_CONTEXT_GET },
                  { "context.set", P3_CONTEXT_SET } };
    char text[96];
    if (name->size >= sizeof(text))
        return false;
    memcpy(text, name->data, name->size);
    text[name->size] = 0;
    char *element = strchr(text, ':');
    if (!element)
        return false;
    *element++ = 0;
    char *async_ = strchr(element, ':');
    if (!async_)
        return false;
    *async_++ = 0;
    if (strcmp(element, "u8") && strcmp(element, "unit")
        && strcmp(element, "bool") && strcmp(element, "char"))
        return false;
    if (strcmp(async_, "0") && strcmp(async_, "1"))
        return false;
    b->element = !strcmp(element, "char")   ? 4
                 : !strcmp(element, "bool") ? 2
                                            : (uint8_t)!strcmp(element, "u8");
    b->async_ = (uint8_t)!strcmp(async_, "1");
    const char *operation = text;
    if (!strncmp(operation, "future.", 7)) {
        b->future = 1;
        operation += 7;
    }
    else if (!strncmp(operation, "stream.", 7))
        operation += 7;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(operation, names[i].name)) {
            b->op = names[i].op;
            return true;
        }
    return false;
}
bool
p3_vm_open(p3_vm *vm, const uint8_t *bytes, size_t size)
{
    return p3_vm_open_with_engine(vm, wasm_engine_new(), bytes, size);
}
bool
p3_vm_open_with_engine(p3_vm *vm, wasm_engine_t *engine, const uint8_t *bytes,
                       size_t size)
{
    memset(vm, 0, sizeof(*vm));
    p3_init(&vm->async);
    vm->async.memory = memory;
    vm->async.memory_env = vm;
    vm->engine = engine;
    if (!vm->engine)
        goto fail;
    vm->store = wasm_store_new(vm->engine);
    if (!vm->store)
        goto fail;
    wasm_byte_vec_t binary;
    wasm_byte_vec_new(&binary, size, (const char *)bytes);
    vm->module = wasm_module_new(vm->store, &binary);
    wasm_byte_vec_delete(&binary);
    if (!vm->module)
        goto fail;
    wasm_importtype_vec_t types;
    wasm_module_imports(vm->module, &types);
    wasm_extern_t *imports[P3_IMPORTS] = { 0 };
    bool ok = types.size <= P3_IMPORTS;
    size_t created = 0;
    for (size_t i = 0; ok && i < types.size; ++i) {
        p3_binding *b = &vm->bindings[i];
        b->vm = vm;
        const wasm_functype_t *type = wasm_externtype_as_functype_const(
            wasm_importtype_type(types.data[i]));
        ok = type && name_is(wasm_importtype_module(types.data[i]), "p3")
             && binding(b, wasm_importtype_name(types.data[i]));
        if (!ok)
            break;
        wasm_func_t *fn =
            wasm_func_new_with_env(vm->store, type, canonical, b, NULL);
        if (!fn) {
            ok = false;
            break;
        }
        imports[created++] = wasm_func_as_extern(fn);
    }
    if (ok) {
        wasm_extern_vec_t in = WASM_EMPTY_VEC;
        in.size = created;
        in.data = imports;
#ifdef P3_WAMR
        in.num_elems = created;
#endif
        wasm_trap_t *t = NULL;
#ifdef P3_WAMR
        vm->instance = wasm_instance_new_with_args(vm->store, vm->module, &in,
                                                   &t, 8192, 0);
#else
        vm->instance = wasm_instance_new(vm->store, vm->module, &in, &t);
#endif
        if (t)
            trap_message(vm, t);
        ok = vm->instance != NULL;
    }
    for (size_t i = 0; i < created; ++i)
        wasm_extern_delete(imports[i]);
    wasm_importtype_vec_delete(&types);
    if (!ok)
        goto fail;
    wasm_instance_exports(vm->instance, &vm->exports);
    wasm_module_exports(vm->module, &vm->export_types);
    if (vm->export_types.size > P3_EXPORTS
        || vm->exports.size != vm->export_types.size)
        goto fail;
    for (size_t i = 0; i < vm->export_types.size; ++i) {
        if (name_is(wasm_exporttype_name(vm->export_types.data[i]),
                    "__p3_memory"))
            vm->memory = wasm_extern_as_memory(vm->exports.data[i]);
        if (wasm_extern_kind(vm->exports.data[i]) != WASM_EXTERN_FUNC)
            continue;
        wasm_func_t *fn = wasm_extern_as_func(vm->exports.data[i]);
        if (!fn)
            continue;
        wasm_functype_t *type = wasm_func_type(fn);
        if (!type)
            goto fail;
        const wasm_valtype_vec_t *params = wasm_functype_params(type);
        const wasm_valtype_vec_t *results = wasm_functype_results(type);
        bool numeric = params->size <= 4 && results->size <= 1;
        for (size_t j = 0; numeric && j < params->size; ++j)
            numeric = wasm_valtype_kind(params->data[j]) == WASM_I32;
        if (numeric && results->size)
            numeric = wasm_valtype_kind(results->data[0]) == WASM_I32;
        if (numeric)
            vm->functions[i] = (p3_export){ fn, (uint8_t)params->size,
                                            (uint8_t)results->size };
        wasm_functype_delete(type);
    }
    if (!vm->memory)
        goto fail;
    for (size_t i = 0; i < vm->export_types.size; ++i)
        if (name_is(wasm_exporttype_name(vm->export_types.data[i]),
                    "__p3_start")) {
            int32_t value;
            bool result;
            if (!p3_vm_call(vm, "__p3_start", NULL, 0, &value, &result))
                goto fail;
        }
    return true;
fail:
    if (!vm->error[0])
        snprintf(vm->error, sizeof(vm->error),
                 "component profile instantiation failed");
    return false;
}
void
p3_vm_close(p3_vm *vm)
{
    wasm_extern_vec_delete(&vm->exports);
    wasm_exporttype_vec_delete(&vm->export_types);
    wasm_instance_delete(vm->instance);
    wasm_module_delete(vm->module);
    wasm_store_delete(vm->store);
    wasm_engine_delete(vm->engine);
}
bool
p3_vm_call(p3_vm *vm, const char *name, const int32_t *args, size_t argc,
           int32_t *result, bool *has_result)
{
    return p3_vm_invoke(vm, p3_vm_export(vm, name), args, argc, result,
                        has_result);
}
const p3_export *
p3_vm_export(p3_vm *vm, const char *name)
{
    for (size_t i = 0; i < vm->export_types.size; ++i)
        if (name_is(wasm_exporttype_name(vm->export_types.data[i]), name)
            && vm->functions[i].func)
            return &vm->functions[i];
    return NULL;
}
bool
p3_vm_invoke(p3_vm *vm, const p3_export *entry, const int32_t *args,
             size_t argc, int32_t *result, bool *has_result)
{
    if (vm->poisoned)
        return false;
    if (!entry || argc != entry->argc || argc > 4) {
        snprintf(vm->error, sizeof(vm->error),
                 "unknown or incompatible core export");
        return false;
    }
    wasm_val_t av[4], rv = WASM_I32_VAL(0);
    for (size_t j = 0; j < argc; ++j) {
        av[j].kind = WASM_I32;
        av[j].of.i32 = args[j];
    }
    wasm_val_vec_t a = WASM_EMPTY_VEC, r = WASM_EMPTY_VEC;
    a.size = argc;
    a.data = av;
    r.size = entry->result;
    r.data = &rv;
#ifdef P3_WAMR
    a.num_elems = argc;
    r.num_elems = r.size;
#endif
    wasm_trap_t *t = wasm_func_call(entry->func, &a, &r);
    if (t) {
        vm->poisoned = true;
        trap_message(vm, t);
        return false;
    }
    if (vm->poisoned)
        return false;
    *result = rv.of.i32;
    *has_result = entry->result != 0;
    return true;
}

/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#include "recorder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                   \
    do {                                                           \
        if (!(x)) {                                                \
            fprintf(stderr, "failed line %d: %s\n", __LINE__, #x); \
            exit(1);                                               \
        }                                                          \
    } while (0)
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#define DEBUG_CRASH() __debugbreak()
#else
#define NOINLINE __attribute__((noinline))
#define DEBUG_CRASH() __builtin_trap()
#endif
#ifdef DIAGNOSTIC_NATIVE
extern int
native_cpp(int);
extern int
native_cpp_error(void);
extern int
native_rust_error(void);
#ifdef DIAGNOSTIC_SWIFT
extern int
native_swift_error(void);
#endif
#else
static int
native_cpp(int v)
{
    return v + 4;
}
static int
native_cpp_error(void)
{
    return -11;
}
static int
native_rust_error(void)
{
    return -12;
}
#endif
static int crashing, sync_mode;
static unsigned cancellations;
NOINLINE int
native_checkpoint(int v)
{
    if (crashing == 1)
        DEBUG_CRASH();
    return v + 1;
}
NOINLINE void
parked_checkpoint(void)
{
    static volatile int visits;
    visits++;
}
NOINLINE void
failure_checkpoint(void)
{
    if (crashing == 2)
        DEBUG_CRASH();
}
static NOINLINE wasm_ext_status_t
diagnostic_import(void *env, const wasm_val_vec_t *args,
                  wasm_val_vec_t *results, wasm_trap_t **trap)
{
    (void)env;
    (void)results;
    (void)trap;
    CHECK(debug_published->banks[debug_load(&debug_published->active)]
              .current.event
          == WASM_EXT_DEBUG_HOST);
    CHECK(native_cpp(args->data[0].of.i32) == args->data[0].of.i32 + 4);
    return WASM_EXT_PENDING;
}
static NOINLINE wasm_trap_t *
sync_import(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *out)
{
    (void)env;
    const debug_task *task =
        &debug_published->banks[debug_load(&debug_published->active)];
    CHECK(task->current.event == WASM_EXT_DEBUG_HOST
          && task->current.guest.count == 3);
    out->data[0].kind = WASM_I32;
    out->data[0].of.i32 = native_cpp(args->data[0].of.i32) - 4;
    return NULL;
}
static void
cancelled(void *env)
{
    (void)env;
    cancellations++;
}
typedef struct invocation {
    wasm_store_t *store;
    wasm_module_t *module;
    wasm_func_t *host;
    wasm_instance_t *instance;
    wasm_extern_vec_t exports;
    wasm_ext_call_t *call;
    debug_task task;
} invocation;
static wasm_engine_t *engine;
static wasm_byte_vec_t bytes;
static uint32_t generation;
static wasm_val_t output;
static wasm_val_vec_t results = { .size = 1, .data = &output };
static void
start(invocation *inv, int value, const debug_task *parent)
{
    memset(inv, 0, sizeof(*inv));
    CHECK(debug_task_start(&inv->task, &generation, parent));
    inv->store = wasm_store_new(engine);
    CHECK(inv->store);
    inv->module = wasm_module_new(inv->store, &bytes);
    CHECK(inv->module);
    wasm_functype_t *type =
        wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
    inv->host =
        sync_mode
            ? wasm_func_new_with_env(inv->store, type, sync_import, NULL, NULL)
            : wasm_ext_func_new(inv->store, type, diagnostic_import, NULL,
                                cancelled);
    CHECK(inv->host);
    wasm_functype_delete(type);
    wasm_extern_t *items[] = { wasm_func_as_extern(inv->host) };
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(items);
    wasm_trap_t *trap = NULL;
    inv->instance = wasm_instance_new(inv->store, inv->module, &imports, &trap);
    CHECK(inv->instance && !trap);
    wasm_instance_exports(inv->instance, &inv->exports);
    wasm_func_t *run = NULL;
    for (size_t i = 0; i < inv->exports.size; i++)
        if (wasm_extern_kind(inv->exports.data[i]) == WASM_EXTERN_FUNC) {
            run = wasm_extern_as_func(inv->exports.data[i]);
            break;
        }
    CHECK(run);
    wasm_val_t arg = WASM_I32_VAL(value);
    wasm_val_vec_t args = { .size = 1, .data = &arg };
    args.size = 1;
    args.data = &arg;
    inv->call = wasm_ext_call_new(run, &args);
    CHECK(inv->call);
    CHECK(wasm_ext_call_observe(inv->call, debug_observe, &inv->task));
    if (value == -99) {
        wasm_ext_call_cancel(inv->call);
        CHECK(inv->task.current.event == WASM_EXT_DEBUG_CANCEL);
        CHECK(inv->task.current.guest.version == 1
              && !inv->task.current.guest.count);
        CHECK(wasm_ext_call_resume(inv->call, NULL, &results, &trap)
                  == WASM_EXT_CANCELLED
              && !trap);
        return;
    }
    int status = wasm_ext_call_resume(inv->call, NULL, &results, &trap);
    CHECK(!wasm_ext_call_observe(inv->call, debug_observe, &inv->task));
    if (sync_mode) {
        CHECK(status == WASM_EXT_DONE && !trap && output.of.i32 == 23);
        CHECK(inv->task.current.event == WASM_EXT_DEBUG_DONE);
        return;
    }
    CHECK(status == (value == 0 ? WASM_EXT_ERROR : WASM_EXT_PENDING));
    if (trap)
        wasm_trap_delete(trap);
}
static void
destroy(invocation *inv)
{
    wasm_ext_call_delete(inv->call);
    wasm_extern_vec_delete(&inv->exports);
    wasm_instance_delete(inv->instance);
    wasm_func_delete(inv->host);
    wasm_module_delete(inv->module);
    wasm_store_delete(inv->store);
}
static void
failure(invocation *inv, uint32_t cause)
{
    CHECK(debug_task_error(&inv->task, inv->task.current.task, cause,
                           "native async failure"));
    wasm_name_t message;
    wasm_name_new_from_string(&message, "native async failure");
    wasm_trap_t *trap = NULL;
    wasm_name_t invalid = { .size = 1, .data = NULL };
    CHECK(wasm_ext_call_fail(inv->call, &invalid, &trap) == WASM_EXT_ERROR
          && !trap);
    CHECK(inv->task.current.event == WASM_EXT_DEBUG_PENDING);
    CHECK(wasm_ext_call_fail(inv->call, &message, &trap) == WASM_EXT_ERROR
          && trap);
    wasm_byte_vec_t text;
    wasm_trap_message(trap, &text);
    CHECK(text.size >= 20 && !memcmp(text.data, "native async failure", 19));
    wasm_byte_vec_delete(&text);
    wasm_trap_delete(trap);
    trap = NULL;
    CHECK(wasm_ext_call_fail(inv->call, &message, &trap) == WASM_EXT_ERROR
          && !trap);
    CHECK(!debug_task_error(&inv->task, inv->task.current.task, cause,
                            "duplicate"));
    wasm_byte_vec_delete(&message);
}
int
main(int argc, char **argv)
{
    CHECK(argc >= 2);
    crashing = argc > 2 && !strcmp(argv[2], "crash")         ? 1
               : argc > 2 && !strcmp(argv[2], "async_crash") ? 2
                                                             : 0;
    CHECK(debug_open(getenv("WASM_DEBUG_SIDECAR")));
    engine = wasm_engine_new();
    CHECK(engine);
    FILE *f = fopen(argv[1], "rb");
    CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    CHECK(n > 0);
    rewind(f);
    wasm_byte_vec_new_uninitialized(&bytes, (size_t)n);
    CHECK(fread(bytes.data, 1, bytes.size, f) == bytes.size);
    fclose(f);
    invocation parent, child;
    start(&parent, 20, NULL);
    CHECK(parent.task.current.guest.count == 3);
    for (uint32_t i = 0; i < 3; i++)
        CHECK(parent.task.current.guest.frames[i].flags
              == WASM_EXT_DEBUG_MAPPED);
    parked_checkpoint();
    start(&child, 21, &parent.task);
    CHECK(child.task.ancestors == 1
          && child.task.ancestry[0].task == parent.task.current.task);
    CHECK(!debug_task_error(&child.task, child.task.current.task - 1, 0,
                            "stale"));
    failure(&child, 0);
    CHECK(child.task.current.event == WASM_EXT_DEBUG_ASYNC_ERROR
          && child.task.current.guest.count == 3);
    uint32_t child_id = child.task.current.task;
    CHECK(debug_task_cause(&parent.task, &child.task));
    failure(&parent, child_id);
    CHECK(parent.task.causes == 1 && parent.task.failures[0].task == child_id);
    /* Retain the child report as the failing async segment, including ancestry.
     */
    debug_observe(&child.task, child.task.current.event,
                  &child.task.current.guest);
    failure_checkpoint();
    destroy(&child);
    destroy(&parent);
    CHECK(!cancellations);
    /* These errors are caught/converted before returning through the C ABI,
     * then delivered later as pending-operation failures. Rust uses Result,
     * not catch_unwind: panic=abort remains a fatal native crash. */
    int (*errors[])(void) = {
        native_cpp_error,
        native_rust_error,
#ifdef DIAGNOSTIC_SWIFT
        native_swift_error,
#endif
    };
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        start(&parent, 20, NULL);
        CHECK(errors[i]() == -11 - (int)i);
        failure(&parent, 0);
        destroy(&parent);
    }
    start(&parent, -99, NULL);
    destroy(&parent);
    CHECK(!cancellations);
    start(&parent, 0, NULL);
    CHECK(parent.task.current.event == WASM_EXT_DEBUG_TRAP
          && parent.task.current.guest.count == 3);
    CHECK(parent.task.current.guest.frames[0].flags == WASM_EXT_DEBUG_MAPPED);
    CHECK(parent.task.current.guest.frames[0].offset < bytes.size);
    CHECK(bytes.data[parent.task.current.guest.frames[0].offset]
          == 0); /* original unreachable opcode */
    destroy(&parent);
    start(&parent, 140, NULL);
    CHECK(parent.task.current.guest.count == 32
          && parent.task.current.guest.flags == 1);
    wasm_ext_call_cancel(parent.call);
    CHECK(parent.task.current.event == WASM_EXT_DEBUG_CANCEL);
    destroy(&parent);
    CHECK(cancellations == 1);
    start(&parent, 20, NULL);
    wasm_trap_t *trap = NULL;
    wasm_val_t value = WASM_I32_VAL(-1);
    wasm_val_vec_t completion = { .size = 1, .data = &value };
    CHECK(wasm_ext_call_resume(parent.call, &completion, &results, &trap)
              == WASM_EXT_ERROR
          && trap);
    CHECK(parent.task.current.event == WASM_EXT_DEBUG_TRAP
          && parent.task.current.guest.count == 3);
    wasm_trap_delete(trap);
    destroy(&parent);
    start(&parent, 20, NULL);
    value.of.i32 = 20;
    CHECK(wasm_ext_call_resume(parent.call, &completion, &results, &trap)
              == WASM_EXT_DONE
          && !trap);
    CHECK(output.of.i32 == 23
          && parent.task.current.event == WASM_EXT_DEBUG_DONE
          && !parent.task.current.guest.count);
    destroy(&parent);
    sync_mode = 1;
    start(&parent, 20, NULL);
    destroy(&parent);
    sync_mode = 0;
    debug_task a, b;
    CHECK(debug_task_start(&a, &generation, NULL));
    for (int i = 0; i < 7; i++) {
        CHECK(debug_task_start(&b, &generation, &a));
        a = b;
    }
    CHECK(a.ancestors == 4 && a.truncated);
    char long_error[200];
    memset(long_error, 'x', sizeof(long_error) - 1);
    long_error[199] = 0;
    a.current.event = WASM_EXT_DEBUG_PENDING;
    CHECK(debug_task_error(&a, a.current.task, 0, long_error));
    CHECK(a.current.error_flags == 1 && a.current.error[127] == 0);
    generation = UINT32_MAX;
    CHECK(!debug_task_start(&a, &generation, NULL));
    wasm_byte_vec_delete(&bytes);
    wasm_engine_delete(engine);
    debug_close();
    puts("PASS bounded frames, source offsets, async failure, ancestry, "
         "truncation, cancellation, stale generations");
}

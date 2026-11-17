/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#include "wasm.h"
#include "wasm_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _MSC_VER
#include <intrin.h>
#define NOINLINE __declspec(noinline)
#define CRASH() __debugbreak()
#else
#define NOINLINE __attribute__((noinline))
#define CRASH() __builtin_trap()
#endif
#define CHECK(x)                                 \
    do {                                         \
        if (!(x)) {                              \
            fprintf(stderr, "failed: %s\n", #x); \
            exit(1);                             \
        }                                        \
    } while (0)
#ifdef STACK_AUDIT_NATIVE
extern int
native_cpp(int value);
#else
static int
native_cpp(int value)
{
    return value + 4;
}
#endif
static int crash_enabled;
NOINLINE int
native_checkpoint(int value)
{
    if (crash_enabled)
        CRASH();
    return value + 1;
}
NOINLINE void
parked_checkpoint(void)
{
    static volatile unsigned visits;
    visits++;
}
static NOINLINE wasm_ext_status_t
native_import(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results,
              wasm_trap_t **trap)
{
    (void)env;
    (void)results;
    (void)trap;
    CHECK(native_cpp(args->data[0].of.i32) == args->data[0].of.i32 + 4);
    return WASM_EXT_PENDING;
}
static NOINLINE wasm_trap_t *
sync_import(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    (void)env;
    results->data[0].kind = WASM_I32;
    results->data[0].of.i32 = native_cpp(args->data[0].of.i32);
    return NULL;
}
int
main(int argc, char **argv)
{
    CHECK(argc >= 2);
    crash_enabled = argc > 2 && !strcmp(argv[2], "crash");
    int ordinary = argc > 2 && !strcmp(argv[2], "ordinary");
    int early = argc > 2 && !strcmp(argv[2], "early");
    wasm_engine_t *engine = wasm_engine_new();
    CHECK(engine);
    wasm_store_t *store = wasm_store_new(engine);
    CHECK(store);
    FILE *f = fopen(argv[1], "rb");
    CHECK(f);
    CHECK(!fseek(f, 0, SEEK_END));
    long n = ftell(f);
    CHECK(n > 0);
    rewind(f);
    wasm_byte_vec_t bytes;
    wasm_byte_vec_new_uninitialized(&bytes, (size_t)n);
    CHECK(fread(bytes.data, 1, bytes.size, f) == bytes.size);
    fclose(f);
    wasm_module_t *module = wasm_module_new(store, &bytes);
    CHECK(module);
    wasm_byte_vec_delete(&bytes);
    wasm_functype_t *type =
        wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasm_func_t *host =
        ordinary ? wasm_func_new_with_env(store, type, sync_import, NULL, NULL)
                 : wasm_ext_func_new(store, type, native_import, NULL, NULL);
    CHECK(host);
    wasm_functype_delete(type);
    wasm_extern_t *items[] = { wasm_func_as_extern(host) };
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(items);
    wasm_trap_t *trap = NULL;
    wasm_instance_t *instance =
        wasm_instance_new(store, module, &imports, &trap);
    CHECK(instance && !trap);
    wasm_extern_vec_t exports;
    wasm_instance_exports(instance, &exports);
    wasm_val_t in[] = { WASM_I32_VAL(20) }, out[] = { WASM_I32_VAL(0) };
    wasm_val_vec_t args = WASM_ARRAY_VEC(in), results = WASM_ARRAY_VEC(out);
    wasm_ext_call_t *call = NULL;
    if (ordinary) {
        trap = wasm_func_call(wasm_extern_as_func(exports.data[0]), &args,
                              &results);
        CHECK(trap);
    }
    else {
        if (early)
            in[0].of.i32 = 0;
        call = wasm_ext_call_new(wasm_extern_as_func(exports.data[0]), &args);
        CHECK(call);
        int status = wasm_ext_call_resume(call, NULL, &results, &trap);
        if (!early) {
            CHECK(status == WASM_EXT_PENDING);
            parked_checkpoint();
            status = wasm_ext_call_resume(call, &args, &results, &trap);
        }
        CHECK(status == WASM_EXT_ERROR && trap);
    }
    wasm_frame_vec_t frames;
    wasm_trap_trace(trap, &frames);
    printf("guest_trace_count=%zu\n", frames.size);
#ifdef EXPECT_GUEST_TRACE
    CHECK(frames.size == 3);
    for (size_t i = 0; i < 3; i++)
        CHECK(wasm_frame_func_index(frames.data[i]) == i + 1);
#endif
    for (size_t i = 0; i < frames.size; i++)
        printf("guest_frame index=%u function_offset=%zu module_offset=%zu\n",
               wasm_frame_func_index(frames.data[i]),
               wasm_frame_func_offset(frames.data[i]),
               wasm_frame_module_offset(frames.data[i]));
    wasm_frame_vec_delete(&frames);
    wasm_trap_delete(trap);
    if (call)
        wasm_ext_call_delete(call);
    wasm_extern_vec_delete(&exports);
    wasm_instance_delete(instance);
    wasm_func_delete(host);
    wasm_module_delete(module);
    wasm_store_delete(store);
    wasm_engine_delete(engine);
    return 0;
}

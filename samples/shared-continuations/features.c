/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#include "wasm.h"
#include "wasm_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                     \
        }                                                                \
    } while (0)

typedef struct Counts {
    unsigned calls, cancels;
} Counts;

static wasm_ext_status_t
pending(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results,
        wasm_trap_t **trap)
{
    (void)trap;
    CHECK(args->size == 1 && results->size == 1);
    ((Counts *)env)->calls++;
    return WASM_EXT_PENDING;
}

static wasm_trap_t *
synchronous(void *env, const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    CHECK(args->size == 1 && results->size == 1);
    ((Counts *)env)->calls++;
    results->data[0].kind = WASM_I32;
    results->data[0].of.i32 = args->data[0].of.i32 + 1;
    return NULL;
}

static void
cancelled(void *env)
{
    ((Counts *)env)->cancels++;
}

int
main(int argc, char **argv)
{
    CHECK(argc == 2 || (argc == 3 && !strcmp(argv[2], "aot")));
    bool aot = argc == 3;
    wasm_engine_t *engine = wasm_engine_new();
    CHECK(engine);
    wasm_store_t *store = wasm_store_new(engine);
    CHECK(store);
    FILE *file = fopen(argv[1], "rb");
    CHECK(file && !fseek(file, 0, SEEK_END));
    long length = ftell(file);
    CHECK(length > 0 && !fseek(file, 0, SEEK_SET));
    wasm_byte_vec_t bytes;
    wasm_byte_vec_new_uninitialized(&bytes, (size_t)length);
    CHECK(fread(bytes.data, 1, bytes.size, file) == bytes.size);
    fclose(file);
    wasm_module_t *module = wasm_module_new(store, &bytes);
    CHECK(module);
    wasm_byte_vec_delete(&bytes);
    Counts counts = { 0 };
    wasm_functype_t *type =
        wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
    wasm_func_t *host =
        aot ? wasm_func_new_with_env(store, type, synchronous, &counts, NULL)
            : wasm_ext_func_new(store, type, pending, &counts, cancelled);
    wasm_functype_delete(type);
    CHECK(host);
    wasm_extern_t *items[] = { wasm_func_as_extern(host) };
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(items);
    wasm_trap_t *trap = NULL;
    wasm_val_t arg[] = { WASM_I32_VAL(20) }, out[] = { WASM_I32_VAL(0) };
    wasm_val_t complete[] = { WASM_I32_VAL(1) };
    wasm_val_vec_t args = WASM_ARRAY_VEC(arg);
    wasm_val_vec_t results = WASM_ARRAY_VEC(out);
    wasm_val_vec_t completion = WASM_ARRAY_VEC(complete);
    for (unsigned budget = 0; budget < 2; ++budget) {
#ifdef TEST_WAMR
        wasm_instance_t *instance = wasm_instance_new_with_args(
            store, module, &imports, &trap, budget ? 65536 : 4096, 0);
#else
        wasm_instance_t *instance =
            wasm_instance_new(store, module, &imports, &trap);
#endif
        CHECK(instance && !trap);
        wasm_extern_vec_t exports;
        wasm_instance_exports(instance, &exports);
        CHECK(exports.size == 3);
        wasm_func_t *run = wasm_extern_as_func(exports.data[0]);
        for (unsigned i = 0; i < 12; ++i) {
            arg[0].of.i32 = 20 + (int32_t)i;
            if (aot) {
                CHECK(!wasm_ext_call_new(run, &args));
                trap = wasm_func_call(run, &args, &results);
                CHECK(!trap);
            }
            else {
                unsigned before = counts.calls;
                wasm_ext_call_t *call = wasm_ext_call_new(run, &args);
                CHECK(call);
                CHECK(wasm_ext_call_resume(call, NULL, &results, &trap)
                      == WASM_EXT_PENDING);
                CHECK(wasm_ext_call_resume(call, &completion, &results, &trap)
                      == WASM_EXT_PENDING);
                CHECK(wasm_ext_call_resume(call, &completion, &results, &trap)
                      == WASM_EXT_DONE);
                CHECK(!trap && counts.calls == before + 2);
                wasm_ext_call_delete(call);
                call = wasm_ext_call_new(run, &args);
                CHECK(call);
                CHECK(wasm_ext_call_resume(call, NULL, &results, &trap)
                      == WASM_EXT_PENDING);
                before = counts.cancels;
                wasm_ext_call_cancel(call);
                wasm_ext_call_cancel(call);
                wasm_ext_call_delete(call);
                CHECK(counts.cancels == before + 1);
            }
            CHECK(out[0].of.i32 == 2 * arg[0].of.i32 + 1);
        }
        if (!aot) {
            wasm_func_t *fail = wasm_extern_as_func(exports.data[1]);
            wasm_ext_call_t *call = wasm_ext_call_new(fail, &args);
            CHECK(call);
            CHECK(wasm_ext_call_resume(call, NULL, &results, &trap)
                  == WASM_EXT_PENDING);
            CHECK(wasm_ext_call_resume(call, &completion, &results, &trap)
                  == WASM_EXT_ERROR);
            CHECK(trap);
            wasm_trap_delete(trap);
            trap = NULL;
            wasm_ext_call_delete(call);
#ifdef TEST_WAMR
            arg[0].of.i32 = 200;
            call =
                wasm_ext_call_new(wasm_extern_as_func(exports.data[2]), &args);
            CHECK(call);
            int status = wasm_ext_call_resume(call, NULL, &results, &trap);
            if (budget) {
                CHECK(status == WASM_EXT_PENDING && !trap);
                CHECK(wasm_ext_call_resume(call, &completion, &results, &trap)
                      == WASM_EXT_DONE);
                CHECK(!trap && out[0].of.i32 == 201);
            }
            else {
                CHECK(status == WASM_EXT_ERROR && trap);
                wasm_trap_delete(trap);
                trap = NULL;
            }
            wasm_ext_call_delete(call);
#endif
        }
        wasm_extern_vec_delete(&exports);
        wasm_instance_delete(instance);
    }
    wasm_func_delete(host);
    wasm_module_delete(module);
    wasm_store_delete(store);
    wasm_engine_delete(engine);
    printf("PASS %s: %s, calls=%u cancels=%u\n", argv[1],
           aot ? "ordinary AOT, continuation rejected"
               : "resume/cancel/trap/roots",
           counts.calls, counts.cancels);
    return 0;
}

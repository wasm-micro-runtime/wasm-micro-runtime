/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#include "wasm.h"
#include "wasm_ext.h"
#include "edges.h"
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
typedef struct Guest {
    wasm_store_t *store;
    wasm_module_t *module;
    wasm_instance_t *instance;
    wasm_func_t *host, *run, *empty, *bad;
    wasm_memory_t *memory;
    unsigned calls, cancels;
    int32_t seed;
} Guest;
static wasm_ext_status_t
pause_host(void *env, const wasm_val_vec_t *a, wasm_val_vec_t *r,
           wasm_trap_t **trap)
{
    Guest *g = env;
    (void)trap;
    CHECK(a->size == 1 && a->data[0].kind == WASM_I32 && r->size == 0);
    int32_t stage = a->data[0].of.i32;
    if (stage == 0 || stage == 1) {
        /* Resolve the pointer on each callback: memory.grow may relocate it. */
        const unsigned char *data =
            (const unsigned char *)wasm_memory_data(g->memory);
        size_t size = wasm_memory_data_size(g->memory);
        int32_t first, last;
        CHECK(data && size >= 65536);
        memcpy(&first, data, 4);
        CHECK(first == g->seed);
        if (stage == 1) {
            CHECK(size >= 131072);
            memcpy(&last, data + 65536, 4);
            CHECK(last == g->seed + 1);
        }
    }
    else
        CHECK(stage == 8 || stage == 9);
    g->calls++;
    return WASM_EXT_PENDING;
}
static void
cancelled(void *env)
{
    ((Guest *)env)->cancels++;
}
static void
guest_new(Guest *g, wasm_engine_t *engine, int32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    g->store = wasm_store_new(engine);
    CHECK(g->store);
    wasm_byte_vec_t bytes;
    wasm_byte_vec_new(&bytes, sizeof(edges_wasm), (const char *)edges_wasm);
    g->module = wasm_module_new(g->store, &bytes);
    wasm_byte_vec_delete(&bytes);
    CHECK(g->module);
    wasm_functype_t *ty = wasm_functype_new_1_0(wasm_valtype_new_i32());
    g->host = wasm_ext_func_new(g->store, ty, pause_host, g, cancelled);
    wasm_functype_delete(ty);
    CHECK(g->host);
    wasm_extern_t *items[] = { wasm_func_as_extern(g->host) };
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(items);
    wasm_trap_t *trap = NULL;
    g->instance = wasm_instance_new(g->store, g->module, &imports, &trap);
    CHECK(g->instance && !trap);
    wasm_extern_vec_t exports;
    wasm_instance_exports(g->instance, &exports);
    CHECK(exports.size == 4);
    g->memory = wasm_memory_copy(wasm_extern_as_memory(exports.data[0]));
    g->run = wasm_func_copy(wasm_extern_as_func(exports.data[1]));
    g->empty = wasm_func_copy(wasm_extern_as_func(exports.data[2]));
    g->bad = wasm_func_copy(wasm_extern_as_func(exports.data[3]));
    wasm_extern_vec_delete(&exports);
    CHECK(g->memory && g->run && g->empty && g->bad);
}
static void
guest_delete(Guest *g)
{
    wasm_func_delete(g->run);
    wasm_func_delete(g->empty);
    wasm_func_delete(g->bad);
    wasm_memory_delete(g->memory);
    wasm_instance_delete(g->instance);
    wasm_module_delete(g->module);
    wasm_func_delete(g->host);
    wasm_store_delete(g->store);
}
static wasm_ext_status_t
resume(wasm_ext_call_t *call, const wasm_val_vec_t *completion,
       wasm_val_vec_t *out)
{
    wasm_trap_t *trap = NULL;
    wasm_ext_status_t status =
        wasm_ext_call_resume(call, completion, out, &trap);
    CHECK(!trap);
    return status;
}
int
main(void)
{
    wasm_engine_t *engine = wasm_engine_new();
    CHECK(engine);
    wasm_val_vec_t empty = WASM_EMPTY_VEC;
    wasm_val_t argument[] = { WASM_I32_VAL(17) },
               result[] = { WASM_I32_VAL(-1) };
    wasm_val_vec_t args = WASM_ARRAY_VEC(argument),
                   out = WASM_ARRAY_VEC(result);
    /* Destructive teardown at every boundary, including late completion. */
    for (unsigned repeat = 0; repeat < 32; repeat++)
        for (unsigned boundary = 0; boundary < 3; boundary++) {
            Guest g;
            guest_new(&g, engine, 17);
            wasm_ext_call_t *call = wasm_ext_call_new(g.run, &args);
            CHECK(call);
            if (boundary) {
                CHECK(resume(call, NULL, &out) == WASM_EXT_PENDING);
                for (unsigned i = 0; i < 100; i++)
                    CHECK(resume(call, NULL, &out) == WASM_EXT_PENDING);
                CHECK(g.calls == 1);
                CHECK(resume(call, &args, &out)
                      == WASM_EXT_ERROR); /* void completion */
                CHECK(g.calls == 1);
                CHECK(!wasm_ext_call_new(
                    g.run, &args)); /* Known store-wide exclusion. */
                if (boundary == 2)
                    CHECK(resume(call, &empty, &out) == WASM_EXT_PENDING);
            }
            wasm_ext_call_cancel(call);
            wasm_ext_call_cancel(call);
            CHECK(g.cancels == (boundary ? 1u : 0u));
            CHECK(resume(call, &empty, &out) == WASM_EXT_CANCELLED);
            wasm_ext_call_delete(call);
            CHECK(result[0].of.i32 == -1);
            guest_delete(&g);
        }
    /* Distinct stores can make progress while another core call is suspended.
     */
    Guest a, b;
    guest_new(&a, engine, 17);
    guest_new(&b, engine, 99);
    wasm_ext_call_t *ca = wasm_ext_call_new(a.run, &args);
    CHECK(ca);
    CHECK(resume(ca, NULL, &out) == WASM_EXT_PENDING);
    argument[0].of.i32 = 99;
    wasm_ext_call_t *cb = wasm_ext_call_new(b.run, &args);
    CHECK(cb);
    CHECK(resume(cb, NULL, &out) == WASM_EXT_PENDING);
    CHECK(resume(ca, &empty, &out) == WASM_EXT_PENDING);
    CHECK(resume(cb, &empty, &out) == WASM_EXT_PENDING);
    CHECK(resume(ca, &empty, &out) == WASM_EXT_DONE && result[0].of.i32 == 35);
    CHECK(resume(cb, &empty, &out) == WASM_EXT_DONE && result[0].of.i32 == 199);
    wasm_ext_call_cancel(ca);
    wasm_ext_call_cancel(cb);
    CHECK(!a.cancels && !b.cancels);
    CHECK(resume(ca, &empty, &out) == WASM_EXT_ERROR);
    wasm_ext_call_delete(ca);
    wasm_ext_call_delete(cb);
    /* Zero-result root and trap after resumption, followed by store reuse. */
    ca = wasm_ext_call_new(a.bad, &empty);
    CHECK(ca);
    CHECK(resume(ca, NULL, &empty) == WASM_EXT_PENDING);
    wasm_trap_t *trap = NULL;
    CHECK(wasm_ext_call_resume(ca, &empty, &empty, &trap) == WASM_EXT_ERROR
          && trap);
    wasm_trap_delete(trap);
    wasm_ext_call_delete(ca);
    ca = wasm_ext_call_new(a.empty, &empty);
    CHECK(ca);
    CHECK(resume(ca, NULL, &empty) == WASM_EXT_PENDING);
    CHECK(resume(ca, &empty, &empty) == WASM_EXT_DONE);
    wasm_ext_call_delete(ca);
    CHECK(!a.cancels);
    guest_delete(&a);
    guest_delete(&b);
    wasm_engine_delete(engine);
    puts("void yields, cancellation boundaries, late completion, memory "
         "growth, cross-store interleaving, post-resume trap: PASS");
    return 0;
}

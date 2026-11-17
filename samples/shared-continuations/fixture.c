/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "wasm.h"
#ifdef TEST_CONTINUATIONS
#include "wasm_ext.h"
#include "wide.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
// clang-format off: psapi.h requires Windows declarations first.
#include <windows.h>
#include <psapi.h>
// clang-format on
#else
#include <sys/resource.h>
#endif
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); \
            exit(1);                                                     \
        }                                                                \
    } while (0)
#ifdef TEST_CONTINUATIONS
static unsigned waits, cancels;
typedef struct HostState {
    wasm_store_t *store;
    unsigned mode;
} HostState;
#endif
static wasm_trap_t *
inc(const wasm_val_vec_t *a, wasm_val_vec_t *r)
{
    r->data[0].kind = WASM_I32;
    r->data[0].of.i32 = a->data[0].of.i32 + 1;
    return NULL;
}
#ifdef TEST_CONTINUATIONS
static wasm_ext_status_t
wait_host(void *env, const wasm_val_vec_t *a, wasm_val_vec_t *r,
          wasm_trap_t **trap)
{
    HostState *host = env;
    CHECK(a->size == 1);
    CHECK(a->data[0].kind == WASM_I32);
    waits++;
    if (host->mode == 1) {
        r->data[0].kind = WASM_I32;
        r->data[0].of.i32 = a->data[0].of.i32 + 1;
        return WASM_EXT_DONE;
    }
    if (host->mode == 2) {
        r->data[0].kind =
            WASM_EXTERNREF; /* Must reject before interpreting union bits. */
        r->data[0].of.i64 = 1;
        return WASM_EXT_DONE;
    }
    if (host->mode == 3) {
        wasm_message_t message;
        wasm_name_new_from_string_nt(&message, "host failure");
        *trap = wasm_trap_new(host->store, &message);
        wasm_byte_vec_delete(&message);
        return WASM_EXT_ERROR;
    }
    return WASM_EXT_PENDING;
}
static wasm_ext_status_t
wide_host(void *env, const wasm_val_vec_t *a, wasm_val_vec_t *r,
          wasm_trap_t **trap)
{
    (void)env;
    (void)r;
    (void)trap;
    CHECK(a->size == 1 && a->data[0].kind == WASM_I64
          && a->data[0].of.i64 == INT64_C(0x100000004));
    return WASM_EXT_PENDING;
}
static void
cancel_host(void *env)
{
    (void)env;
    cancels++;
}
#else
static wasm_trap_t *
wait_host(const wasm_val_vec_t *a, wasm_val_vec_t *r)
{
    return inc(a, r);
}
#endif
static wasm_func_t *
find(wasm_module_t *module, wasm_instance_t *instance, const char *name)
{
    wasm_exporttype_vec_t types;
    wasm_extern_vec_t exports;
    wasm_module_exports(module, &types);
    wasm_instance_exports(instance, &exports);
    wasm_func_t *found = NULL;
    for (size_t i = 0; i < types.size; i++) {
        const wasm_name_t *n = wasm_exporttype_name(types.data[i]);
        if ((n->size == strlen(name)
             || (n->size == strlen(name) + 1 && n->data[n->size - 1] == 0))
            && !memcmp(n->data, name, strlen(name)))
            found = wasm_func_copy(wasm_extern_as_func(exports.data[i]));
    }
    wasm_exporttype_vec_delete(&types);
    wasm_extern_vec_delete(&exports);
    CHECK(found);
    return found;
}
/* MSVC clock() measures elapsed time; use process CPU time for this fixture. */
static double
cpu_seconds(void)
{
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    ULARGE_INTEGER k, u;
    CHECK(GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel,
                          &user));
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) * 1e-7;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}
#ifdef _WIN32
static uint64_t
thread_cycles(void)
{
    ULONG64 cycles;
    CHECK(QueryThreadCycleTime(GetCurrentThread(), &cycles));
    return cycles;
}
#endif
static int32_t
invoke(wasm_func_t *f, int32_t n)
{
    wasm_val_t a[] = { WASM_I32_VAL(n) }, r[] = { WASM_I32_VAL(0) };
    wasm_val_vec_t av = WASM_ARRAY_VEC(a), rv = WASM_ARRAY_VEC(r);
    wasm_trap_t *trap = wasm_func_call(f, &av, &rv);
    CHECK(!trap);
    return r[0].of.i32;
}
int
main(int argc, char **argv)
{
    wasm_engine_t *engine = wasm_engine_new();
    CHECK(engine);
    wasm_store_t *store = wasm_store_new(engine);
    CHECK(store);
    printf("wasm_val_bytes %zu wasm_vec_bytes %zu\n", sizeof(wasm_val_t),
           sizeof(wasm_val_vec_t));
    CHECK(argc >= 2);
    FILE *file = fopen(argv[1], "rb");
    CHECK(file);
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    wasm_byte_vec_t bytes;
    wasm_byte_vec_new_uninitialized(&bytes, (size_t)length);
    CHECK(fread(bytes.data, 1, (size_t)length, file) == (size_t)length);
    fclose(file);

    wasm_module_t *module = wasm_module_new(store, &bytes);
    CHECK(module);
    wasm_byte_vec_delete(&bytes);
    wasm_functype_t *type =
        wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
#ifdef TEST_CONTINUATIONS
    HostState host = { store, 0 };
    wasm_func_t *wait =
        wasm_ext_func_new(store, type, wait_host, &host, cancel_host);
#else
    wasm_func_t *wait = wasm_func_new(store, type, wait_host);
#endif
    wasm_func_t *native = wasm_func_new(store, type, inc);
    CHECK(wait && native);
    wasm_functype_delete(type);
    wasm_extern_t *items[] = { wasm_func_as_extern(wait),
                               wasm_func_as_extern(native) };
    wasm_extern_vec_t imports = WASM_ARRAY_VEC(items);
    wasm_trap_t *trap = NULL;
    wasm_instance_t *instance =
        wasm_instance_new(store, module, &imports, &trap);
    CHECK(instance && !trap);
    wasm_func_t *run = find(module, instance, "run"),
                *visits = find(module, instance, "visits"),
                *compute = find(module, instance, "compute"),
                *calls = find(module, instance, "native");
#ifdef TEST_CONTINUATIONS
    wasm_val_t a[] = { WASM_I32_VAL(20) }, r[] = { WASM_I32_VAL(-1) },
               v[] = { WASM_I32_VAL(42) };
    wasm_val_vec_t av = WASM_ARRAY_VEC(a), rv = WASM_ARRAY_VEC(r),
                   completion = WASM_ARRAY_VEC(v), empty = WASM_EMPTY_VEC;
    a[0].kind = WASM_I64;
    CHECK(!wasm_ext_call_new(run, &av));
    a[0].kind = WASM_I32;
    wasm_ext_call_t *call = wasm_ext_call_new(run, &av);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING
          && !trap);
    CHECK(waits == 1 && r[0].of.i32 == -1);
    trap = wasm_func_call(run, &av, &rv);
    CHECK(trap);
    wasm_trap_delete(trap);
    trap = NULL;
    CHECK(waits == 1);
    CHECK(!wasm_ext_call_new(run, &av));
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING);
    CHECK(waits == 1);
    CHECK(wasm_ext_call_resume(call, &empty, &rv, &trap) == WASM_EXT_ERROR
          && !trap);
    v[0].kind = WASM_I64;
    v[0].of.i64 = 42;
    CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap) == WASM_EXT_ERROR
          && !trap);
    CHECK(waits == 1);
    v[0].kind = WASM_I32;
    v[0].of.i32 = 42;
    CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap) == WASM_EXT_DONE
          && !trap);
    CHECK(r[0].of.i32 == 85 && waits == 1);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_ERROR);
    wasm_ext_call_delete(call);
    wasm_val_vec_t noargs = WASM_EMPTY_VEC;
    CHECK(!wasm_func_call(visits, &noargs, &rv));
    CHECK(r[0].of.i32 == 1);
    call = wasm_ext_call_new(run, &av);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING);
    wasm_ext_call_cancel(call);
    wasm_ext_call_cancel(call);
    CHECK(cancels == 1);
    CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap)
          == WASM_EXT_CANCELLED);
    wasm_ext_call_delete(call);
    CHECK(cancels == 1);
    call = wasm_ext_call_new(run, &av);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING);
    wasm_ext_call_delete(call);
    CHECK(cancels == 2);
    CHECK(!wasm_func_call(visits, &noargs, &rv));
    CHECK(r[0].of.i32 == 3);
    unsigned before_waits = waits, before_cancels = cancels;
    call = wasm_ext_call_new(run, &av);
    CHECK(call);
    wasm_ext_call_cancel(call);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_CANCELLED);
    wasm_ext_call_delete(call);
    CHECK(waits == before_waits && cancels == before_cancels);
    wasm_func_t *twice = find(module, instance, "twice"),
                *indirect = find(module, instance, "indirect"),
                *bad = find(module, instance, "trap");
    for (int i = 0; i < 100; i++) {
        unsigned old = waits;
        call = wasm_ext_call_new(twice, &av);
        CHECK(call);
        CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING);
        CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap)
              == WASM_EXT_PENDING);
        CHECK(waits == old + 2);
        CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap)
              == WASM_EXT_DONE);
        CHECK(r[0].of.i32 == 42);
        wasm_ext_call_delete(call);
    }
    call = wasm_ext_call_new(indirect, &av);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_PENDING);
    CHECK(wasm_ext_call_resume(call, &completion, &rv, &trap) == WASM_EXT_DONE);
    CHECK(r[0].of.i32 == 42);
    wasm_ext_call_delete(call);
    call = wasm_ext_call_new(bad, &noargs);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &empty, &trap) == WASM_EXT_ERROR
          && trap);
    wasm_trap_delete(trap);
    trap = NULL;
    wasm_ext_call_delete(call);
    wasm_func_delete(twice);
    wasm_func_delete(indirect);
    wasm_func_delete(bad);
    unsigned old_waits = waits;
    trap = wasm_func_call(run, &av, &rv);
    CHECK(trap && waits == old_waits);
    wasm_trap_delete(trap);
    trap = NULL;
    for (host.mode = 1; host.mode <= 3; host.mode++) {
        call = wasm_ext_call_new(run, &av);
        CHECK(call);
        if (host.mode == 1) {
            CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_DONE
                  && !trap);
            CHECK(r[0].of.i32 == 43);
        }
        else {
            CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_ERROR
                  && trap);
            wasm_trap_delete(trap);
            trap = NULL;
        }
        CHECK(wasm_ext_call_resume(call, NULL, &rv, &trap) == WASM_EXT_ERROR
              && !trap);
        wasm_ext_call_delete(call);
    }
    host.mode = 0;
    wasm_byte_vec_t wide_bytes;
    wasm_byte_vec_new(&wide_bytes, sizeof(wide_wasm), (const char *)wide_wasm);
    wasm_module_t *wide_module = wasm_module_new(store, &wide_bytes);
    CHECK(wide_module);
    wasm_byte_vec_delete(&wide_bytes);
    wasm_functype_t *wide_type =
        wasm_functype_new_1_1(wasm_valtype_new_i64(), wasm_valtype_new_i64());
    wasm_func_t *wide_cb =
        wasm_ext_func_new(store, wide_type, wide_host, NULL, NULL);
    CHECK(wide_cb);
    wasm_functype_delete(wide_type);
    wasm_extern_t *wide_import[] = { wasm_func_as_extern(wide_cb) };
    wasm_extern_vec_t wide_imports = WASM_ARRAY_VEC(wide_import);
    wasm_instance_t *wide_instance =
        wasm_instance_new(store, wide_module, &wide_imports, &trap);
    if (trap) {
        wasm_message_t m;
        wasm_trap_message(trap, &m);
        fprintf(stderr, "wide instantiate: %.*s\n", (int)m.size, m.data);
        wasm_byte_vec_delete(&m);
    }
    CHECK(wide_instance && !trap);
    wasm_func_t *wide_run = find(wide_module, wide_instance, "run");
    wasm_val_t wa[] = { WASM_I32_VAL(7), WASM_I64_VAL(INT64_C(0x100000004)) },
               wr[] = { WASM_I64_VAL(0) },
               wc[] = { WASM_I64_VAL(-INT64_C(0x100000009)) };
    wasm_val_vec_t wav = WASM_ARRAY_VEC(wa), wrv = WASM_ARRAY_VEC(wr),
                   wcv = WASM_ARRAY_VEC(wc);
    call = wasm_ext_call_new(wide_run, &wav);
    CHECK(call);
    CHECK(wasm_ext_call_resume(call, NULL, &wrv, &trap) == WASM_EXT_PENDING
          && !trap);
    CHECK(wasm_ext_call_resume(call, &wcv, &wrv, &trap) == WASM_EXT_DONE
          && !trap);
    CHECK(wr[0].of.i64 == -INT64_C(0x100000008));
    wasm_ext_call_delete(call);
    wasm_func_delete(wide_run);
    wasm_instance_delete(wide_instance);
    wasm_module_delete(wide_module);
    wasm_func_delete(wide_cb);
    puts("pending/resume/cancel, repeat, indirect, guest trap, mixed i32/i64: "
         "PASS");
#endif
    const int32_t n = getenv("WASM_EXT_TEST_ONLY") ? 100 : 100000;
    for (int mode = 0; mode < 2; mode++) {
        const unsigned loops =
            getenv("WASM_EXT_TEST_ONLY") ? 1 : (mode ? 100 : 1000);
        wasm_func_t *f = mode ? calls : compute;
        /* Accumulate in 64 bits to avoid overflow before division. */
        int32_t expected =
            (int32_t)((uint64_t)n * (n + 1u) / 2u + (mode ? (uint32_t)n : 0));
        for (unsigned i = 0; i < 3; i++)
            CHECK(invoke(f, n) == expected);
        double start = cpu_seconds();
#ifdef _WIN32
        uint64_t cycle_start = thread_cycles();
#endif
        for (unsigned i = 0; i < loops; i++)
            CHECK(invoke(f, n) == expected);
        printf("%s_ns_per_iteration %.4f\n", mode ? "native" : "compute",
               1e9 * (cpu_seconds() - start) / loops / n);
#ifdef _WIN32
        printf("%s_cycles_per_iteration %.4f\n", mode ? "native" : "compute",
               (double)(thread_cycles() - cycle_start) / loops / n);
#endif
    }
#ifdef TEST_CONTINUATIONS
    wasm_val_t native_arg[] = { WASM_I32_VAL(n) },
               native_out[] = { WASM_I32_VAL(0) };
    wasm_val_vec_t nav = WASM_ARRAY_VEC(native_arg),
                   nrv = WASM_ARRAY_VEC(native_out);
    double native_start = cpu_seconds();
    for (unsigned i = 0; i < 20; i++) {
        call = wasm_ext_call_new(calls, &nav);
        CHECK(call);
        CHECK(wasm_ext_call_resume(call, NULL, &nrv, &trap) == WASM_EXT_DONE
              && !trap);
        CHECK(native_out[0].of.i32
              == (int32_t)((uint64_t)n * (n + 1u) / 2u + n));
        wasm_ext_call_delete(call);
    }
    printf("optin_native_ns_per_iteration %.4f\n",
           1e9 * (cpu_seconds() - native_start) / 20 / n);
#endif
    wasm_func_delete(run);
    wasm_func_delete(visits);
    wasm_func_delete(compute);
    wasm_func_delete(calls);
    wasm_instance_delete(instance);
    wasm_module_delete(module);
    wasm_func_delete(wait);
    wasm_func_delete(native);
    wasm_store_delete(store);
    wasm_engine_delete(engine);
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory;
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)));
    printf("peak_rss_bytes %zu peak_commit_bytes %zu\n",
           (size_t)memory.PeakWorkingSetSize, (size_t)memory.PeakPagefileUsage);
#else
    struct rusage memory;
    CHECK(!getrusage(RUSAGE_SELF, &memory));
#ifdef __APPLE__
    printf("peak_rss_bytes %ld\n", memory.ru_maxrss);
#else
    printf("peak_rss_bytes %ld\n", memory.ru_maxrss * 1024);
#endif
#endif
    return 0;
}

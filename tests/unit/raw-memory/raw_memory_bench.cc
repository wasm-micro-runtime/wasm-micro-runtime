/*
 * Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Microbench: sandbox vs Raw (order-swapped medians) + shared-pool productish.
 * Usage: ./raw_memory_bench [rounds]
 */

#include "bh_read_file.h"
#include "wasm_export.h"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <time.h>
#include <vector>

static constexpr uint32_t kElems = 16384;
static constexpr size_t kBufBytes = kElems * sizeof(uint32_t);
static constexpr int kWarmup = 3;
static constexpr int kSamples = 5;

static double
now_sec()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int
host_ldadd(wasm_exec_env_t exec_env, uint32_t *p)
{
    (void)exec_env;
    return (int)(*p + 1u);
}

static NativeSymbol g_natives[] = {
    { "host_ldadd", (void *)host_ldadd, "(*)i", nullptr },
};

static void *
map32_malloc(void *env, size_t size)
{
    (void)env;
    size_t map_sz = size < kBufBytes ? kBufBytes : size;
    void *p = mmap(nullptr, map_sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

static void
map32_free(void *env, void *ptr)
{
    (void)env;
    if (ptr)
        munmap(ptr, kBufBytes);
}

static void *
map32_realloc(void *env, void *ptr, size_t size)
{
    void *n = map32_malloc(env, size);
    if (n && ptr) {
        memcpy(n, ptr, size < kBufBytes ? size : kBufBytes);
        map32_free(env, ptr);
    }
    return n;
}

static void *
map32_calloc(void *env, size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = map32_malloc(env, total);
    if (p)
        memset(p, 0, total);
    return p;
}

struct Env {
    wasm_module_t module = nullptr;
    wasm_module_inst_t inst = nullptr;
    wasm_exec_env_t exec = nullptr;
    unsigned char *buf = nullptr;
    void *raw_native = nullptr;
    uint64_t raw_ptr = 0;
};

static void
destroy(Env &e)
{
    if (e.exec)
        wasm_runtime_destroy_exec_env(e.exec);
    if (e.inst) {
        if (e.raw_ptr)
            wasm_runtime_module_free(e.inst, e.raw_ptr);
        wasm_runtime_deinstantiate(e.inst);
    }
    if (e.module)
        wasm_runtime_unload(e.module);
    if (e.buf)
        wasm_runtime_free(e.buf);
    e = Env{};
}

static bool
setup(Env &e, bool raw, RunningMode mode, bool malloc_raw_buf = true)
{
    char err[128] = {};
    unsigned size = 0;
    e.buf = (unsigned char *)bh_read_file_to_buffer("raw_mem_bench.wasm", &size);
    if (!e.buf)
        return false;
    e.module = wasm_runtime_load(e.buf, size, err, sizeof(err));
    if (!e.module) {
        printf("load failed: %s\n", err);
        return false;
    }
    e.inst = wasm_runtime_instantiate(e.module, 256 * 1024, 256 * 1024, err,
                                      sizeof(err));
    if (!e.inst) {
        printf("instantiate failed: %s\n", err);
        return false;
    }
    if (raw) {
        if (!wasm_runtime_set_address_mode(e.inst, WASM_ADDR_RAW))
            return false;
        WASMRawAllocHooks hooks = { map32_malloc, map32_free, map32_realloc,
                                    map32_calloc, nullptr };
        if (!wasm_runtime_set_raw_alloc_hooks(e.inst, &hooks))
            return false;
        if (malloc_raw_buf) {
            e.raw_ptr = wasm_runtime_module_malloc(e.inst, kBufBytes,
                                                   &e.raw_native);
            if (!e.raw_ptr || !e.raw_native)
                return false;
            for (uint32_t i = 0; i < kElems; i++)
                ((uint32_t *)e.raw_native)[i] = i;
        }
    }
    else {
        void *mem = wasm_runtime_addr_app_to_native(e.inst, 0);
        if (!mem)
            return false;
        for (uint32_t i = 0; i < kElems; i++)
            ((uint32_t *)mem)[i] = i;
    }
#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
    if (!wasm_runtime_set_running_mode(e.inst, mode))
        return false;
#else
    (void)mode;
#endif
    e.exec = wasm_runtime_create_exec_env(e.inst, 64 * 1024);
    return e.exec != nullptr;
}

static bool
run_named(Env &e, const char *fn, uint32_t base, uint32_t n, uint32_t *out)
{
    wasm_function_inst_t f = wasm_runtime_lookup_function(e.inst, fn);
    if (!f)
        return false;
    uint32_t argv[2] = { base, n };
    if (!wasm_runtime_call_wasm(e.exec, f, 2, argv)) {
        printf("call %s failed: %s\n", fn,
               wasm_runtime_get_exception(e.inst)
                   ? wasm_runtime_get_exception(e.inst)
                   : "?");
        return false;
    }
    *out = argv[0];
    return true;
}

static double
time_fn_once(Env &e, const char *fn, uint32_t base, int rounds)
{
    uint32_t sink = 0;
    for (int i = 0; i < kWarmup; i++)
        run_named(e, fn, base, kElems, &sink);
    double t0 = now_sec();
    for (int i = 0; i < rounds; i++)
        run_named(e, fn, base, kElems, &sink);
    return now_sec() - t0;
}

static double
median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

static const char *
mode_name(RunningMode m)
{
    switch (m) {
        case Mode_Interp:
            return "interp";
#if WASM_ENABLE_FAST_JIT != 0
        case Mode_Fast_JIT:
            return "fast_jit";
#endif
        default:
            return "other";
    }
}

static void
bench_pair(RunningMode mode, int rounds)
{
    printf("\n== engine=%s  elems=%u  rounds=%d  samples=%d (order-swapped) ==\n",
           mode_name(mode), kElems, rounds, kSamples);

    const char *fns[] = { "sum_i32", "touch_i32", "host_churn" };
    for (const char *fn : fns) {
        std::vector<double> sand_s, raw_s;
        for (int s = 0; s < kSamples; s++) {
            Env a{}, b{};
            /* Alternate which mode runs first to cancel turbo/order bias. */
            bool raw_first = (s & 1) != 0;
            if (!setup(a, raw_first, mode) || !setup(b, !raw_first, mode)) {
                printf("setup failed\n");
                destroy(a);
                destroy(b);
                return;
            }
            Env &raw = raw_first ? a : b;
            Env &sand = raw_first ? b : a;
            uint32_t raw_base = (uint32_t)raw.raw_ptr;
            uint32_t sand_base = 0;

            if (raw_first) {
                raw_s.push_back(time_fn_once(raw, fn, raw_base, rounds));
                sand_s.push_back(time_fn_once(sand, fn, sand_base, rounds));
            }
            else {
                sand_s.push_back(time_fn_once(sand, fn, sand_base, rounds));
                raw_s.push_back(time_fn_once(raw, fn, raw_base, rounds));
            }
            destroy(a);
            destroy(b);
        }
        double ms = median(sand_s), mr = median(raw_s);
        double ns_s = ms * 1e9 / ((double)rounds * (double)kElems);
        double ns_r = mr * 1e9 / ((double)rounds * (double)kElems);
        printf("%-10s  sandbox %7.3f s (%6.2f ns/el)  raw %7.3f s (%6.2f "
               "ns/el)  speedup %.2fx\n",
               fn, ms, ns_s, mr, ns_r, ms / mr);
    }
}

/* Product-ish: two instances, same buffer.
 * Raw: writer + reader share one host pointer (no copy).
 * Sandbox: writer fills linear mem, host memcpy into reader mem, then read.
 */
static void
bench_shared_pool(RunningMode mode, int rounds)
{
    printf("\n== shared_pool engine=%s rounds=%d ==\n", mode_name(mode),
           rounds);

    std::vector<double> sand_s, raw_s;
    for (int s = 0; s < kSamples; s++) {
        bool raw_first = (s & 1) != 0;

        auto time_raw = [&]() -> double {
            Env w{}, r{};
            if (!setup(w, true, mode) || !setup(r, true, mode, false))
                return -1;
            /* Reader uses writer's buffer pointer. */
            uint32_t ptr = (uint32_t)w.raw_ptr;
            uint32_t sink = 0;
            for (int i = 0; i < kWarmup; i++) {
                run_named(w, "touch_i32", ptr, kElems, &sink);
                run_named(r, "sum_i32", ptr, kElems, &sink);
            }
            double t0 = now_sec();
            for (int i = 0; i < rounds; i++) {
                run_named(w, "touch_i32", ptr, kElems, &sink);
                run_named(r, "sum_i32", ptr, kElems, &sink);
            }
            double dt = now_sec() - t0;
            destroy(w);
            destroy(r);
            return dt;
        };

        auto time_sand = [&]() -> double {
            Env w{}, r{};
            if (!setup(w, false, mode) || !setup(r, false, mode))
                return -1;
            void *wm = wasm_runtime_addr_app_to_native(w.inst, 0);
            void *rm = wasm_runtime_addr_app_to_native(r.inst, 0);
            uint32_t sink = 0;
            for (int i = 0; i < kWarmup; i++) {
                run_named(w, "touch_i32", 0, kElems, &sink);
                memcpy(rm, wm, kBufBytes);
                run_named(r, "sum_i32", 0, kElems, &sink);
            }
            double t0 = now_sec();
            for (int i = 0; i < rounds; i++) {
                run_named(w, "touch_i32", 0, kElems, &sink);
                memcpy(rm, wm, kBufBytes);
                run_named(r, "sum_i32", 0, kElems, &sink);
            }
            double dt = now_sec() - t0;
            destroy(w);
            destroy(r);
            return dt;
        };

        if (raw_first) {
            raw_s.push_back(time_raw());
            sand_s.push_back(time_sand());
        }
        else {
            sand_s.push_back(time_sand());
            raw_s.push_back(time_raw());
        }
    }

    double ms = median(sand_s), mr = median(raw_s);
    printf("write+share+read  sandbox %7.3f s  raw %7.3f s  speedup %.2fx\n",
           ms, mr, ms / mr);
}

#if defined(RAW_MEMORY_HAS_AOT_FIXTURE)
static bool
setup_aot(Env &e, bool raw)
{
    char err[128] = {};
    unsigned size = 0;
    const char *path =
        raw ? "raw_mem_bench_raw.aot" : "raw_mem_bench_sandbox.aot";
    e.buf = (unsigned char *)bh_read_file_to_buffer(path, &size);
    if (!e.buf)
        return false;
    e.module = wasm_runtime_load(e.buf, size, err, sizeof(err));
    if (!e.module) {
        printf("aot load %s failed: %s\n", path, err);
        return false;
    }
    e.inst = wasm_runtime_instantiate(e.module, 256 * 1024, 256 * 1024, err,
                                      sizeof(err));
    if (!e.inst)
        return false;
    if (raw) {
        if (!wasm_runtime_set_address_mode(e.inst, WASM_ADDR_RAW))
            return false;
        WASMRawAllocHooks hooks = { map32_malloc, map32_free, map32_realloc,
                                    map32_calloc, nullptr };
        if (!wasm_runtime_set_raw_alloc_hooks(e.inst, &hooks))
            return false;
        e.raw_ptr =
            wasm_runtime_module_malloc(e.inst, kBufBytes, &e.raw_native);
        if (!e.raw_ptr)
            return false;
        for (uint32_t i = 0; i < kElems; i++)
            ((uint32_t *)e.raw_native)[i] = i;
    }
    else {
        void *mem = wasm_runtime_addr_app_to_native(e.inst, 0);
        if (!mem)
            return false;
        for (uint32_t i = 0; i < kElems; i++)
            ((uint32_t *)mem)[i] = i;
    }
    e.exec = wasm_runtime_create_exec_env(e.inst, 64 * 1024);
    return e.exec != nullptr;
}

static void
bench_aot_host(int rounds)
{
    printf("\n== aot host_churn rounds=%d samples=%d ==\n", rounds, kSamples);
    std::vector<double> sand_s, raw_s;
    for (int s = 0; s < kSamples; s++) {
        bool raw_first = (s & 1) != 0;
        Env a{}, b{};
        if (!setup_aot(a, raw_first) || !setup_aot(b, !raw_first)) {
            printf("aot setup failed (fixtures missing?)\n");
            destroy(a);
            destroy(b);
            return;
        }
        Env &raw = raw_first ? a : b;
        Env &sand = raw_first ? b : a;
        if (raw_first) {
            raw_s.push_back(
                time_fn_once(raw, "host_churn", (uint32_t)raw.raw_ptr, rounds));
            sand_s.push_back(time_fn_once(sand, "host_churn", 0, rounds));
        }
        else {
            sand_s.push_back(time_fn_once(sand, "host_churn", 0, rounds));
            raw_s.push_back(
                time_fn_once(raw, "host_churn", (uint32_t)raw.raw_ptr, rounds));
        }
        destroy(a);
        destroy(b);
    }
    double ms = median(sand_s), mr = median(raw_s);
    printf("host_churn  sandbox %7.3f s  raw %7.3f s  speedup %.2fx\n", ms, mr,
           ms / mr);
}
#endif

int
main(int argc, char **argv)
{
    int rounds = 400;
    if (argc > 1)
        rounds = atoi(argv[1]);
    if (rounds < 1)
        rounds = 1;

    RuntimeInitArgs init{};
    memset(&init, 0, sizeof(init));
    init.mem_alloc_type = Alloc_With_System_Allocator;
    init.native_module_name = "env";
    init.n_native_symbols = 1;
    init.native_symbols = g_natives;

    if (!wasm_runtime_full_init(&init)) {
        printf("runtime init failed\n");
        return 1;
    }

    bench_pair(Mode_Interp, rounds);
#if WASM_ENABLE_FAST_JIT != 0
    bench_pair(Mode_Fast_JIT, rounds);
    bench_shared_pool(Mode_Fast_JIT, rounds / 2 > 0 ? rounds / 2 : 1);
#endif
#if WASM_ENABLE_FAST_INTERP != 0 && WASM_ENABLE_FAST_JIT == 0
    bench_shared_pool(Mode_Interp, rounds / 2 > 0 ? rounds / 2 : 1);
#endif
#if defined(RAW_MEMORY_HAS_AOT_FIXTURE)
    bench_aot_host(rounds);
#endif

    wasm_runtime_destroy();
    return 0;
}

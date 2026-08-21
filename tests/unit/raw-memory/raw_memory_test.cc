/*
 * Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "wasm_runtime_common.h"

#include <cstring>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

class raw_memory_test : public testing::Test {
  protected:
    WAMRRuntimeRAII<512 * 1024> runtime;
};

struct ModuleEnv {
    wasm_exec_env_t exec_env = nullptr;
    wasm_module_t module = nullptr;
    wasm_module_inst_t inst = nullptr;
    unsigned char *buf = nullptr;
};

static void
destroy_env(ModuleEnv &e)
{
    if (e.exec_env)
        wasm_runtime_destroy_exec_env(e.exec_env);
    if (e.inst)
        wasm_runtime_deinstantiate(e.inst);
    if (e.module)
        wasm_runtime_unload(e.module);
    if (e.buf)
        wasm_runtime_free(e.buf);
    e = ModuleEnv{};
}

static bool
load_file(ModuleEnv &e, const char *path, bool raw,
          RunningMode mode = Mode_Interp)
{
    char error_buf[128] = {};
    unsigned size = 0;
    e.buf = (unsigned char *)bh_read_file_to_buffer(path, &size);
    if (!e.buf)
        return false;
    e.module = wasm_runtime_load(e.buf, size, error_buf, sizeof(error_buf));
    if (!e.module) {
        printf("load %s failed: %s\n", path, error_buf);
        return false;
    }
    e.inst = wasm_runtime_instantiate(e.module, 16 * 1024, 16 * 1024, error_buf,
                                      sizeof(error_buf));
    if (!e.inst) {
        printf("instantiate %s failed: %s\n", path, error_buf);
        return false;
    }
    if (raw) {
        if (!wasm_runtime_set_address_mode(e.inst, WASM_ADDR_RAW))
            return false;
    }
#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
    if (!wasm_runtime_set_running_mode(e.inst, mode))
        return false;
#else
    (void)mode;
#endif
    e.exec_env = wasm_runtime_create_exec_env(e.inst, 16 * 1024);
    return e.exec_env != nullptr;
}

static bool
load_env(ModuleEnv &e, bool raw, RunningMode mode = Mode_Interp)
{
    return load_file(e, "raw_mem_test.wasm", raw, mode);
}

/* Allocate in the low 32-bit VA so mem32 Raw Fast-JIT can use the pointer. */
static void *
map32_malloc(void *env, size_t size)
{
    (void)env;
    size_t map_sz = size < 4096 ? 4096 : size;
    void *p = mmap(nullptr, map_sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

static void
map32_free(void *env, void *ptr)
{
    (void)env;
    if (ptr)
        munmap(ptr, 4096);
}

static void *
map32_realloc(void *env, void *ptr, size_t size)
{
    void *n = map32_malloc(env, size);
    if (n && ptr) {
        memcpy(n, ptr, size < 4096 ? size : 4096);
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

static bool
install_map32_hooks(wasm_module_inst_t inst)
{
    WASMRawAllocHooks hooks = { map32_malloc, map32_free, map32_realloc,
                                map32_calloc, nullptr };
    return wasm_runtime_set_raw_alloc_hooks(inst, &hooks);
}

/*
 * Embedder-style nest: each Raw node's hooks forward module_malloc to its
 * parent instance (or map32 at the top). Demonstrates the policy chain —
 * WAMR does not wire this automatically.
 */
struct NestNode {
    const char *name = nullptr;
    wasm_module_inst_t inst = nullptr;
    NestNode *parent = nullptr;
    bool parent_is_sandbox = false;
    std::vector<std::string> *trace = nullptr;
    std::unordered_map<void *, uint64_t> *sandbox_offs = nullptr;
};

static void
nest_log(NestNode *n, const char *op)
{
    n->trace->push_back(std::string(n->name) + "." + op);
}

static void *
nest_malloc(void *env, size_t size)
{
    NestNode *n = (NestNode *)env;
    nest_log(n, "malloc");
    if (!n->parent) {
        nest_log(n, "malloc(map32 backer)");
        return map32_malloc(nullptr, size);
    }
    n->trace->push_back(std::string("→ forward to ") + n->parent->name
                        + ".module_malloc");
    void *native = nullptr;
    uint64_t app =
        wasm_runtime_module_malloc(n->parent->inst, size, &native);
    if (!native && !app)
        return nullptr;
    if (n->parent_is_sandbox) {
        (*n->sandbox_offs)[native] = app;
        return native;
    }
    return (void *)(uintptr_t)app;
}

static void
nest_free(void *env, void *ptr)
{
    NestNode *n = (NestNode *)env;
    if (!ptr)
        return;
    nest_log(n, "free");
    if (!n->parent) {
        map32_free(nullptr, ptr);
        return;
    }
    uint64_t app = (uint64_t)(uintptr_t)ptr;
    if (n->parent_is_sandbox) {
        auto it = n->sandbox_offs->find(ptr);
        if (it == n->sandbox_offs->end())
            return;
        app = it->second;
        n->sandbox_offs->erase(it);
    }
    wasm_runtime_module_free(n->parent->inst, app);
}

static void *
nest_realloc(void *env, void *ptr, size_t size)
{
    void *n = nest_malloc(env, size);
    if (n && ptr) {
        memcpy(n, ptr, size < 4096 ? size : 4096);
        nest_free(env, ptr);
    }
    return n;
}

static void *
nest_calloc(void *env, size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = nest_malloc(env, total);
    if (p)
        memset(p, 0, total);
    return p;
}

static bool
install_nest_hooks(NestNode *node)
{
    WASMRawAllocHooks hooks = { nest_malloc, nest_free, nest_realloc,
                                nest_calloc, node };
    return wasm_runtime_set_raw_alloc_hooks(node->inst, &hooks);
}

static bool
ptr_in_sandbox_linear(wasm_module_inst_t sandbox, void *native)
{
    wasm_memory_inst_t mem;
    uint8_t *base;
    uint8_t *addr;
    uint64_t bytes;

    if (!sandbox || !native)
        return false;
    /* Raw identity converters make offset round-trips meaningless. */
    if (wasm_runtime_get_address_mode(sandbox) == WASM_ADDR_RAW)
        return false;

    mem = wasm_runtime_get_default_memory(sandbox);
    if (!mem)
        return false;
    base = (uint8_t *)wasm_memory_get_base_address(mem);
    bytes = (uint64_t)wasm_memory_get_cur_page_count(mem)
            * (uint64_t)wasm_memory_get_bytes_per_page(mem);
    addr = (uint8_t *)native;
    return addr >= base && addr < base + bytes;
}

static void
print_trace(const char *title, const std::vector<std::string> &trace)
{
    printf("\n== %s ==\n", title);
    for (const auto &s : trace)
        printf("  %s\n", s.c_str());
}

static void
pack_i64(uint32_t *argv, uint64_t v)
{
    argv[0] = (uint32_t)v;
    argv[1] = (uint32_t)(v >> 32);
}

static uint64_t
unpack_i64(const uint32_t *argv)
{
    return ((uint64_t)argv[1] << 32) | argv[0];
}

TEST_F(raw_memory_test, sandbox_default_unchanged)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, false, Mode_Interp));
    EXPECT_EQ(wasm_runtime_get_address_mode(e.inst), WASM_ADDR_SANDBOX);

    void *native = nullptr;
    uint64_t off = wasm_runtime_module_malloc(e.inst, 16, &native);
    ASSERT_NE(off, 0u);
    ASSERT_NE(native, nullptr);
    EXPECT_EQ(wasm_runtime_addr_app_to_native(e.inst, off), native);
    wasm_runtime_module_free(e.inst, off);
    destroy_env(e);
}

TEST_F(raw_memory_test, raw_default_os_hooks_identity_malloc)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Interp));
    /* No custom hooks → os_malloc / os_free path. */
    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 64, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_EQ(native, (void *)(uintptr_t)ptr);
    ASSERT_EQ(wasm_runtime_addr_app_to_native(e.inst, ptr), native);
    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}

TEST_F(raw_memory_test, nest_alloc_chain_sandbox_vs_raw_outer)
{
    /*
     * Embedder demo (not automatic): leaf → mid → outer.
     *  A) outermost Raw  → process-like map32 backer
     *  B) outermost Sandbox → bytes live in outer linear mem
     *  C) Raw under sandbox with NO hooks → escapes outer linear
     */
    std::vector<std::string> trace;
    std::unordered_map<void *, uint64_t> sandbox_offs;

    /* --- A: outermost RAW --- */
    {
        ModuleEnv top{}, mid{}, leaf{};
        ASSERT_TRUE(load_env(top, true, Mode_Interp));
        ASSERT_TRUE(load_env(mid, true, Mode_Interp));
        ASSERT_TRUE(load_env(leaf, true, Mode_Interp));

        NestNode n_top{ "top(raw)", top.inst, nullptr, false, &trace,
                        &sandbox_offs };
        NestNode n_mid{ "mid(raw)", mid.inst, &n_top, false, &trace,
                        &sandbox_offs };
        NestNode n_leaf{ "leaf(raw)", leaf.inst, &n_mid, false, &trace,
                         &sandbox_offs };
        ASSERT_TRUE(install_nest_hooks(&n_top));
        ASSERT_TRUE(install_nest_hooks(&n_mid));
        ASSERT_TRUE(install_nest_hooks(&n_leaf));

        trace.clear();
        void *native = nullptr;
        uint64_t ptr =
            wasm_runtime_module_malloc(leaf.inst, 64, &native);
        ASSERT_NE(ptr, 0u);
        ASSERT_EQ(native, (void *)(uintptr_t)ptr);

        print_trace("nest A: outermost RAW (process-like)", trace);
        ASSERT_GE(trace.size(), 3u);
        EXPECT_EQ(trace[0], "leaf(raw).malloc");
        EXPECT_EQ(trace[1], "→ forward to mid(raw).module_malloc");
        EXPECT_EQ(trace[2], "mid(raw).malloc");
        EXPECT_EQ(trace[3], "→ forward to top(raw).module_malloc");
        EXPECT_EQ(trace[4], "top(raw).malloc");
        EXPECT_EQ(trace[5], "top(raw).malloc(map32 backer)");
        /* Not inside any sandbox linear — host/map32 VA. */
        EXPECT_FALSE(ptr_in_sandbox_linear(top.inst, native));

        wasm_runtime_module_free(leaf.inst, ptr);
        destroy_env(leaf);
        destroy_env(mid);
        destroy_env(top);
    }

    /* --- B: outermost SANDBOX --- */
    {
        ModuleEnv outer{}, mid{}, leaf{};
        ASSERT_TRUE(load_env(outer, false, Mode_Interp));
        ASSERT_TRUE(load_env(mid, true, Mode_Interp));
        ASSERT_TRUE(load_env(leaf, true, Mode_Interp));

        NestNode n_outer{ "outer(sandbox)", outer.inst, nullptr, false,
                          &trace, &sandbox_offs };
        NestNode n_mid{ "mid(raw)", mid.inst, &n_outer, true, &trace,
                        &sandbox_offs };
        NestNode n_leaf{ "leaf(raw)", leaf.inst, &n_mid, false, &trace,
                         &sandbox_offs };
        ASSERT_TRUE(install_nest_hooks(&n_mid));
        ASSERT_TRUE(install_nest_hooks(&n_leaf));

        trace.clear();
        sandbox_offs.clear();
        void *native = nullptr;
        uint64_t ptr =
            wasm_runtime_module_malloc(leaf.inst, 64, &native);
        ASSERT_NE(ptr, 0u);
        ASSERT_EQ(native, (void *)(uintptr_t)ptr);

        print_trace("nest B: outermost SANDBOX (hooked into outer)",
                    trace);
        ASSERT_GE(trace.size(), 3u);
        EXPECT_EQ(trace[0], "leaf(raw).malloc");
        EXPECT_EQ(trace[1], "→ forward to mid(raw).module_malloc");
        EXPECT_EQ(trace[2], "mid(raw).malloc");
        EXPECT_EQ(trace[3],
                  "→ forward to outer(sandbox).module_malloc");
        EXPECT_TRUE(ptr_in_sandbox_linear(outer.inst, native));

        /* Prove shared bytes via host ptr (mem32 leaf cannot i32-truncate
         * a high VA into outer linear on 64-bit hosts). */
        *(uint32_t *)native = 0x4E535401u;
        EXPECT_EQ(*(uint32_t *)native, 0x4E535401u);

        wasm_runtime_module_free(leaf.inst, ptr);
        destroy_env(leaf);
        destroy_env(mid);
        destroy_env(outer);
    }

    /* --- C: Raw under sandbox, default hooks → escapes --- */
    {
        ModuleEnv outer{}, inner{};
        ASSERT_TRUE(load_env(outer, false, Mode_Interp));
        ASSERT_TRUE(load_env(inner, true, Mode_Interp));
        /* No nest hooks on inner → os_malloc / identity. */
        void *native = nullptr;
        uint64_t ptr =
            wasm_runtime_module_malloc(inner.inst, 64, &native);
        ASSERT_NE(ptr, 0u);
        printf("\n== nest C: Raw under sandbox, NO hooks (escapes) ==\n");
        printf("  inner.malloc → default os_* native=%p\n", native);
        EXPECT_FALSE(ptr_in_sandbox_linear(outer.inst, native));
        wasm_runtime_module_free(inner.inst, ptr);
        destroy_env(inner);
        destroy_env(outer);
    }
}

TEST_F(raw_memory_test, raw_malloc_identity_and_load_store_interp)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Interp));
    EXPECT_EQ(wasm_runtime_get_address_mode(e.inst), WASM_ADDR_RAW);
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 16, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_EQ(native, (void *)(uintptr_t)ptr);
    ASSERT_EQ(wasm_runtime_addr_app_to_native(e.inst, ptr), native);
    ASSERT_LE(ptr, UINT32_MAX);

    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(load, nullptr);

    uint32_t argv[2] = { (uint32_t)ptr, 0xA1B2C3D4u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 2, argv));
    argv[0] = (uint32_t)ptr;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 1, argv));
    EXPECT_EQ(argv[0], 0xA1B2C3D4u);

    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}

TEST_F(raw_memory_test, raw_realloc_identity)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Interp));
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 8, &native);
    ASSERT_NE(ptr, 0u);
    memset(native, 0x5A, 8);

    void *native2 = nullptr;
    uint64_t ptr2 =
        wasm_runtime_module_realloc(e.inst, ptr, 32, &native2);
    ASSERT_NE(ptr2, 0u);
    ASSERT_EQ(native2, (void *)(uintptr_t)ptr2);
    EXPECT_EQ(((uint8_t *)native2)[0], 0x5A);

    wasm_runtime_module_free(e.inst, ptr2);
    destroy_env(e);
}

TEST_F(raw_memory_test, raw_memory_grow_returns_minus_one)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Interp));
    wasm_function_inst_t grow =
        wasm_runtime_lookup_function(e.inst, "memory_grow");
    ASSERT_NE(grow, nullptr);
    uint32_t argv[2] = { 1u, 0 };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, grow, 1, argv));
#if WASM_ENABLE_MEMORY64 != 0
    /* mem32 fixture still uses i32 grow result when MEMORY64 runtime loads
     * mem32 modules. */
#endif
    EXPECT_EQ(argv[0], (uint32_t)-1);
    destroy_env(e);
}

TEST_F(raw_memory_test, raw_bulk_fill_and_copy_interp)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Interp));
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 64, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_LE(ptr, UINT32_MAX);

    wasm_function_inst_t fill =
        wasm_runtime_lookup_function(e.inst, "memory_fill");
    wasm_function_inst_t copy =
        wasm_runtime_lookup_function(e.inst, "memory_copy");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    ASSERT_NE(fill, nullptr);
    ASSERT_NE(copy, nullptr);

    uint32_t argv[3] = { (uint32_t)ptr, 0xABu, 16u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, fill, 3, argv));
    EXPECT_EQ(((uint8_t *)native)[0], 0xAB);
    EXPECT_EQ(((uint8_t *)native)[15], 0xAB);

    uint32_t dst = (uint32_t)ptr + 32;
    argv[0] = dst;
    argv[1] = (uint32_t)ptr;
    argv[2] = 16u;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, copy, 3, argv));
    EXPECT_EQ(((uint8_t *)native)[32], 0xAB);

    argv[0] = dst;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 1, argv));
    EXPECT_EQ(argv[0] & 0xFFu, 0xABu);

    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}

TEST_F(raw_memory_test, sandbox_memory_grow_can_succeed)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, false, Mode_Interp));
    wasm_function_inst_t grow =
        wasm_runtime_lookup_function(e.inst, "memory_grow");
    ASSERT_NE(grow, nullptr);
    uint32_t argv[1] = { 1u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, grow, 1, argv));
    EXPECT_NE(argv[0], (uint32_t)-1);
    destroy_env(e);
}

#if WASM_ENABLE_FAST_JIT != 0
TEST_F(raw_memory_test, sandbox_fast_jit_smoke)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, false, Mode_Fast_JIT));
    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    uint32_t argv[2] = { 0, 0x11u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 2, argv));
    destroy_env(e);
}


TEST_F(raw_memory_test, raw_load_store_fast_jit)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Fast_JIT));
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 16, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_NE(native, nullptr);
    memset(native, 0, 16);

    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(load, nullptr);

    uint32_t argv[2] = { (uint32_t)ptr, 0x55667788u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 2, argv));
    ASSERT_EQ(*(uint32_t *)native, 0x55667788u);

    argv[0] = (uint32_t)ptr;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 1, argv));
    EXPECT_EQ(argv[0], 0x55667788u);

    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}


TEST_F(raw_memory_test, raw_memory_grow_fast_jit_returns_minus_one)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Fast_JIT));
    wasm_function_inst_t grow =
        wasm_runtime_lookup_function(e.inst, "memory_grow");
    ASSERT_NE(grow, nullptr);
    uint32_t argv[1] = { 1u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, grow, 1, argv));
    EXPECT_EQ(argv[0], (uint32_t)-1);
    destroy_env(e);
}

TEST_F(raw_memory_test, raw_bulk_fill_fast_jit)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_env(e, true, Mode_Fast_JIT));
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 32, &native);
    ASSERT_NE(ptr, 0u);

    wasm_function_inst_t fill =
        wasm_runtime_lookup_function(e.inst, "memory_fill");
    uint32_t argv[3] = { (uint32_t)ptr, 0xCDu, 8u };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, fill, 3, argv));
    EXPECT_EQ(((uint8_t *)native)[0], 0xCD);
    EXPECT_EQ(((uint8_t *)native)[7], 0xCD);
    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}
#endif

#if defined(RAW_MEMORY_HAS_AOT_FIXTURE)
TEST_F(raw_memory_test, raw_aot_load_store)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_file(e, "raw_mem_test.aot", true, Mode_Interp));
    ASSERT_TRUE(install_map32_hooks(e.inst));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 16, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_LE(ptr, UINT32_MAX);

    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(load, nullptr);

    uint32_t argv[2] = { (uint32_t)ptr, 0xDEADBEEFu };
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 2, argv));
    argv[0] = (uint32_t)ptr;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 1, argv));
    EXPECT_EQ(argv[0], 0xDEADBEEFu);

    wasm_function_inst_t grow =
        wasm_runtime_lookup_function(e.inst, "memory_grow");
    ASSERT_NE(grow, nullptr);
    argv[0] = 1u;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, grow, 1, argv));
    EXPECT_EQ(argv[0], (uint32_t)-1);

    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}
#endif

#if WASM_ENABLE_MEMORY64 != 0
TEST_F(raw_memory_test, raw_memory64_load_store_full_pointer)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_file(e, "raw_mem64_test.wasm", true, Mode_Interp));
    /* Default os_* hooks: full 64-bit host pointers are valid guest i64s. */
    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 32, &native);
    ASSERT_NE(ptr, 0u);
    ASSERT_EQ(native, (void *)(uintptr_t)ptr);

    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    ASSERT_NE(store, nullptr);
    ASSERT_NE(load, nullptr);

    uint32_t argv[4] = {};
    pack_i64(argv, ptr);
    argv[2] = 0x11223344u;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 3, argv));

    pack_i64(argv, ptr);
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 2, argv));
    EXPECT_EQ(argv[0], 0x11223344u);

    wasm_function_inst_t fill =
        wasm_runtime_lookup_function(e.inst, "memory_fill");
    pack_i64(argv, ptr);
    argv[2] = 0x7Eu;
    pack_i64(argv + 3, 8);
    /* fill(i64,i32,i64) → 2+1+2 = 5 cells */
    uint32_t fargv[5] = {};
    pack_i64(fargv, ptr);
    fargv[2] = 0x7Eu;
    pack_i64(fargv + 3, 8);
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, fill, 5, fargv));
    EXPECT_EQ(((uint8_t *)native)[0], 0x7E);

    wasm_function_inst_t grow =
        wasm_runtime_lookup_function(e.inst, "memory_grow");
    uint32_t gargv[2] = {};
    pack_i64(gargv, 1);
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, grow, 2, gargv));
    EXPECT_EQ(unpack_i64(gargv), (uint64_t)-1);

    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}

#if defined(RAW_MEMORY_HAS_AOT64_FIXTURE)
TEST_F(raw_memory_test, raw_memory64_aot_load_store)
{
    ModuleEnv e{};
    ASSERT_TRUE(load_file(e, "raw_mem64_test.aot", true, Mode_Interp));

    void *native = nullptr;
    uint64_t ptr = wasm_runtime_module_malloc(e.inst, 16, &native);
    ASSERT_NE(ptr, 0u);

    wasm_function_inst_t store =
        wasm_runtime_lookup_function(e.inst, "store_i32");
    wasm_function_inst_t load =
        wasm_runtime_lookup_function(e.inst, "load_i32");
    uint32_t argv[4] = {};
    pack_i64(argv, ptr);
    argv[2] = 0xCAFEBABEu;
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, store, 3, argv));
    pack_i64(argv, ptr);
    ASSERT_TRUE(wasm_runtime_call_wasm(e.exec_env, load, 2, argv));
    EXPECT_EQ(argv[0], 0xCAFEBABEu);
    wasm_runtime_module_free(e.inst, ptr);
    destroy_env(e);
}
#endif
#endif /* WASM_ENABLE_MEMORY64 */

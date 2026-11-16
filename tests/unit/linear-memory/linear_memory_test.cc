/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "wasm_runtime_common.h"

#include <string>
#include <vector>

/*
 * Shared by the build directories under tests/unit/linear-memory/. Each
 * directory passes these as compile definitions:
 *   WASM_ENABLE_MEMORY64     1 for memory64 (i64 addresses), 0 for memory32
 *   WASM_ENABLE_SHARED_HEAP  1 when a shared heap is attached before each call
 *   WASM_ENABLE_AOT          1 to load the .aot module, 0 to load the .wasm
 *                            module
 * The expectations are the classic-interp results, which AOT must match.
 */

#if WASM_ENABLE_MEMORY64
static const uint64 MAX_ADDR = UINT64_MAX;
static const char *MODULE_BASE = "access_mem64";
#else
static const uint64 MAX_ADDR = UINT32_MAX;
static const char *MODULE_BASE = "access_mem32";
#endif

static const uint64 MEM_SIZE = 65536;
static const uint64 HEAP_SIZE = 65536;
static const uint64 HEAP_START = MAX_ADDR - HEAP_SIZE + 1;
static const uint32 FILL_VALUE = 0x5A;
static const char *OOB_MSG = "out of bounds memory access";

enum Expect { OK, TRAP };

/* Backing store of the shared heap, page aligned for pre_allocated_addr */
alignas(4096) static uint8 heap_buf[HEAP_SIZE];

struct Module {
    uint8 *file_buf = nullptr;
    wasm_module_t module = nullptr;
    wasm_module_inst_t inst = nullptr;
    wasm_exec_env_t exec = nullptr;
};

/* Build the argument cells of one call; i64 takes two cells */
class ArgBuf
{
  public:
    void i32(uint32 v) { cells_[size_++] = v; }

    void i64(uint64 v)
    {
        PUT_I64_TO_ADDR(cells_ + size_, v);
        size_ += 2;
    }

    void addr(uint64 v)
    {
#if WASM_ENABLE_MEMORY64
        i64(v);
#else
        i32((uint32)v);
#endif
    }

    uint32 *cells() { return cells_; }
    uint32 size() const { return size_; }

  private:
    uint32 cells_[8] = {};
    uint32 size_ = 0;
};

/* Each byte of memory holds a value that depends on its address */
static uint8
pattern(uint64 app_addr)
{
    return (uint8)(app_addr * 31 + 7);
}

static uint8 *
app_to_native(Module &m, uint64 app_addr)
{
    return (uint8 *)wasm_runtime_addr_app_to_native(m.inst, app_addr);
}

static void
reset_memory(Module &m)
{
    uint8 *linear = app_to_native(m, 0);
    for (uint64 i = 0; i < MEM_SIZE; i++) {
        linear[i] = pattern(i);
    }

#if WASM_ENABLE_SHARED_HEAP
    uint8 *heap = app_to_native(m, HEAP_START);
    for (uint64 i = 0; i < HEAP_SIZE; i++) {
        heap[i] = pattern(HEAP_START + i);
    }
#endif
}

/* True when no byte of linear memory or the shared heap has changed */
static bool
memory_pristine(Module &m)
{
    uint8 *linear = app_to_native(m, 0);
    for (uint64 i = 0; i < MEM_SIZE; i++) {
        if (linear[i] != pattern(i)) {
            return false;
        }
    }

#if WASM_ENABLE_SHARED_HEAP
    uint8 *heap = app_to_native(m, HEAP_START);
    for (uint64 i = 0; i < HEAP_SIZE; i++) {
        if (heap[i] != pattern(HEAP_START + i)) {
            return false;
        }
    }
#endif
    return true;
}

static uint64
read_le(const uint8 *p, uint32 width)
{
    uint64 v = 0;
    for (uint32 i = 0; i < width; i++) {
        v |= (uint64)p[i] << (8 * i);
    }
    return v;
}

static uint64
width_mask(uint32 width)
{
    return width == 8 ? UINT64_MAX : (((uint64)1 << (8 * width)) - 1);
}

static bool
load_module(Module &m)
{
    std::string path = get_test_binary_dir() + "/" + MODULE_BASE;
#if WASM_ENABLE_AOT
    path += ".aot";
#else
    path += ".wasm";
#endif

    uint32 size = 0;
    char error_buf[128] = { 0 };
    m.file_buf = (uint8 *)bh_read_file_to_buffer(path.c_str(), &size);
    if (!m.file_buf) {
        return false;
    }

    m.module =
        wasm_runtime_load(m.file_buf, size, error_buf, sizeof(error_buf));
    if (!m.module) {
        return false;
    }

    m.inst = wasm_runtime_instantiate(m.module, 16 * 1024, 0, error_buf,
                                      sizeof(error_buf));
    if (!m.inst) {
        return false;
    }

    m.exec = wasm_runtime_create_exec_env(m.inst, 16 * 1024);
    if (!m.exec) {
        return false;
    }

#if WASM_ENABLE_SHARED_HEAP
    SharedHeapInitArgs args = {};
    args.size = HEAP_SIZE;
    args.pre_allocated_addr = heap_buf;
    WASMSharedHeap *heap = wasm_runtime_create_shared_heap(&args);
    if (!heap) {
        return false;
    }

    if (!wasm_runtime_attach_shared_heap(m.inst, heap)) {
        return false;
    }
#endif
    return true;
}

static void
unload_module(Module &m)
{
#if WASM_ENABLE_SHARED_HEAP
    if (m.inst) {
        wasm_runtime_detach_shared_heap(m.inst);
    }
#endif
    if (m.exec) {
        wasm_runtime_destroy_exec_env(m.exec);
    }
    if (m.inst) {
        wasm_runtime_deinstantiate(m.inst);
    }
    if (m.module) {
        wasm_runtime_unload(m.module);
    }
    if (m.file_buf) {
        wasm_runtime_free(m.file_buf);
    }
}

/* Call an export. On trap, returns false and sets exception. */
static bool
call_export(Module &m, const std::string &name, ArgBuf &args,
            std::string &exception)
{
    wasm_function_inst_t func =
        wasm_runtime_lookup_function(m.inst, name.c_str());
    if (!func) {
        ADD_FAILURE() << "missing export " << name;
        return false;
    }

    wasm_runtime_clear_exception(m.inst);
    if (wasm_runtime_call_wasm(m.exec, func, args.size(), args.cells())) {
        return true;
    }

    const char *text = wasm_runtime_get_exception(m.inst);
    exception = text ? text : "";
    return false;
}

static void
expect_trapped(Module &m, const std::string &exception)
{
    EXPECT_NE(std::string::npos, exception.find(OOB_MSG)) << exception;
    EXPECT_TRUE(memory_pristine(m)) << "trapping access modified memory";
}

static Expect
pick(Expect no_heap, Expect heap)
{
    return WASM_ENABLE_SHARED_HEAP ? heap : no_heap;
}

/* One scalar access; effective is the address the access touches */
struct Scalar {
    const char *label;
    uint64 effective;
    Expect no_heap;
    Expect heap;
};

static std::vector<Scalar>
scalar_cases(uint32 width)
{
    std::vector<Scalar> cases = {
        { "S1 linear inside", 16, OK, OK },
        { "S2 linear end", MEM_SIZE - width, OK, OK },
        { "S3 linear past end", MEM_SIZE - width + 1, TRAP, TRAP },
        { "S4 heap end", MAX_ADDR - width + 1, TRAP, OK },
        { "S5 before heap", HEAP_START - 1, TRAP, TRAP },
    };
    if (width >= 2) {
        cases.push_back(
            { "S6 across heap end", MAX_ADDR - width + 2, TRAP, TRAP });
    }
    return cases;
}

static void
check_load(Module &m, const std::string &name, uint32 width, bool result_i64,
           bool takes_addr, uint64 param, uint64 effective, Expect expect)
{
    SCOPED_TRACE(name);
    reset_memory(m);

    ArgBuf args;
    if (takes_addr) {
        args.addr(param);
    }

    std::string exception;
    bool ok = call_export(m, name, args, exception);
    if (expect == OK) {
        ASSERT_TRUE(ok) << exception;
        uint64 expected = read_le(app_to_native(m, effective), width);
        uint64 got = result_i64 ? (uint64)GET_I64_FROM_ADDR(args.cells())
                                : args.cells()[0];
        EXPECT_EQ(expected, got);
    }
    else {
        EXPECT_FALSE(ok);
        expect_trapped(m, exception);
    }
}

static void
check_store(Module &m, const std::string &name, uint32 width, bool takes_addr,
            uint64 param, uint64 effective, Expect expect)
{
    SCOPED_TRACE(name);
    reset_memory(m);

    const uint64 value = 0x0123456789ABCDEF;
    ArgBuf args;
    if (takes_addr) {
        args.addr(param);
    }
    if (width == 8) {
        args.i64(value);
    }
    else {
        args.i32((uint32)value);
    }

    std::string exception;
    bool ok = call_export(m, name, args, exception);
    if (expect == OK) {
        ASSERT_TRUE(ok) << exception;
        EXPECT_EQ(value & width_mask(width),
                  read_le(app_to_native(m, effective), width));
    }
    else {
        EXPECT_FALSE(ok);
        expect_trapped(m, exception);
    }
}

/* One bulk case. The expectations are per run mode, see pick(). */
struct Bulk {
    const char *label;
    const char *cc_export;
    uint64 dst;
    uint64 src;
    uint64 len;
    Expect no_heap;
    Expect heap;
};

static void
check_fill(Module &m, const std::string &name, ArgBuf &args, uint64 dst,
           uint64 len, Expect expect)
{
    SCOPED_TRACE(name);
    reset_memory(m);

    std::string exception;
    bool ok = call_export(m, name, args, exception);
    if (expect == OK) {
        ASSERT_TRUE(ok) << exception;
        const uint8 *p = len ? app_to_native(m, dst) : nullptr;
        bool match = true;
        for (uint64 i = 0; i < len; i++) {
            if (p[i] != FILL_VALUE) {
                match = false;
                break;
            }
        }
        EXPECT_TRUE(match) << "fill did not write the expected bytes";
    }
    else {
        EXPECT_FALSE(ok);
        expect_trapped(m, exception);
    }
}

static void
check_copy(Module &m, const std::string &name, ArgBuf &args, uint64 dst,
           uint64 src, uint64 len, Expect expect)
{
    SCOPED_TRACE(name);
    reset_memory(m);

    std::string exception;
    bool ok = call_export(m, name, args, exception);
    if (expect == OK) {
        ASSERT_TRUE(ok) << exception;
        const uint8 *p = len ? app_to_native(m, dst) : nullptr;
        bool match = true;
        for (uint64 i = 0; i < len; i++) {
            if (p[i] != pattern(src + i)) {
                match = false;
                break;
            }
        }
        EXPECT_TRUE(match) << "copy did not write the source bytes";
    }
    else {
        EXPECT_FALSE(ok);
        expect_trapped(m, exception);
    }
}

static const Bulk fill_cases[] = {
    { "B1", "fill_cc_b1", 16, 0, 16, OK, OK },
    { "B3", "fill_cc_b3", 0, 0, 0, OK, OK },
    { "B4", "fill_cc_b4", HEAP_START, 0, 0, TRAP, OK },
    { "B5", "fill_cc_b5", MAX_ADDR, 0, 0, TRAP, OK },
    { "B6", "fill_cc_b6", MAX_ADDR, 0, 1, TRAP, OK },
    { "B7", "fill_cc_b7", HEAP_START, 0, MEM_SIZE, TRAP, OK },
    { "B11", "fill_cc_b11", MAX_ADDR - 1, 0, 4, TRAP, TRAP },
    { "B12", "fill_cc_b12", HEAP_START - 1, 0, 2, TRAP, TRAP },
    { "B13", "fill_cc_b13", MAX_ADDR - 0x10FF, 0, 0x1100, TRAP, OK },
    { "B14", "fill_cc_b14", MEM_SIZE, 0, 0, OK, OK },
    { "B15", "fill_cc_b15", MEM_SIZE + 1, 0, 0, TRAP, TRAP },
};

static const Bulk copy_cases[] = {
    { "B2", "copy_cc_b2", 0, 16, 16, OK, OK },
    { "B8", "copy_cc_b8", 16, MAX_ADDR, 1, TRAP, OK },
    { "B9", "copy_cc_b9", MAX_ADDR, 16, 1, TRAP, OK },
    { "B10", "copy_cc_b10", 0, HEAP_START, MEM_SIZE, TRAP, OK },
};

class linear_memory_test : public testing::Test
{
  protected:
    WAMRRuntimeRAII<512 * 1024> runtime;
};

TEST_F(linear_memory_test, bulk_fill)
{
    Module m;
    ASSERT_TRUE(load_module(m));

    for (const Bulk &c : fill_cases) {
        SCOPED_TRACE(c.label);
        Expect expect = pick(c.no_heap, c.heap);

        /* Variable form: dst, val and len are parameters */
        ArgBuf pp;
        pp.addr(c.dst);
        pp.i32(FILL_VALUE);
        pp.addr(c.len);
        check_fill(m, "fill_pp", pp, c.dst, c.len, expect);

        /* Constant form: dst and len are both constants */
        ArgBuf none;
        check_fill(m, c.cc_export, none, c.dst, c.len, expect);

        /* Mixed forms: only one operand is a constant */
        if (c.dst == MAX_ADDR) {
            ArgBuf cdst;
            cdst.i32(FILL_VALUE);
            cdst.addr(c.len);
            check_fill(m, "fill_cdst_max", cdst, c.dst, c.len, expect);
        }
        if (c.len == 1) {
            ArgBuf clen;
            clen.addr(c.dst);
            clen.i32(FILL_VALUE);
            check_fill(m, "fill_pdst_clen1", clen, c.dst, c.len, expect);
        }
    }

    unload_module(m);
}

TEST_F(linear_memory_test, bulk_copy)
{
    Module m;
    ASSERT_TRUE(load_module(m));

    for (const Bulk &c : copy_cases) {
        SCOPED_TRACE(c.label);
        Expect expect = pick(c.no_heap, c.heap);

        ArgBuf pp;
        pp.addr(c.dst);
        pp.addr(c.src);
        pp.addr(c.len);
        check_copy(m, "copy_pp", pp, c.dst, c.src, c.len, expect);

        ArgBuf none;
        check_copy(m, c.cc_export, none, c.dst, c.src, c.len, expect);

        if (c.src == MAX_ADDR) {
            ArgBuf csrc;
            csrc.addr(c.dst);
            csrc.addr(c.len);
            check_copy(m, "copy_csrc_max", csrc, c.dst, c.src, c.len, expect);
        }
        if (c.dst == MAX_ADDR) {
            ArgBuf cdst;
            cdst.addr(c.src);
            cdst.addr(c.len);
            check_copy(m, "copy_pdst_max", cdst, c.dst, c.src, c.len, expect);
        }
    }

    unload_module(m);
}

TEST_F(linear_memory_test, scalar_load)
{
    Module m;
    ASSERT_TRUE(load_module(m));

    const uint32 widths[] = { 1, 2, 4, 8 };
    for (uint32 width : widths) {
        const std::string base = "ld" + std::to_string(width * 8);
        const bool result_i64 = width == 8;

        for (uint64 offset : { (uint64)0, (uint64)16 }) {
            for (const char *align : { "a1", "an" }) {
                std::string name =
                    base + "_p_o" + std::to_string(offset) + "_" + align;
                for (const Scalar &s : scalar_cases(width)) {
                    SCOPED_TRACE(s.label);
                    check_load(m, name, width, result_i64, true,
                               s.effective - offset, s.effective,
                               pick(s.no_heap, s.heap));
                }
            }
        }

        check_load(m, base + "_c_end", width, result_i64, false, 0,
                   MAX_ADDR - width + 1, pick(TRAP, OK));
        check_load(m, base + "_c_lin", width, result_i64, false, 0,
                   MEM_SIZE - width, OK);
    }

    /* i64 result, zero-extended from 1, 2 and 4 bytes */
    const uint32 narrow[] = { 1, 2, 4 };
    for (uint32 width : narrow) {
        const std::string base = "ld" + std::to_string(width * 8) + "_i64";

        for (uint64 offset : { (uint64)0, (uint64)16 }) {
            for (const char *align : { "a1", "an" }) {
                std::string name =
                    base + "_p_o" + std::to_string(offset) + "_" + align;
                for (const Scalar &s : scalar_cases(width)) {
                    SCOPED_TRACE(s.label);
                    check_load(m, name, width, true, true, s.effective - offset,
                               s.effective, pick(s.no_heap, s.heap));
                }
            }
        }

        check_load(m, base + "_c_end", width, true, false, 0,
                   MAX_ADDR - width + 1, pick(TRAP, OK));
        check_load(m, base + "_c_lin", width, true, false, 0, MEM_SIZE - width,
                   OK);
    }

    /* Static offset 16 on MAX-7 wraps; the effective address is out of range */
    check_load(m, "ld8_c_wrap_o16", 1, false, false, 0, MAX_ADDR - 7 + 16,
               TRAP);

    unload_module(m);
}

TEST_F(linear_memory_test, scalar_store)
{
    Module m;
    ASSERT_TRUE(load_module(m));

    const uint32 widths[] = { 1, 2, 4, 8 };
    for (uint32 width : widths) {
        const std::string base = "st" + std::to_string(width * 8);

        for (const Scalar &s : scalar_cases(width)) {
            SCOPED_TRACE(s.label);
            check_store(m, base + "_p_o0_a1", width, true, s.effective,
                        s.effective, pick(s.no_heap, s.heap));
            check_store(m, base + "_p_o16_an", width, true, s.effective - 16,
                        s.effective, pick(s.no_heap, s.heap));
        }

        check_store(m, base + "_c_end", width, false, 0, MAX_ADDR - width + 1,
                    pick(TRAP, OK));
    }

    unload_module(m);
}

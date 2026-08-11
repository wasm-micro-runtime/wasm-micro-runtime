/*
 * Copyright (C) 2024 Xiaomi Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "wasm_runtime_common.h"

#include <gtest/gtest-spi.h>

#include <cinttypes>
#include <thread>
#include <vector>
#include <iostream>
#include <cstring>

class shared_heap_test : public testing::Test
{
  protected:
    virtual void SetUp() {}
    static void SetUpTestCase() {}
    virtual void TearDown() {}
    WAMRRuntimeRAII<512 * 1024> runtime;
};

struct ret_env {
    wasm_exec_env_t exec_env;
    wasm_module_t wasm_module;
    wasm_module_inst_t wasm_module_inst;
    unsigned char *wasm_file_buf;
    char error_buf[128];
};

static void
destroy_module_env(struct ret_env module_env);

static bool
load_wasm_with_mode(const char *wasm_file_tested, unsigned int app_heap_size,
                    ret_env &ret_module_env, bool use_fast_jit)
{
    char *wasm_file = strdup(wasm_file_tested);
    unsigned int wasm_file_size = 0;
    unsigned int stack_size = 16 * 1024, heap_size = app_heap_size;
    char error_buf[128] = {};

    ret_module_env.wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(wasm_file, &wasm_file_size);
    if (!ret_module_env.wasm_file_buf) {
        ADD_FAILURE() << "Failed to read wasm file buffer: " << wasm_file;
        goto fail;
    }

    ret_module_env.wasm_module =
        wasm_runtime_load(ret_module_env.wasm_file_buf, wasm_file_size,
                          error_buf, sizeof(error_buf));
    if (!ret_module_env.wasm_module) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        ADD_FAILURE() << "Failed to load wasm module: " << wasm_file
                      << " with error: " << error_buf;
        goto fail;
    }

    ret_module_env.wasm_module_inst =
        wasm_runtime_instantiate(ret_module_env.wasm_module, stack_size,
                                 heap_size, error_buf, sizeof(error_buf));
    if (!ret_module_env.wasm_module_inst) {
        memcpy(ret_module_env.error_buf, error_buf, 128);
        ADD_FAILURE() << "Failed to instantiate wasm module: " << wasm_file
                      << " with error: " << error_buf;
        goto fail;
    }

#if WASM_ENABLE_FAST_JIT != 0
    if (use_fast_jit) {
        if (!wasm_runtime_set_running_mode(ret_module_env.wasm_module_inst,
                                           Mode_Fast_JIT)) {
            ADD_FAILURE() << "Failed to set fast-jit running mode: "
                          << wasm_file;
            goto fail;
        }
    }
#else
    (void)use_fast_jit;
#endif

    ret_module_env.exec_env = wasm_runtime_create_exec_env(
        ret_module_env.wasm_module_inst, stack_size);
    if (!ret_module_env.exec_env) {
        ADD_FAILURE() << "Failed to create wasm execution environment: "
                      << wasm_file;
        goto fail;
    }

    free(wasm_file);
    return true;
fail:
    free(wasm_file);
    destroy_module_env(ret_module_env);
    return false;
}

static bool
load_wasm(const char *wasm_file_tested, unsigned int app_heap_size,
          ret_env &ret_module_env)
{
    return load_wasm_with_mode(wasm_file_tested, app_heap_size, ret_module_env,
                               false);
}

void
destroy_module_env(struct ret_env module_env)
{
    if (module_env.exec_env) {
        wasm_runtime_destroy_exec_env(module_env.exec_env);
    }

    if (module_env.wasm_module_inst) {
        wasm_runtime_deinstantiate(module_env.wasm_module_inst);
    }

    if (module_env.wasm_module) {
        wasm_runtime_unload(module_env.wasm_module);
    }

    if (module_env.wasm_file_buf) {
        wasm_runtime_free(module_env.wasm_file_buf);
    }
}

static void
test_shared_heap_with_mode(WASMSharedHeap *shared_heap, const char *file,
                           const char *func_name, uint32 argc, uint32 argv[],
                           bool use_fast_jit)
{
    struct ret_env tmp_module_env;
    WASMFunctionInstanceCommon *func_test = nullptr;
    bool ret = false;

    if (!load_wasm_with_mode((char *)file, 0, tmp_module_env, use_fast_jit)) {
        ADD_FAILURE() << "Failed to load wasm file\n";
        goto fail0;
    }

    if (!wasm_runtime_attach_shared_heap(tmp_module_env.wasm_module_inst,
                                         shared_heap)) {
        ADD_FAILURE() << "Failed to attach shared heap\n";
        goto fail1;
    }

    func_test = wasm_runtime_lookup_function(tmp_module_env.wasm_module_inst,
                                             func_name);
    if (!func_test) {
        ADD_FAILURE() << "Failed to wasm_runtime_lookup_function!\n";
        goto fail2;
    }

    ret =
        wasm_runtime_call_wasm(tmp_module_env.exec_env, func_test, argc, argv);
    if (!ret) {
        const char *s =
            wasm_runtime_get_exception(tmp_module_env.wasm_module_inst);
        ADD_FAILURE() << "Failed to wasm_runtime_call_wasm with "
                      << "exception: " << s;
    }

fail2:
    wasm_runtime_detach_shared_heap(tmp_module_env.wasm_module_inst);
fail1:
    destroy_module_env(tmp_module_env);
fail0:
    return;
}

static void
test_shared_heap(WASMSharedHeap *shared_heap, const char *file,
                 const char *func_name, uint32 argc, uint32 argv[])
{
    test_shared_heap_with_mode(shared_heap, file, func_name, argc, argv,
                               false);
}

TEST_F(shared_heap_test, test_shared_heap_basic)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {};

    args.size = os_getpagesize();
    shared_heap = wasm_runtime_create_shared_heap(&args);

    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    test_shared_heap(shared_heap, "test.wasm", "test", 0, argv);
    EXPECT_EQ(10, argv[0]);

    test_shared_heap(shared_heap, "test.aot", "test", 0, argv);
    EXPECT_EQ(10, argv[0]);

    test_shared_heap(shared_heap, "test_chain.aot", "test", 0, argv);
    EXPECT_EQ(10, argv[0]);
}

#if WASM_ENABLE_FAST_JIT != 0
TEST_F(shared_heap_test, test_shared_heap_basic_fast_jit)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {};

    args.size = os_getpagesize();
    shared_heap = wasm_runtime_create_shared_heap(&args);

    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    /* Bytecode under Mode_Fast_JIT: loads/stores must honor shared heap. */
    test_shared_heap_with_mode(shared_heap, "test.wasm", "test", 0, argv, true);
    EXPECT_EQ(10, argv[0]);
}
#endif

TEST_F(shared_heap_test, test_shared_heap_malloc_fail)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {};

    args.size = os_getpagesize();
    shared_heap = wasm_runtime_create_shared_heap(&args);

    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    argv[0] = os_getpagesize();
    test_shared_heap(shared_heap, "test.wasm", "test_malloc_fail", 1, argv);
    EXPECT_EQ(1, argv[0]);

    argv[0] = os_getpagesize();
    test_shared_heap(shared_heap, "test.aot", "test_malloc_fail", 1, argv);
    EXPECT_EQ(1, argv[0]);

    argv[0] = os_getpagesize();
    test_shared_heap(shared_heap, "test_chain.aot", "test_malloc_fail", 1,
                     argv);
    EXPECT_EQ(1, argv[0]);
}

TEST_F(shared_heap_test, test_preallocated_shared_heap_malloc_fail)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    /* create a preallocated shared heap */
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    /* test wasm can't malloc with preallocated shared heap */
    argv[0] = 1024;
    test_shared_heap(shared_heap, "test.wasm", "my_shared_heap_malloc", 1,
                     argv);
    EXPECT_EQ(0, argv[0]);

    argv[0] = 1024;
    test_shared_heap(shared_heap, "test.aot", "my_shared_heap_malloc", 1, argv);
    EXPECT_EQ(0, argv[0]);

    argv[0] = 1024;
    test_shared_heap(shared_heap, "test_chain.aot", "my_shared_heap_malloc", 1,
                     argv);
    EXPECT_EQ(0, argv[0]);
}

TEST_F(shared_heap_test, test_preallocated_shared_runtime_api)
{
    struct ret_env tmp_module_env;
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {};
    void *native_ptr;
    uint64 offset, size;
    bool ret;

    args.size = 0x4000;
    shared_heap = wasm_runtime_create_shared_heap(&args);

    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    if (!load_wasm("test.wasm", 0, tmp_module_env)) {
        FAIL() << "Failed to load wasm file\n";
    }

    if (!wasm_runtime_attach_shared_heap(tmp_module_env.wasm_module_inst,
                                         shared_heap)) {
        ADD_FAILURE() << "Failed to attach shared heap\n";
        goto fail1;
    }

    offset = wasm_runtime_shared_heap_malloc(tmp_module_env.wasm_module_inst,
                                             32, &native_ptr);
    if (!offset) {
        ADD_FAILURE() << "Failed to attach shared heap\n";
        goto fail2;
    }

    size = (uint64_t)UINT32_MAX + 0x2000;
    printf("offset %" PRIx64 " size: %" PRIx64 "\n", offset, size);
    ASSERT_EQ(false, wasm_runtime_validate_app_addr(
                         tmp_module_env.wasm_module_inst, offset, size));

    ASSERT_EQ(NULL, wasm_runtime_addr_app_to_native(
                        tmp_module_env.wasm_module_inst, offset + size));

    size = (uint64_t)10;
    ASSERT_EQ(true, wasm_runtime_validate_app_addr(
                        tmp_module_env.wasm_module_inst, offset, size));

    ASSERT_EQ((char *)native_ptr + size,
              wasm_runtime_addr_app_to_native(tmp_module_env.wasm_module_inst,
                                              offset + size));

fail2:
    wasm_runtime_detach_shared_heap(tmp_module_env.wasm_module_inst);
fail1:
    destroy_module_env(tmp_module_env);
}

static void
create_test_shared_heap(uint8 *preallocated_buf, size_t size,
                        WASMSharedHeap **shared_heap_res)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    args.pre_allocated_addr = preallocated_buf;
    args.size = size;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    *shared_heap_res = shared_heap;
    if (!*shared_heap_res) {
        FAIL() << "Create shared heap chain failed.\n";
    }
}

static void
create_test_shared_heap_chain(uint8 *preallocated_buf, size_t size,
                              uint8 *preallocated_buf2, size_t size2,
                              WASMSharedHeap **shared_heap_chain)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr;
    args.pre_allocated_addr = preallocated_buf;
    args.size = size;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf2;
    args.size = size2;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    *shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!*shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }
}

TEST_F(shared_heap_test, test_shared_heap_rmw)
{
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[2] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint32 start1, end1;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    create_test_shared_heap(preallocated_buf_ptr, BUF_SIZE, &shared_heap);

    /* app addr for shared heap */
    start1 = UINT32_MAX - BUF_SIZE + 1;
    end1 = UINT32_MAX;

    argv[0] = end1;
    argv[1] = 101;
    test_shared_heap(shared_heap, "test.wasm", "read_modify_write_8", 2, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 101);

    argv[0] = start1;
    argv[1] = 37;
    test_shared_heap(shared_heap, "test.wasm", "read_modify_write_8", 2, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 37);

    argv[0] = end1;
    argv[1] = 81;
    test_shared_heap(shared_heap, "test.aot", "read_modify_write_8", 2, argv);
    EXPECT_EQ(101, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 81);

    argv[0] = start1;
    argv[1] = 98;
    test_shared_heap(shared_heap, "test.aot", "read_modify_write_8", 2, argv);
    EXPECT_EQ(37, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 98);
}

#if WASM_ENABLE_FAST_JIT != 0
TEST_F(shared_heap_test, test_shared_heap_rmw_fast_jit)
{
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[2] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint32 start1, end1;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    create_test_shared_heap(preallocated_buf_ptr, BUF_SIZE, &shared_heap);

    start1 = UINT32_MAX - BUF_SIZE + 1;
    end1 = UINT32_MAX;

    argv[0] = end1;
    argv[1] = 101;
    test_shared_heap_with_mode(shared_heap, "test.wasm", "read_modify_write_8",
                               2, argv, true);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 101);

    argv[0] = start1;
    argv[1] = 37;
    test_shared_heap_with_mode(shared_heap, "test.wasm", "read_modify_write_8",
                               2, argv, true);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 37);
}
#endif

TEST_F(shared_heap_test, test_shared_heap_chain_rmw)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[2] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint32 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT32_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT32_MAX - BUF_SIZE;
    start2 = UINT32_MAX - BUF_SIZE + 1;
    end2 = UINT32_MAX;

    /* shared heap 1 */
    argv[0] = end1;
    argv[1] = 101;
    test_shared_heap(shared_heap_chain, "test.wasm", "read_modify_write_8", 2,
                     argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 101);

    /* shared heap 2 */
    argv[0] = start2;
    argv[1] = 129;
    test_shared_heap(shared_heap_chain, "test.wasm", "read_modify_write_8", 2,
                     argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf2[0], 129);

    argv[0] = start1;
    argv[1] = 98;
    test_shared_heap(shared_heap_chain, "test_chain.aot", "read_modify_write_8",
                     2, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 98);

    argv[0] = end2;
    argv[1] = 81;
    test_shared_heap(shared_heap_chain, "test_chain.aot", "read_modify_write_8",
                     2, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf2[BUF_SIZE - 1], 81);
}

TEST_F(shared_heap_test, test_shared_heap_chain_rmw_bulk_memory)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[3] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint32 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT32_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT32_MAX - BUF_SIZE;
    start2 = UINT32_MAX - BUF_SIZE + 1;
    end2 = UINT32_MAX;

    argv[0] = end1;
    argv[1] = 101;
    argv[2] = 1;
    test_shared_heap(shared_heap_chain, "test_bulk_memory.wasm",
                     "memory_fill_test", 3, argv);
    /* no modification since no return value */
    EXPECT_EQ(end1, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 101);

    argv[0] = start1;
    argv[1] = 14;
    argv[2] = 1;
    test_shared_heap(shared_heap_chain, "test_bulk_memory_chain.aot",
                     "memory_fill_test", 3, argv);
    /* no modification since no return value */
    EXPECT_EQ(start1, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 14);

    /* nothing happen when memory fill 0 byte */
    argv[0] = start2;
    argv[1] = 68;
    argv[2] = 0;
    test_shared_heap(shared_heap_chain, "test_bulk_memory_chain.aot",
                     "memory_fill_test", 3, argv);
    /* no modification since no return value */
    EXPECT_EQ(start2, argv[0]);
    EXPECT_EQ(preallocated_buf2[0], 0);

    argv[0] = end2;
    argv[1] = 98;
    argv[2] = 1;
    test_shared_heap(shared_heap_chain, "test_bulk_memory_chain.aot",
                     "memory_fill_test", 3, argv);
    /* no modification since no return value */
    EXPECT_EQ(end2, argv[0]);
    EXPECT_EQ(preallocated_buf2[BUF_SIZE - 1], 98);
}

TEST_F(shared_heap_test, test_shared_heap_chain_rmw_bulk_memory_oob)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[3] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint32 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT32_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT32_MAX - BUF_SIZE;
    start2 = UINT32_MAX - BUF_SIZE + 1;
    end2 = UINT32_MAX;

    /* shared heap 1 */
    argv[0] = end1;
    argv[1] = 101;
    argv[2] = 2;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory.wasm",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = end2;
    argv[1] = 98;
    argv[2] = 2;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory.wasm",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = start1;
    argv[1] = 98;
    argv[2] = BUF_SIZE + 1;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory.wasm",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = start2;
    argv[1] = 98;
    argv[2] = BUF_SIZE + 1;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory.wasm",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = end1;
    argv[1] = 101;
    argv[2] = 2;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory_chain.aot",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = end2;
    argv[1] = 98;
    argv[2] = 2;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory_chain.aot",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = start1;
    argv[1] = 98;
    argv[2] = BUF_SIZE + 1;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory_chain.aot",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");

    argv[0] = start2;
    argv[1] = 98;
    argv[2] = BUF_SIZE + 1;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_bulk_memory_chain.aot",
                                             "memory_fill_test", 3, argv),
                            "Exception: out of bounds memory access");
}

TEST_F(shared_heap_test, test_shared_heap_rmw_oob)
{
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[2] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint32 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap(preallocated_buf_ptr, BUF_SIZE, &shared_heap);

    /* app addr for shared heap */
    start1 = UINT32_MAX - BUF_SIZE + 1;
    end1 = UINT32_MAX;

    /* try to rmw an u16, first u8 is in the first shared heap and second u8 is
     * in the second shared heap, will be seen as oob */
    argv[0] = end1;
    argv[1] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap, "test.wasm",
                                             "read_modify_write_16", 2, argv),
                            "Exception: out of bounds memory access");

    argv[0] = start1 - 1;
    argv[1] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap, "test.aot",
                                             "read_modify_write_16", 2, argv),
                            "Exception: out of bounds memory access");

    argv[0] = end1;
    argv[1] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap, "test.aot",
                                             "read_modify_write_16", 2, argv),
                            "Exception: out of bounds memory access");
}

TEST_F(shared_heap_test, test_shared_heap_chain_rmw_oob)
{
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[2] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint32 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT32_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT32_MAX - BUF_SIZE;
    start2 = UINT32_MAX - BUF_SIZE + 1;
    end2 = UINT32_MAX;

    /* try to rmw an u16, first u8 is in the first shared heap and second u8 is
     * in the second shared heap, will be seen as oob */
    argv[0] = end2;
    argv[1] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain, "test.wasm",
                                             "read_modify_write_16", 2, argv),
                            "Exception: out of bounds memory access");

    argv[0] = end1;
    argv[1] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_chain.aot",
                                             "read_modify_write_16", 2, argv),
                            "Exception: out of bounds memory access");
}

#if WASM_ENABLE_MEMORY64 != 0
TEST_F(shared_heap_test, test_shared_heap_chain_memory64_rmw)
{
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[3] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint64 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT64_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT64_MAX - BUF_SIZE;
    start2 = UINT64_MAX - BUF_SIZE + 1;
    end2 = UINT64_MAX;

    /* shared heap 1 */
    PUT_I64_TO_ADDR(argv, end1);
    argv[2] = 101;
    test_shared_heap(shared_heap_chain, "test64.wasm", "read_modify_write_8", 3,
                     argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[BUF_SIZE - 1], 101);

    /* shared heap 2 */
    PUT_I64_TO_ADDR(argv, start2);
    argv[2] = 129;
    test_shared_heap(shared_heap_chain, "test64.wasm", "read_modify_write_8", 3,
                     argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf2[0], 129);

    PUT_I64_TO_ADDR(argv, start1);
    argv[2] = 98;
    test_shared_heap(shared_heap_chain, "test64_chain.aot",
                     "read_modify_write_8", 3, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf[0], 98);

    PUT_I64_TO_ADDR(argv, end2);
    argv[2] = 81;
    test_shared_heap(shared_heap_chain, "test64_chain.aot",
                     "read_modify_write_8", 3, argv);
    EXPECT_EQ(0, argv[0]);
    EXPECT_EQ(preallocated_buf2[BUF_SIZE - 1], 81);
}

TEST_F(shared_heap_test, test_shared_heap_chain_memory64_rmw_oob)
{
    WASMSharedHeap *shared_heap_chain = nullptr;
    uint32 argv[3] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    uint64 start1, end1, start2, end2;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    create_test_shared_heap_chain(preallocated_buf_ptr, BUF_SIZE,
                                  preallocated_buf2_ptr, BUF_SIZE,
                                  &shared_heap_chain);

    /* app addr for shared heap */
    start1 = UINT64_MAX - 2 * BUF_SIZE + 1;
    end1 = UINT64_MAX - BUF_SIZE;
    start2 = UINT64_MAX - BUF_SIZE + 1;
    end2 = UINT64_MAX;

    /* try to rmw an u16, first u8 is in the first shared heap and second u8 is
     * in the second shared heap, will be seen as oob */
    PUT_I64_TO_ADDR(argv, end1);
    argv[2] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain, "test64.wasm",
                                             "read_modify_write_16", 3, argv),
                            "Exception: out of bounds memory access");

    PUT_I64_TO_ADDR(argv, end1);
    argv[2] = 12025;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test64_chain.aot",
                                             "read_modify_write_16", 3, argv),
                            "Exception: out of bounds memory access");
}
#endif

#ifndef native_function
/* clang-format off */
#define native_function(func_name, signature) \
    { #func_name, (void *)glue_## func_name, signature, NULL }
/* clang-format on */
#endif
#ifndef nitems
#define nitems(_a) (sizeof(_a) / sizeof(0 [(_a)]))
#endif /* nitems */
uintptr_t
glue_test_addr_conv(wasm_exec_env_t env, uintptr_t addr)
{
    wasm_module_inst_t module_inst = get_module_inst(env);
    void *native_addr = (void *)addr;
    uintptr_t app_addr = addr_native_to_app(native_addr);

    native_addr = addr_app_to_native(app_addr);
    if (native_addr != (void *)addr) {
        ADD_FAILURE() << "address conversion incorrect";
        return 0;
    }
    return app_addr;
}

static NativeSymbol g_test_native_symbols[] = {
    native_function(test_addr_conv, "(*)i"),
};

TEST_F(shared_heap_test, test_addr_conv)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {};
    bool ret = false;

    ret = wasm_native_register_natives("env", g_test_native_symbols,
                                       nitems(g_test_native_symbols));
    if (!ret) {
        FAIL() << "Failed to register natives";
    }

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    test_shared_heap(shared_heap, "test_addr_conv.wasm", "test", 0, argv);
    EXPECT_EQ(1, argv[0]);

    test_shared_heap(shared_heap, "test_addr_conv.aot", "test", 0, argv);
    EXPECT_EQ(1, argv[0]);

    test_shared_heap(shared_heap, "test_addr_conv_chain.aot", "test", 0, argv);
    EXPECT_EQ(1, argv[0]);
}

TEST_F(shared_heap_test, test_addr_conv_pre_allocated_oob)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize(),
           app_addr = 0xFFFFFFFF - BUF_SIZE;
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    bool ret = false;

    /* create a preallocated shared heap */
    ret = wasm_native_register_natives("env", g_test_native_symbols,
                                       nitems(g_test_native_symbols));
    if (!ret) {
        FAIL() << "Failed to register natives";
    }

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    argv[0] = app_addr;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap, "test_addr_conv.wasm",
                                             "test_preallocated", 1, argv),
                            "Exception: out of bounds memory access");

    argv[0] = app_addr;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap, "test_addr_conv.aot",
                                             "test_preallocated", 1, argv),
                            "Exception: out of bounds memory access");

    argv[0] = app_addr;
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap,
                                             "test_addr_conv_chain.aot",
                                             "test_preallocated", 1, argv),
                            "Exception: out of bounds memory access");
}

TEST_F(shared_heap_test, test_shared_heap_chain)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    bool ret = false;

    ret = wasm_native_register_natives("env", g_test_native_symbols,
                                       nitems(g_test_native_symbols));
    if (!ret) {
        FAIL() << "Failed to register natives";
    }

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    /* create a preallocated shared heap */
    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    test_shared_heap(shared_heap_chain, "test_addr_conv.wasm", "test", 0, argv);
    EXPECT_EQ(1, argv[0]);

    test_shared_heap(shared_heap, "test_addr_conv.aot", "test", 0, argv);
    EXPECT_EQ(1, argv[0]);
}

TEST_F(shared_heap_test, test_shared_heap_chain_create_fail)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    args.size = 4096;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    EXPECT_EQ(shared_heap_chain, nullptr);
}

TEST_F(shared_heap_test, test_shared_heap_chain_create_fail2)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    struct ret_env tmp_module_env;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    if (!load_wasm((char *)"test.wasm", 0, tmp_module_env)) {
        FAIL() << "Failed to load wasm file\n";
    }

    if (!wasm_runtime_attach_shared_heap(tmp_module_env.wasm_module_inst,
                                         shared_heap)) {
        FAIL() << "Failed to attach shared heap\n";
    }

    /* can't create shared heap chain when shared heap is attached to a wasm
     * app */
    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    EXPECT_EQ(shared_heap_chain, nullptr);

    wasm_runtime_detach_shared_heap(tmp_module_env.wasm_module_inst);
    destroy_module_env(tmp_module_env);
}

TEST_F(shared_heap_test, test_shared_heap_chain_create_fail3)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap3 = nullptr, *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf2_ptr;
    args.size = BUF_SIZE;
    shared_heap3 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap3) {
        FAIL() << "Failed to create shared heap";
    }

    /* The head and body can't be already in other shared heap chain as body */
    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap3, shared_heap2);
    EXPECT_EQ(shared_heap_chain, nullptr);
    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap2, shared_heap);
    EXPECT_EQ(shared_heap_chain, nullptr);
}

TEST_F(shared_heap_test, test_shared_heap_chain_unchain)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap3 = nullptr, *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint8 *preallocated_buf2_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    std::vector<uint8> preallocated_buf2(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    preallocated_buf2_ptr = preallocated_buf2.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    ASSERT_NE(preallocated_buf2_ptr, nullptr);

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf2_ptr;
    args.size = BUF_SIZE;
    shared_heap3 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap3) {
        FAIL() << "Failed to create shared heap";
    }

    /* unchain shared heap so that the 'body' can be another chain 'body'
     * again(1->2 to 1->3->2) */
    EXPECT_EQ(shared_heap2,
              wasm_runtime_unchain_shared_heaps(shared_heap_chain, false));
    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap3, shared_heap2);
    EXPECT_EQ(shared_heap_chain, shared_heap3);
    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap3);
    EXPECT_EQ(shared_heap, shared_heap_chain);

    /* break down the entire shared heap chain */
    EXPECT_EQ(shared_heap2,
              wasm_runtime_unchain_shared_heaps(shared_heap_chain, true));
}

TEST_F(shared_heap_test, test_shared_heap_chain_reset_runtime_managed)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;
    uint8 buf_size = 64;
    uint64 offset = 0, offset_after_reset = 0;
    void *native_ptr = nullptr, *native_ptr_after_reset = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    struct ret_env tmp_module_env;

    args.size = 4096;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    args.size = BUF_SIZE;
    args.pre_allocated_addr = preallocated_buf_ptr;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Failed to create second shared heap";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    if (!load_wasm((char *)"test.wasm", 0, tmp_module_env)) {
        FAIL() << "Failed to load wasm file\n";
    }

    if (!wasm_runtime_attach_shared_heap(tmp_module_env.wasm_module_inst,
                                         shared_heap_chain)) {
        destroy_module_env(tmp_module_env);
        FAIL() << "Failed to attach shared heap chain";
    }

    offset = wasm_runtime_shared_heap_malloc(tmp_module_env.wasm_module_inst,
                                             buf_size, &native_ptr);
    ASSERT_NE(0u, offset);
    ASSERT_NE(nullptr, native_ptr);

    memset(native_ptr, 0x5A, buf_size);
    for (uint8 i = 0; i < buf_size; i++) {
        EXPECT_EQ(0x5A, *((uint8 *)native_ptr + i));
    }

    wasm_runtime_detach_shared_heap(tmp_module_env.wasm_module_inst);
    EXPECT_TRUE(wasm_runtime_reset_shared_heap_chain(shared_heap_chain));

    if (!load_wasm((char *)"test.wasm", 0, tmp_module_env)) {
        FAIL() << "Failed to load wasm file after reset\n";
    }

    if (!wasm_runtime_attach_shared_heap(tmp_module_env.wasm_module_inst,
                                         shared_heap_chain)) {
        destroy_module_env(tmp_module_env);
        FAIL() << "Failed to attach shared heap chain after reset";
    }

    offset_after_reset = wasm_runtime_shared_heap_malloc(
        tmp_module_env.wasm_module_inst, buf_size, &native_ptr_after_reset);
    ASSERT_NE(0u, offset_after_reset);
    ASSERT_NE(nullptr, native_ptr_after_reset);

    EXPECT_EQ(offset, offset_after_reset);
    EXPECT_EQ(native_ptr, native_ptr_after_reset);

    /* Only on some platform, the os_mmap will memset the memory to 0
        for (uint8 i = 0; i < buf_size; i++) {
            EXPECT_EQ(0, *((uint8 *)native_ptr_after_reset + i));
        }
    */

    wasm_runtime_detach_shared_heap(tmp_module_env.wasm_module_inst);
    destroy_module_env(tmp_module_env);
}

TEST_F(shared_heap_test, test_shared_heap_chain_reset_preallocated)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    uint8 set_val = 0xA5;

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    memset(preallocated_buf_ptr, set_val, BUF_SIZE);
    for (uint32 i = 0; i < BUF_SIZE; i++) {
        EXPECT_EQ(set_val, preallocated_buf[i]);
    }

    EXPECT_TRUE(wasm_runtime_reset_shared_heap_chain(shared_heap));

    for (uint32 i = 0; i < BUF_SIZE; i++) {
        EXPECT_EQ(0, preallocated_buf[i]);
    }
}

TEST_F(shared_heap_test, test_shared_heap_chain_reset_attached_fail)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    struct ret_env module_env = {};
    bool ret;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    ret = load_wasm((char *)"test.wasm", 0, module_env);
    if (!ret) {
        FAIL() << "Failed to load wasm";
    }

    ret = wasm_runtime_attach_shared_heap(module_env.wasm_module_inst,
                                          shared_heap);
    if (!ret) {
        destroy_module_env(module_env);
        FAIL() << "Failed to attach shared heap";
    }

    EXPECT_FALSE(wasm_runtime_reset_shared_heap_chain(shared_heap));

    wasm_runtime_detach_shared_heap(module_env.wasm_module_inst);
    destroy_module_env(module_env);

    EXPECT_TRUE(wasm_runtime_reset_shared_heap_chain(shared_heap));
}

TEST_F(shared_heap_test, test_shared_heap_chain_addr_conv)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    bool ret = false;

    ret = wasm_native_register_natives("env", g_test_native_symbols,
                                       nitems(g_test_native_symbols));
    if (!ret) {
        FAIL() << "Failed to register natives";
    }

    args.size = 4096;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    /* create a preallocated shared heap */
    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    argv[0] = 0xFFFFFFFF;
    test_shared_heap(shared_heap_chain, "test_addr_conv.wasm",
                     "test_preallocated", 1, argv);
    EXPECT_EQ(1, argv[0]);

    argv[0] = 0xFFFFF000;
    test_shared_heap(shared_heap_chain, "test_addr_conv.wasm",
                     "test_preallocated", 1, argv);
    EXPECT_EQ(1, argv[0]);

    argv[0] = 0xFFFFFFFF;
    test_shared_heap(shared_heap, "test_addr_conv_chain.aot",
                     "test_preallocated", 1, argv);
    EXPECT_EQ(1, argv[0]);

    argv[0] = 0xFFFFF000;
    test_shared_heap(shared_heap, "test_addr_conv_chain.aot",
                     "test_preallocated", 1, argv);
    EXPECT_EQ(1, argv[0]);
}

TEST_F(shared_heap_test, test_shared_heap_chain_addr_conv_oob)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr, *shared_heap2 = nullptr,
                   *shared_heap_chain = nullptr;
    uint32 argv[1] = {}, BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);
    bool ret = false;

    ret = wasm_native_register_natives("env", g_test_native_symbols,
                                       nitems(g_test_native_symbols));
    if (!ret) {
        FAIL() << "Failed to register natives";
    }

    args.size = os_getpagesize();
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    /* create a preallocated shared heap */
    memset(&args, 0, sizeof(args));
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap2 = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap2) {
        FAIL() << "Create preallocated shared heap failed.\n";
    }

    shared_heap_chain =
        wasm_runtime_chain_shared_heaps(shared_heap, shared_heap2);
    if (!shared_heap_chain) {
        FAIL() << "Create shared heap chain failed.\n";
    }

    /* test wasm */
    argv[0] = 0xFFFFFFFF - BUF_SIZE - os_getpagesize();
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_addr_conv.wasm",
                                             "test_preallocated", 1, argv),
                            "Exception: out of bounds memory access");

    /* test aot */
    argv[0] = 0xFFFFFFFF - BUF_SIZE - os_getpagesize();
    EXPECT_NONFATAL_FAILURE(test_shared_heap(shared_heap_chain,
                                             "test_addr_conv_chain.aot",
                                             "test_preallocated", 1, argv),
                            "Exception: out of bounds memory access");
}

/* Test the behavior of the shared heap in a multi-threaded environment to
 * ensure thread safety and data consistency. */

TEST_F(shared_heap_test, test_shared_heap_multithread_access)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = { 0 };
    const int num_threads = 2;
    std::vector<std::thread> threads;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    auto thread_func = [&shared_heap]() {
        wasm_runtime_init_thread_env();
        uint32 local_argv[1] = { 0 };
        test_shared_heap(shared_heap, "test.wasm", "test", 0, local_argv);
        wasm_runtime_destroy_thread_env();
        EXPECT_EQ(10, local_argv[0]);
    };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func);
    }

    for (auto &thread : threads) {
        thread.join();
    }
}

TEST_F(shared_heap_test, test_shared_heap_concurrent_access)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    auto thread_func = [&](uint8 *buf, int value) {
        for (uint32 i = 0; i < BUF_SIZE; ++i) {
            buf[i] = value;
        }
    };

    // Using std::ref to solve array parameter passing problem
    std::thread t1(thread_func, preallocated_buf_ptr, 0xAA);
    std::thread t2(thread_func, preallocated_buf_ptr, 0x55);

    t1.join();
    t2.join();

    //  Verify shared memory consistency (last thread wins)
    for (uint32 i = 0; i < BUF_SIZE; ++i) {
        EXPECT_TRUE(preallocated_buf[i] == 0xAA || preallocated_buf[i] == 0x55)
            << "Data corruption detected";
    }
}

TEST_F(shared_heap_test, test_shared_heap_cross_instance)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;
    uint32 start1;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;

    /* app addr for shared heap */
    start1 = UINT32_MAX - BUF_SIZE + 1;

    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    struct ret_env module_env1, module_env2;

    ASSERT_TRUE(load_wasm((char *)"test.wasm", 0, module_env1))
        << "Failed to load wasm file for instance 1";
    ASSERT_TRUE(load_wasm((char *)"test.wasm", 0, module_env2))
        << "Failed to load wasm file for instance 2";

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(module_env1.wasm_module_inst,
                                                shared_heap));
    ASSERT_TRUE(wasm_runtime_attach_shared_heap(module_env2.wasm_module_inst,
                                                shared_heap));

    // Instance 1 writes to shared memory
    uint32 argv1[2] = { start1, 123 };
    test_shared_heap(shared_heap, "test.wasm", "read_modify_write_8", 2, argv1);

    // Instance 2 reads from shared memory
    uint32 argv2[2] = { start1, 0 };
    test_shared_heap(shared_heap, "test.wasm", "read_modify_write_8", 2, argv2);

    EXPECT_EQ(argv2[0], 123);

    wasm_runtime_detach_shared_heap(module_env1.wasm_module_inst);
    wasm_runtime_detach_shared_heap(module_env2.wasm_module_inst);
    destroy_module_env(module_env1);
    destroy_module_env(module_env2);
}

TEST_F(shared_heap_test, test_shared_heap_lifecycle)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    preallocated_buf[0] = 42;
    EXPECT_EQ(preallocated_buf[0], 42);

    // Manually zeroing preallocated memory to simulate deallocation
    memset(preallocated_buf_ptr, 0, BUF_SIZE);

    // Check if it is still accessible after clearing
    EXPECT_EQ(preallocated_buf[0], 0)
        << "Memory not properly cleared after manual cleanup";
}

// Unaligned memory access
TEST_F(shared_heap_test, test_shared_heap_unaligned_access)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize();
    uint8 *preallocated_buf_ptr = nullptr;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    // Write unaligned data
    uint16 *unaligned_ptr = (uint16 *)(preallocated_buf_ptr + 1);
    *unaligned_ptr = 0xABCD;

    // Read back unaligned data
    uint16 result = *(uint16 *)(preallocated_buf_ptr + 1);
    EXPECT_EQ(result, 0xABCD);
}

// test_sandbox.wasm
TEST_F(shared_heap_test, test_memory_size_and_growth)
{
    uint32 argv[1] = { 0 };
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    // Load the WebAssembly module
    struct ret_env module_env;
    ASSERT_TRUE(load_wasm((char *)"test_sandbox.wasm", 0, module_env))
        << "Failed to load wasm file";

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(module_env.wasm_module_inst,
                                                shared_heap))
        << "Failed to attach shared heap";

    // Test initial memory size
    WASMFunctionInstanceCommon *func_memsize = wasm_runtime_lookup_function(
        module_env.wasm_module_inst, "memory_size");
    ASSERT_NE(func_memsize, nullptr) << "Failed to find 'memory_size' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_memsize, 0, argv));
    EXPECT_EQ(argv[0], 1) << "Initial memory size should be 1 page";

    // Test growing memory by 1 page
    argv[0] = 1;
    WASMFunctionInstanceCommon *func_grow = wasm_runtime_lookup_function(
        module_env.wasm_module_inst, "grow_memory");
    ASSERT_NE(func_grow, nullptr) << "Failed to find 'grow_memory' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_grow, 1, argv));
    EXPECT_EQ(argv[0], 1)
        << "Memory growth result should be the previous size (1 page)";

    // Verify new memory size (2 pages)
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_memsize, 0, argv));
    EXPECT_EQ(argv[0], 2) << "Memory size should now be 2 pages";

    // Test growing memory to maximum size
    argv[0] = 2;
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_grow, 1, argv));
    EXPECT_EQ(argv[0], 2)
        << "Memory growth result should be the previous size (2 pages)";

    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_memsize, 0, argv));
    EXPECT_EQ(argv[0], 4) << "Memory size should now be 4 pages (maximum)";

    // Test exceeding maximum memory size
    argv[0] = 1;
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_grow, 1, argv));
    EXPECT_EQ(int32_t(argv[0]), -1)
        << "Memory growth should fail when exceeding max size";

    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_memsize, 0, argv));
    EXPECT_EQ(argv[0], 4) << "Memory size should remain 4 pages";

    wasm_runtime_detach_shared_heap(module_env.wasm_module_inst);
    destroy_module_env(module_env);
}

TEST_F(shared_heap_test, test_store_and_load)
{
    uint32 argv[2] = { 0 };
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    // Load the WebAssembly module
    struct ret_env module_env;
    ASSERT_TRUE(load_wasm((char *)"test_sandbox.wasm", 0, module_env))
        << "Failed to load wasm file";

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(module_env.wasm_module_inst,
                                                shared_heap))
        << "Failed to attach shared heap";

    // Store value 100 at address 4
    argv[0] = 4;   // Address
    argv[1] = 100; // Value
    WASMFunctionInstanceCommon *func_store =
        wasm_runtime_lookup_function(module_env.wasm_module_inst, "store");
    ASSERT_NE(func_store, nullptr) << "Failed to find 'store' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_store, 2, argv));

    // Load value from address 4
    argv[0] = 4; // Address
    WASMFunctionInstanceCommon *func_load =
        wasm_runtime_lookup_function(module_env.wasm_module_inst, "load");
    ASSERT_NE(func_load, nullptr) << "Failed to find 'load' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_load, 1, argv));
    EXPECT_EQ(argv[0], 100) << "Loaded value should be 100";

    wasm_runtime_detach_shared_heap(module_env.wasm_module_inst);
    destroy_module_env(module_env);
}

TEST_F(shared_heap_test, test_unaligned_store_and_load)
{
    uint32 argv[2] = { 0 };
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    ASSERT_NE(shared_heap, nullptr) << "Failed to create shared heap";

    // Load the WebAssembly module
    struct ret_env module_env;
    ASSERT_TRUE(load_wasm((char *)"test_sandbox.wasm", 0, module_env))
        << "Failed to load wasm file";

    ASSERT_TRUE(wasm_runtime_attach_shared_heap(module_env.wasm_module_inst,
                                                shared_heap))
        << "Failed to attach shared heap";

    // Store value 42 at unaligned address 1
    argv[0] = 1;  // Address
    argv[1] = 42; // Value
    WASMFunctionInstanceCommon *func_store =
        wasm_runtime_lookup_function(module_env.wasm_module_inst, "store");
    ASSERT_NE(func_store, nullptr) << "Failed to find 'store' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_store, 2, argv));

    // Load value from unaligned address 1
    argv[0] = 1; // Address
    WASMFunctionInstanceCommon *func_load =
        wasm_runtime_lookup_function(module_env.wasm_module_inst, "load");
    ASSERT_NE(func_load, nullptr) << "Failed to find 'load' function";
    ASSERT_TRUE(
        wasm_runtime_call_wasm(module_env.exec_env, func_load, 1, argv));
    EXPECT_EQ(argv[0], 42) << "Loaded value should be 42";

    wasm_runtime_detach_shared_heap(module_env.wasm_module_inst);
    destroy_module_env(module_env);
}

// Test Case: Invalid size parameter
TEST_F(shared_heap_test, test_shared_heap_invalid_size)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;

    // Test size = 0
    args.size = 0;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_EQ(shared_heap, nullptr)
        << "Expected shared_heap to be NULL for size=0";

    // Test size < APP_HEAP_SIZE_MIN
    args.size = APP_HEAP_SIZE_MIN - 1;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_NE(shared_heap, nullptr)
        << "Expected shared_heap will not be NULL even Size < "
           "APP_HEAP_SIZE_MIN due to align_uint";

    // Test size > APP_HEAP_SIZE_MAX
    args.size = APP_HEAP_SIZE_MAX + 1;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_EQ(shared_heap, nullptr)
        << "Expected shared_heap to be NULL for Size > APP_HEAP_SIZE_MAX";

    // Test size not aligned to system page size
    args.size = os_getpagesize() - 1; // Non-aligned size
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_NE(shared_heap, nullptr) << "Expected shared_heap will not be NULL "
                                       "for Non-aligned size due to align_uint";
}

// Test Case: Invalid pre-allocated address parameter
TEST_F(shared_heap_test, test_shared_heap_invalid_pre_allocated_addr)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 BUF_SIZE = os_getpagesize(); // System page size
    uint8 *preallocated_buf_ptr = nullptr;

    ASSERT_GT(BUF_SIZE, 0u);
    std::vector<uint8> preallocated_buf(BUF_SIZE);
    preallocated_buf_ptr = preallocated_buf.data();
    ASSERT_NE(preallocated_buf_ptr, nullptr);

    // Test NULL pre-allocated address with invalid size
    args.pre_allocated_addr = nullptr;
    args.size = 0; // Invalid size
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_EQ(shared_heap, nullptr)
        << "Expected shared_heap to be NULL for NULL pre_allocated_addr and "
           "invalid size";

    // Test mismatched size with pre-allocated address
    args.pre_allocated_addr = preallocated_buf_ptr;
    args.size = BUF_SIZE - 1; // Size not aligned
    shared_heap = wasm_runtime_create_shared_heap(&args);
    EXPECT_EQ(shared_heap, nullptr)
        << "Expected shared_heap to be NULL for pre_allocated_addr with "
           "mismatched size";
}

TEST_F(shared_heap_test, test_divide_by_zero)
{
    struct ret_env module_env;
    uint32 argv[1] = { 42 };
    bool ret = false;

    if (!load_wasm((char *)"test_runtime.wasm", 0, module_env)) {
        FAIL() << "Failed to load test_runtime.wasm file";
        return;
    }

    WASMFunctionInstanceCommon *func_test = wasm_runtime_lookup_function(
        module_env.wasm_module_inst, "divide_by_zero");
    if (!func_test) {
        FAIL() << "Failed to find divide_by_zero function";
        destroy_module_env(module_env);
        return;
    }

    ret = wasm_runtime_call_wasm(module_env.exec_env, func_test, 1, argv);
    if (!ret) {
        const char *exception =
            wasm_runtime_get_exception(module_env.wasm_module_inst);
        if (exception && strstr(exception, "integer divide by zero")) {
            SUCCEED() << "Caught expected divide by zero exception: "
                      << exception;
        }
        else {
            FAIL() << "Unexpected exception occurred: "
                   << (exception ? exception : "unknown");
        }
    }
    else {
        FAIL() << "Expected divide by zero exception, but function executed "
                  "successfully";
    }

    destroy_module_env(module_env);
}

TEST_F(shared_heap_test, test_shared_heap_multithread_performance)
{
    SharedHeapInitArgs args = {};
    WASMSharedHeap *shared_heap = nullptr;
    uint32 argv[1] = { 0 };
    const int num_threads = 20;
    std::vector<std::thread> threads;

    args.size = 1024;
    shared_heap = wasm_runtime_create_shared_heap(&args);
    if (!shared_heap) {
        FAIL() << "Failed to create shared heap";
    }

    auto thread_func = [&shared_heap]() {
        wasm_runtime_init_thread_env();
        uint32 local_argv[1] = { 0 };
        test_shared_heap(shared_heap, "test.wasm", "test", 0, local_argv);
        EXPECT_EQ(10, local_argv[0]);
        wasm_runtime_destroy_thread_env();
    };

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(thread_func);
    }

    for (auto &thread : threads) {
        thread.join();
    }
    // Declare a mem_alloc_info_t struct to store memory allocation details
    mem_alloc_info_t info;

    // Call the function to fetch memory allocation details
    wasm_runtime_get_mem_alloc_info(&info);

    // Print memory allocation details
    std::cout << "After inst" << std::endl;
    std::cout << "Total size: " << info.total_size << std::endl;
    std::cout << "Total free size: " << info.total_free_size << std::endl;
    std::cout << "Highmark size: " << info.highmark_size << std::endl;
}

#include <random> // Include this for std::random_device and std::mt19937

std::vector<uint8_t>
generate_random_data(size_t size)
{
    std::vector<uint8_t> data(size);
    std::random_device rd;  // Random number generator
    std::mt19937 gen(rd()); // Mersenne Twister engine
    std::uniform_int_distribution<uint8_t> dis(0, 255); // Uniform distribution

    for (size_t i = 0; i < size; i++) {
        data[i] = dis(gen);
    }
    return data;
}

// Fuzzing test for wasm_runtime_load
TEST_F(shared_heap_test, test_FuzzWasmRuntimeLoad)
{
    const size_t max_size = 1024 * 1024; // 1 MB max size
    auto random_data = generate_random_data(max_size);

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(
        random_data.data(), random_data.size(), error_buf, sizeof(error_buf));

    if (module != NULL) {
        wasm_runtime_unload(module);
    }
    else {
        printf("Error message: %s\n", error_buf);
    }
}

// Fuzzing test for wasm_runtime_instantiate
TEST_F(shared_heap_test, test_FuzzWasmRuntimeInstantiate)
{
    const size_t max_size = 1024 * 1024; // 1 MB max size
    auto random_data = generate_random_data(max_size);

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(
        random_data.data(), random_data.size(), error_buf, sizeof(error_buf));

    if (module != NULL) {
        wasm_module_inst_t module_inst = wasm_runtime_instantiate(
            module, 8092, 8092, error_buf, sizeof(error_buf));
        if (module_inst != NULL) {
            wasm_runtime_deinstantiate(module_inst);
        }
        else {
            printf("Error message: %s\n", error_buf);
        }
        wasm_runtime_unload(module);
    }
}

// Fuzzing test for wasm_runtime_call_wasm
TEST_F(shared_heap_test, test_FuzzWasmRuntimeCallWasm)
{
    const size_t max_size = 1024 * 1024; // 1 MB max size
    auto random_data = generate_random_data(max_size);

    char error_buf[128];
    wasm_module_t module = wasm_runtime_load(
        random_data.data(), random_data.size(), error_buf, sizeof(error_buf));

    if (module != NULL) {
        wasm_module_inst_t module_inst = wasm_runtime_instantiate(
            module, 8092, 8092, error_buf, sizeof(error_buf));
        if (module_inst != NULL) {
            wasm_exec_env_t exec_env =
                wasm_runtime_create_exec_env(module_inst, 8092);
            if (exec_env != NULL) {
                wasm_function_inst_t func = wasm_runtime_lookup_function(
                    module_inst, "some_function_name");
                if (func != NULL) {
                    uint32_t wasm_argv[4] = { 0 };
                    wasm_runtime_call_wasm(exec_env, func, 4, wasm_argv);
                }
                wasm_runtime_destroy_exec_env(exec_env);
            }
            wasm_runtime_deinstantiate(module_inst);
        }
        wasm_runtime_unload(module);
    }
}
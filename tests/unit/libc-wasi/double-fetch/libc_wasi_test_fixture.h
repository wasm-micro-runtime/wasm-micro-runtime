/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * Tests for the iovec handling of libc-wasi fd and socket APIs.
 *
 * The guest iovec array can be modified by another wasm thread while the
 * host reads it. These cases check the normal and out-of-range paths, and a
 * concurrent case where a wasi-threads thread rewrites the array during
 * sock_send.
 */

#ifndef LIBC_WASI_TEST_FIXTURE_H
#define LIBC_WASI_TEST_FIXTURE_H

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "wasm_export.h"
#include "bh_read_file.h"

/* guest layout used by the iovec_ops module */
static const uint32_t IOVS_OFF = 0x100;  /* iovec array */
static const uint32_t DATA_OFF = 0x1000; /* payload bytes */
static const uint32_t OUT_OFF = 0x200;   /* nwritten/nread output */
static const uint32_t OUT2_OFF = 0x204;  /* second output, e.g. roflags */

/* WASI errno values */
static const uint32_t WASI_EBADF = 8;
static const uint32_t WASI_EINVAL = 28;

typedef struct guest_iovec {
    uint32_t buf_offset;
    uint32_t buf_len;
} guest_iovec_t;

class LibcWasiTest : public ::testing::Test
{
  protected:
    static constexpr uint32_t STACK_SIZE = 32768;
    static constexpr uint32_t HEAP_SIZE = 65536;

    bool runtime_inited = false;
    wasm_module_t module = nullptr;
    wasm_module_inst_t inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
    uint8_t *file_buf = nullptr;

    void SetUp() override
    {
        RuntimeInitArgs init_args;
        memset(&init_args, 0, sizeof(init_args));
        init_args.mem_alloc_type = Alloc_With_Allocator;
        init_args.mem_alloc_option.allocator.malloc_func = (void *)malloc;
        init_args.mem_alloc_option.allocator.realloc_func = (void *)realloc;
        init_args.mem_alloc_option.allocator.free_func = (void *)free;

        ASSERT_TRUE(wasm_runtime_full_init(&init_args));
        runtime_inited = true;
    }

    void TearDown() override
    {
        if (exec_env)
            wasm_runtime_destroy_exec_env(exec_env);
        if (inst)
            wasm_runtime_deinstantiate(inst);
        if (module)
            wasm_runtime_unload(module);
        if (file_buf)
            BH_FREE(file_buf);
        if (runtime_inited)
            wasm_runtime_destroy();
    }

    /* Load and instantiate a fixture; asserts on failure. */
    /* addr_pool, when given, is the WASI address pool of the module. */
    void load_module(const char *wasm_file,
                     const char *const *addr_pool = nullptr,
                     uint32_t addr_pool_size = 0)
    {
        uint32_t buf_size = 0;
        char error_buf[256] = { 0 };

        file_buf = (uint8_t *)bh_read_file_to_buffer(wasm_file, &buf_size);
        ASSERT_NE(file_buf, nullptr) << "Cannot read " << wasm_file;

        module =
            wasm_runtime_load(file_buf, buf_size, error_buf, sizeof(error_buf));
        ASSERT_NE(module, nullptr) << "Load failed: " << error_buf;

        wasm_runtime_set_wasi_args(module, nullptr, 0, nullptr, 0, nullptr, 0,
                                   nullptr, 0);
        if (addr_pool != nullptr)
            wasm_runtime_set_wasi_addr_pool(module, (const char **)addr_pool,
                                            addr_pool_size);

        inst = wasm_runtime_instantiate(module, STACK_SIZE, HEAP_SIZE,
                                        error_buf, sizeof(error_buf));
        ASSERT_NE(inst, nullptr) << "Instantiate failed: " << error_buf;

        exec_env = wasm_runtime_create_exec_env(inst, STACK_SIZE);
        ASSERT_NE(exec_env, nullptr);
    }

    /*
     * Call an exported function. argv holds the wasm arguments as 32-bit
     * cells, and on success argv[0] holds the first result. Returns false
     * when the call raised an exception.
     */
    bool call(const char *name, std::vector<uint32_t> &argv)
    {
        wasm_function_inst_t func = wasm_runtime_lookup_function(inst, name);
        EXPECT_NE(func, nullptr) << "Function not found: " << name;
        if (!func)
            return false;

        if (argv.empty())
            argv.push_back(0);
        return wasm_runtime_call_wasm(exec_env, func, (uint32_t)argv.size(),
                                      argv.data());
    }

    /*
     * A guest pointer that fails validation makes the runtime raise
     * "out of bounds memory access", which aborts the call. Check that the
     * call was rejected this way.
     */
    void expect_rejected(const char *name, std::vector<uint32_t> &argv)
    {
        ASSERT_FALSE(call(name, argv));
        const char *exception = wasm_runtime_get_exception(inst);
        ASSERT_NE(exception, nullptr);
        EXPECT_NE(strstr(exception, "out of bounds memory access"), nullptr)
            << "exception: " << exception;
        wasm_runtime_clear_exception(inst);
    }

    uint8_t *guest(uint32_t offset)
    {
        return (uint8_t *)wasm_runtime_addr_app_to_native(inst, offset);
    }

    void write_u32(uint32_t offset, uint32_t value)
    {
        memcpy(guest(offset), &value, sizeof(value));
    }

    uint32_t read_u32(uint32_t offset)
    {
        uint32_t value = 0;
        memcpy(&value, guest(offset), sizeof(value));
        return value;
    }

    /* Write one iovec entry at IOVS_OFF. */
    void set_iovec(uint32_t buf_offset, uint32_t buf_len)
    {
        guest_iovec_t iov = { buf_offset, buf_len };
        memcpy(guest(IOVS_OFF), &iov, sizeof(iov));
    }

    /* Place one payload and one iovec entry that points at it. */
    void set_single_iovec(const char *payload, uint32_t iov_off,
                          uint32_t data_off, uint32_t len)
    {
        memcpy(guest(data_off), payload, len);
        guest_iovec_t iov = { data_off, len };
        memcpy(guest(iov_off), &iov, sizeof(iov));
    }
};

#endif /* LIBC_WASI_TEST_FIXTURE_H */

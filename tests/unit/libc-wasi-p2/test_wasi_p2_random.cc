/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern "C" {
    #include "wasi_p2_random.h"
    #include "wasi_p2_common.h"
}

class WasiP2RandomTest : public testing::Test {
protected:
    void SetUp() override {
        wasm_runtime_init();
    }

    void TearDown() override {
        wasm_runtime_destroy();
    }
};

TEST_F(WasiP2RandomTest, wasi_random_get_random_bytes) {
    wasi_list_u8_t ret1;
    wasi_random_get_random_bytes(16, &ret1);
    ASSERT_EQ(ret1.buf_len, 16);
    ASSERT_NE(ret1.buf, nullptr);

    // Check that the bytes are not all the same
    bool all_same = true;
    for (size_t i = 1; i < ret1.buf_len; i++) {
        if (ret1.buf[i] != ret1.buf[0]) {
            all_same = false;
            break;
        }
    }
    ASSERT_FALSE(all_same);

    wasi_list_u8_t ret2;
    wasi_random_get_random_bytes(16, &ret2);
    ASSERT_EQ(ret2.buf_len, 16);
    ASSERT_NE(ret2.buf, nullptr);

    ASSERT_NE(memcmp(ret1.buf, ret2.buf, 16), 0);

    wasm_runtime_free(ret1.buf);
    wasm_runtime_free(ret2.buf);
}

TEST_F(WasiP2RandomTest, wasi_random_get_random_u64) {
    uint64_t val1 = wasi_random_get_random_u64();
    uint64_t val2 = wasi_random_get_random_u64();
    ASSERT_NE(val1, val2);
}

TEST_F(WasiP2RandomTest, wasi_random_get_insecure_random_bytes) {
    wasi_list_u8_t buf1, buf2;
    wasi_random_get_insecure_random_bytes(16, &buf1);
    ASSERT_EQ(buf1.buf_len, 16);
    ASSERT_NE(buf1.buf, nullptr);

    wasi_random_get_insecure_random_bytes(16, &buf2);
    ASSERT_NE(memcmp(buf1.buf, buf2.buf, 16), 0);
    wasm_runtime_free(buf1.buf);
    wasm_runtime_free(buf2.buf);
}

TEST_F(WasiP2RandomTest, wasi_random_get_insecure_random_u64) {
    uint64_t val1 = wasi_random_get_insecure_random_u64();
    uint64_t val2 = wasi_random_get_insecure_random_u64();
    ASSERT_NE(val1, val2);
}

TEST_F(WasiP2RandomTest, wasi_random_insecure_seed) {
    uint64_t seed1, seed2;
    wasi_random_get_insecure_seed(&seed1, &seed2);
    ASSERT_NE(seed1, 0);
    ASSERT_NE(seed2, 0);
    ASSERT_NE(seed1, seed2);
}

TEST_F(WasiP2RandomTest, wasi_random_get_random_bytes_different_sizes) {
    wasi_list_u8_t ret;

    // Test with size 0
    wasi_random_get_random_bytes(0, &ret);
    ASSERT_EQ(ret.buf_len, 0);

    // Test with size 1
    wasi_random_get_random_bytes(1, &ret);
    ASSERT_EQ(ret.buf_len, 1);
    ASSERT_NE(ret.buf, nullptr);
    wasm_runtime_free(ret.buf);

    // Test with a larger size
    wasi_random_get_random_bytes(1024, &ret);
    ASSERT_EQ(ret.buf_len, 1024);
    ASSERT_NE(ret.buf, nullptr);
    wasm_runtime_free(ret.buf);
}

#define NUM_THREADS 10
#define NUM_ITERATIONS 1000

static void *insecure_random_thread_func(void *arg) {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        wasi_random_get_insecure_random_u64();
    }
    return NULL;
}

TEST_F(WasiP2RandomTest, wasi_random_get_insecure_random_u64_multithreaded) {
    pthread_t threads[NUM_THREADS];

    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, insecure_random_thread_func, NULL);
        ASSERT_EQ(ret, 0);
    }

    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_join(threads[i], NULL);
        ASSERT_EQ(ret, 0);
    }
}

TEST_F(WasiP2RandomTest, wasi_random_invalid_args) {
    // Test get_random_bytes with null pointer
    wasi_random_get_random_bytes(16, NULL);

    // Test get_insecure_random_bytes with null pointer
    wasi_random_get_insecure_random_bytes(16, NULL);

    // Test insecure_seed with null pointers
    wasi_random_get_insecure_seed(NULL, NULL);
}

TEST_F(WasiP2RandomTest, wasi_random_get_insecure_random_bytes_different_sizes) {
    wasi_list_u8_t ret;

    // Test with size 0
    wasi_random_get_insecure_random_bytes(0, &ret);
    ASSERT_EQ(ret.buf_len, 0);

    // Test with size 1
    wasi_random_get_insecure_random_bytes(1, &ret);
    ASSERT_EQ(ret.buf_len, 1);
    ASSERT_NE(ret.buf, nullptr);
    wasm_runtime_free(ret.buf);

    // Test with a larger size
    wasi_random_get_insecure_random_bytes(1024, &ret);
    ASSERT_EQ(ret.buf_len, 1024);
    ASSERT_NE(ret.buf, nullptr);
    wasm_runtime_free(ret.buf);
}

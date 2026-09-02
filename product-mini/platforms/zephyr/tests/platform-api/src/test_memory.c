/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_common.h"

ZTEST_SUITE(pool_memory, NULL, NULL, pool_before, pool_after, NULL);

ZTEST(pool_memory, test_allocate_write_free_and_reuse)
{
    uint8_t *ptr = wasm_runtime_malloc(64U);
    uint8_t *reused;

    zassert_not_null(ptr, "initial allocation failed");
    memset(ptr, 0xA5, 64U);
    for (size_t i = 0; i < 64U; ++i) {
        zassert_equal(ptr[i], 0xA5, "allocated block is not writable");
    }

    wasm_runtime_free(ptr);
    reused = wasm_runtime_malloc(64U);
    zassert_not_null(reused, "freed storage was not reusable");
    memset(reused, 0x5A, 64U);
    for (size_t i = 0; i < 64U; ++i) {
        zassert_equal(reused[i], 0x5A, "reused block is not writable");
    }
    wasm_runtime_free(reused);
}

ZTEST(pool_memory, test_realloc_preserves_existing_bytes)
{
    uint8_t *ptr = wasm_runtime_malloc(64U);

    zassert_not_null(ptr, "initial allocation failed");
    for (size_t i = 0; i < 64U; ++i) {
        ptr[i] = (uint8_t)i;
    }

    ptr = wasm_runtime_realloc(ptr, 128U);
    zassert_not_null(ptr, "reallocation failed");
    for (size_t i = 0; i < 64U; ++i) {
        zassert_equal(ptr[i], (uint8_t)i, "existing byte was not preserved");
    }
    wasm_runtime_free(ptr);
}

ZTEST(pool_memory, test_multiple_allocations_do_not_overlap)
{
    uint8_t *first = wasm_runtime_malloc(64U);
    uint8_t *second = wasm_runtime_malloc(64U);

    zassert_not_null(first, "first allocation failed");
    zassert_not_null(second, "second allocation failed");
    zassert_true((uintptr_t)first + 64U <= (uintptr_t)second
                     || (uintptr_t)second + 64U <= (uintptr_t)first,
                 "allocations overlap");
    memset(first, 0xA5, 64U);
    memset(second, 0x5A, 64U);
    for (size_t i = 0; i < 64U; ++i) {
        zassert_equal(first[i], 0xA5, "first allocation was overwritten");
        zassert_equal(second[i], 0x5A, "second allocation was overwritten");
    }
    wasm_runtime_free(first);
    wasm_runtime_free(second);
}

ZTEST(pool_memory, test_aligned_alloc_returns_aligned_writable_block)
{
    void *ptr = wasm_runtime_aligned_alloc(64U, 32U);
    zassert_not_null(ptr, "32-byte aligned allocation failed");
    zassert_equal((uintptr_t)ptr % 32U, 0U, "incorrect alignment");
    memset(ptr, 0x5A, 64U);
    for (size_t i = 0; i < 64U; ++i) {
        zassert_equal(((uint8_t *)ptr)[i], 0x5A, "block is not writable");
    }
    wasm_runtime_free(ptr);
    zassert_not_null(wasm_runtime_aligned_alloc(64U, 32U),
                     "freed aligned storage was not reusable");
}

ZTEST(pool_memory, test_pool_exhaustion_returns_null)
{
    void *blocks[TEST_POOL_SIZE / 1024U];
    size_t count = 0;

    while (count < ARRAY_SIZE(blocks)) {
        blocks[count] = wasm_runtime_malloc(1024U);
        if (blocks[count] == NULL) {
            break;
        }
        count++;
    }

    zassert_true(count > 0U, "no pool allocations succeeded");
    zassert_true(count < ARRAY_SIZE(blocks),
                 "pointer array filled before pool exhaustion");
    while (count > 0U) {
        wasm_runtime_free(blocks[--count]);
    }
    zassert_not_null(wasm_runtime_malloc(1024U),
                     "pool did not recover after exhaustion");
}

ZTEST(pool_memory, test_aligned_alloc_rejects_invalid_alignment)
{
    zassert_is_null(wasm_runtime_aligned_alloc(64U, 3U),
                    "non-power-of-two alignment accepted");
}

ZTEST(pool_memory, test_aligned_alloc_rejects_invalid_size)
{
    zassert_is_null(wasm_runtime_aligned_alloc(63U, 32U),
                    "size not divisible by alignment accepted");
}

ZTEST(pool_memory, test_pool_recovers_after_fragmentation)
{
    void *first = wasm_runtime_malloc(32U * 1024U);
    void *middle = wasm_runtime_malloc(32U * 1024U);
    void *last = wasm_runtime_malloc(32U * 1024U);
    void *larger;

    zassert_not_null(first, "first allocation failed");
    zassert_not_null(middle, "middle allocation failed");
    zassert_not_null(last, "last allocation failed");
    wasm_runtime_free(middle);
    wasm_runtime_free(first);
    wasm_runtime_free(last);

    larger = wasm_runtime_malloc(64U * 1024U);
    zassert_not_null(larger, "pool did not coalesce fragmented blocks");
    wasm_runtime_free(larger);
}

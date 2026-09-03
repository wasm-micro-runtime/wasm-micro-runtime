/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#if WAMR_BUILD_TEST != 1
#error "WAMR_BUILD_TEST must be defined as 1"
#endif

#include "mem_alloc.h"
#include "ems_gc_internal.h"
#include "wasm_export.h"

/* Test helper: Check if pointer is aligned */
static inline bool
is_aligned(void *ptr, size_t alignment)
{
    return ((uintptr_t)ptr % alignment) == 0;
}

/* Test helper: Check if allocation is aligned (has magic value) */
static inline bool
is_aligned_allocation(gc_object_t obj)
{
    uint32_t *magic_ptr = (uint32_t *)((char *)obj - 4);
    return ((*magic_ptr & ALIGNED_ALLOC_MAGIC_MASK)
            == ALIGNED_ALLOC_MAGIC_VALUE);
}

/* Test: Normal allocation still works (regression) */
static void
test_normal_alloc_basic(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Normal allocation should still work */
    ptr = mem_allocator_malloc(allocator, 128);
    assert_non_null(ptr);

    /* Should be 8-byte aligned */
    assert_true(is_aligned(ptr, 8));

    /* Should NOT be marked as aligned allocation */
    assert_false(is_aligned_allocation(ptr));

    /* Free should work */
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Valid alignment powers of 2 */
static void
test_aligned_alloc_valid_alignments(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[128 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Test each valid alignment */
    int alignments[] = { 8, 16, 32, 64, 128, 256, 512, 1024 };
    int num_alignments = sizeof(alignments) / sizeof(alignments[0]);
    for (int i = 0; i < num_alignments; i++) {
        int align = alignments[i];

        /* Allocate with size = multiple of alignment */
        ptr = mem_allocator_malloc_aligned(allocator, align * 2, align);
        assert_non_null(ptr);

        /* Verify alignment */
        assert_true(is_aligned(ptr, align));

        /* Verify marked as aligned */
        assert_true(is_aligned_allocation(ptr));

        /* Free */
        mem_allocator_free(allocator, ptr);
    }

    mem_allocator_destroy(allocator);
}

/* Test: Realloc rejects aligned allocations */
static void
test_realloc_rejects_aligned(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr, *new_ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate aligned */
    ptr = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(ptr);
    assert_true(is_aligned_allocation(ptr));

    /* Realloc should reject aligned allocation */
    new_ptr = mem_allocator_realloc(allocator, ptr, 256);
    assert_null(new_ptr);

    /* Original pointer should still be valid - free it */
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Realloc still works for normal allocations */
static void
test_normal_realloc_works(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr, *new_ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate normal */
    ptr = mem_allocator_malloc(allocator, 128);
    assert_non_null(ptr);

    /* Write some data */
    memset(ptr, 0xAB, 128);

    /* Realloc should work */
    new_ptr = mem_allocator_realloc(allocator, ptr, 256);
    assert_non_null(new_ptr);

    /* Data should be preserved */
    for (int i = 0; i < 128; i++) {
        assert_int_equal(((unsigned char *)new_ptr)[i], 0xAB);
    }

    mem_allocator_free(allocator, new_ptr);
    mem_allocator_destroy(allocator);
}

/* Test: Invalid alignments (not power of 2 or zero) */
static void
test_aligned_alloc_invalid_not_power_of_2(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* These should all fail (zero or not power of 2) */
    int invalid_alignments[] = { 0, 3, 5, 7, 9, 15, 17, 100 };
    int num_invalid =
        sizeof(invalid_alignments) / sizeof(invalid_alignments[0]);
    for (int i = 0; i < num_invalid; i++) {
        ptr =
            mem_allocator_malloc_aligned(allocator, 128, invalid_alignments[i]);
        assert_null(ptr);
    }

    /* Small powers of 2 should succeed (adjusted to GC_MIN_ALIGNMENT) */
    ptr = mem_allocator_malloc_aligned(allocator, 8, 1);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    ptr = mem_allocator_malloc_aligned(allocator, 8, 2);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    ptr = mem_allocator_malloc_aligned(allocator, 8, 4);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Size must be multiple of alignment */
static void
test_aligned_alloc_size_not_multiple(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Size not multiple of alignment - should fail */
    ptr = mem_allocator_malloc_aligned(allocator, 100, 64);
    assert_null(ptr);

    ptr = mem_allocator_malloc_aligned(allocator, 65, 64);
    assert_null(ptr);

    /* Size is multiple - should succeed */
    ptr = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Mixed normal and aligned allocations */
static void
test_mixed_alloc_interleaved(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[128 * 1024];
    void *normal1, *aligned1, *normal2, *aligned2;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate: normal -> aligned -> normal -> aligned */
    normal1 = mem_allocator_malloc(allocator, 64);
    assert_non_null(normal1);
    assert_false(is_aligned_allocation(normal1));

    aligned1 = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(aligned1);
    assert_true(is_aligned_allocation(aligned1));
    assert_true(is_aligned(aligned1, 64));

    normal2 = mem_allocator_malloc(allocator, 96);
    assert_non_null(normal2);
    assert_false(is_aligned_allocation(normal2));

    aligned2 = mem_allocator_malloc_aligned(allocator, 256, 128);
    assert_non_null(aligned2);
    assert_true(is_aligned_allocation(aligned2));
    assert_true(is_aligned(aligned2, 128));

    /* Free in mixed order */
    mem_allocator_free(allocator, normal1);
    mem_allocator_free(allocator, aligned2);
    mem_allocator_free(allocator, normal2);
    mem_allocator_free(allocator, aligned1);

    mem_allocator_destroy(allocator);
}

/* Test: obj_to_hmu works correctly for both types */
static void
test_mixed_obj_to_hmu(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *normal, *aligned;
    hmu_t *hmu_normal, *hmu_aligned;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate both types */
    normal = mem_allocator_malloc(allocator, 128);
    assert_non_null(normal);

    aligned = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(aligned);

    /* Get HMU pointers */
    hmu_normal = obj_to_hmu(normal);
    hmu_aligned = obj_to_hmu(aligned);

    assert_non_null(hmu_normal);
    assert_non_null(hmu_aligned);

    /* Both should have HMU_VO type */
    assert_int_equal(hmu_get_ut(hmu_normal), HMU_VO);
    assert_int_equal(hmu_get_ut(hmu_aligned), HMU_VO);

    /* Sizes should be reasonable */
    assert_true(hmu_get_size(hmu_normal) >= 128);
    assert_true(hmu_get_size(hmu_aligned) >= 128);

    /* Free both */
    mem_allocator_free(allocator, normal);
    mem_allocator_free(allocator, aligned);

    mem_allocator_destroy(allocator);
}

/* Test: Many aligned allocations */
static void
test_aligned_alloc_many(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[512 * 1024];
    void *ptrs[100];
    int count = 0;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate as many as possible */
    for (int i = 0; i < 100; i++) {
        int align = (i % 4 == 0) ? 64 : 32;
        ptrs[i] = mem_allocator_malloc_aligned(allocator, align * 2, align);
        if (ptrs[i]) {
            assert_true(is_aligned(ptrs[i], align));
            count++;
        }
        else {
            break;
        }
    }

    assert_true(count > 10); /* At least some should succeed */

    /* Free all */
    for (int i = 0; i < count; i++) {
        mem_allocator_free(allocator, ptrs[i]);
    }

    mem_allocator_destroy(allocator);
}

/* Test: Many mixed allocations */
static void
test_mixed_alloc_many(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[512 * 1024];
    void *ptrs[200];
    int count = 0;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Alternate normal and aligned */
    for (int i = 0; i < 200; i++) {
        if (i % 2 == 0) {
            /* Normal allocation */
            ptrs[i] = mem_allocator_malloc(allocator, 64);
        }
        else {
            /* Aligned allocation */
            ptrs[i] = mem_allocator_malloc_aligned(allocator, 64, 32);
        }

        if (ptrs[i]) {
            count++;
        }
        else {
            break;
        }
    }

    assert_true(count > 20);

    /* Free in reverse order */
    for (int i = count - 1; i >= 0; i--) {
        mem_allocator_free(allocator, ptrs[i]);
    }

    mem_allocator_destroy(allocator);
}

/* Test: free a .ro data */
static void
test_free_ro_data(void **state)
{

    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Freeing a .ro data pointer should not crash */
    const char *ro_str = "This is a read-only string.";
    // FIXME: This case should trigger an exception because the pointer is not
    // allocated by the allocator, but currently it just does nothing. We should
    // add a check in mem_allocator_free to detect this case and return an
    // error. mem_allocator_free(allocator, (void *)ro_str);
    mem_allocator_destroy(allocator);
}

/* Test: free a freed pointer */
static void
test_free_freed_pointer(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    ptr = mem_allocator_malloc(allocator, 64);
    assert_non_null(ptr);

    mem_allocator_free(allocator, ptr);
    /* Freeing the same pointer again should not crash */
    mem_allocator_free(allocator, ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: free a freed pointer from aligned-alloc */
static void
test_free_freed_pointer_aligned(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    ptr = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(ptr);

    mem_allocator_free(allocator, ptr);
    /* Freeing the same pointer again should not crash */
    mem_allocator_free(allocator, ptr);
    mem_allocator_free(allocator, ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: wasm_runtime_aligned_alloc with valid inputs in POOL mode */
static void
test_wasm_runtime_aligned_alloc_valid(void **state)
{
    RuntimeInitArgs init_args;
    void *ptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = malloc(256 * 1024);
    init_args.mem_alloc_option.pool.heap_size = 256 * 1024;

    assert_true(wasm_runtime_full_init(&init_args));

    /* Test valid aligned allocation */
    ptr = wasm_runtime_aligned_alloc(128, 64);
    assert_non_null(ptr);
    assert_true(is_aligned(ptr, 64));

    /* Free should work */
    wasm_runtime_free(ptr);

    wasm_runtime_destroy();
    free(init_args.mem_alloc_option.pool.heap_buf);
}

/* Test: wasm_runtime_aligned_alloc with zero size */
static void
test_wasm_runtime_aligned_alloc_zero_size(void **state)
{
    RuntimeInitArgs init_args;
    void *ptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = malloc(256 * 1024);
    init_args.mem_alloc_option.pool.heap_size = 256 * 1024;

    assert_true(wasm_runtime_full_init(&init_args));

    /* Zero size should allocate alignment bytes (like malloc(0) behavior) */
    ptr = wasm_runtime_aligned_alloc(0, 64);
    assert_non_null(ptr);
    assert_true(is_aligned(ptr, 64));

    wasm_runtime_free(ptr);
    wasm_runtime_destroy();
    free(init_args.mem_alloc_option.pool.heap_buf);
}

/* Test: wasm_runtime_aligned_alloc with zero alignment */
static void
test_wasm_runtime_aligned_alloc_zero_alignment(void **state)
{
    RuntimeInitArgs init_args;
    void *ptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = malloc(256 * 1024);
    init_args.mem_alloc_option.pool.heap_size = 256 * 1024;

    assert_true(wasm_runtime_full_init(&init_args));

    /* Zero alignment should return NULL */
    ptr = wasm_runtime_aligned_alloc(128, 0);
    assert_null(ptr);

    wasm_runtime_destroy();
    free(init_args.mem_alloc_option.pool.heap_buf);
}

/* Test: wasm_runtime_aligned_alloc in SYSTEM_ALLOCATOR mode returns NULL */
static void
test_wasm_runtime_aligned_alloc_system_mode(void **state)
{
    RuntimeInitArgs init_args;
    void *ptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_System_Allocator;

    assert_true(wasm_runtime_full_init(&init_args));

    /* Should return NULL in non-POOL mode */
    ptr = wasm_runtime_aligned_alloc(128, 64);
    assert_null(ptr);

    wasm_runtime_destroy();
}

/* Test: wasm_runtime_realloc rejects aligned allocations */
static void
test_wasm_runtime_realloc_rejects_aligned(void **state)
{
    RuntimeInitArgs init_args;
    void *ptr, *new_ptr;

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = malloc(256 * 1024);
    init_args.mem_alloc_option.pool.heap_size = 256 * 1024;

    assert_true(wasm_runtime_full_init(&init_args));

    /* Allocate with alignment */
    ptr = wasm_runtime_aligned_alloc(128, 64);
    assert_non_null(ptr);

    /* Realloc should return NULL */
    new_ptr = wasm_runtime_realloc(ptr, 256);
    assert_null(new_ptr);

    /* Original pointer still valid */
    wasm_runtime_free(ptr);

    wasm_runtime_destroy();
    free(init_args.mem_alloc_option.pool.heap_buf);
}

/* Test: wasm_runtime_aligned_alloc with various alignments */
static void
test_wasm_runtime_aligned_alloc_multiple_alignments(void **state)
{
    RuntimeInitArgs init_args;
    int alignments[] = { 8, 16, 32, 64, 128, 256 };
    int num_alignments = sizeof(alignments) / sizeof(alignments[0]);

    memset(&init_args, 0, sizeof(RuntimeInitArgs));
    init_args.mem_alloc_type = Alloc_With_Pool;
    init_args.mem_alloc_option.pool.heap_buf = malloc(512 * 1024);
    init_args.mem_alloc_option.pool.heap_size = 512 * 1024;

    assert_true(wasm_runtime_full_init(&init_args));

    for (int i = 0; i < num_alignments; i++) {
        int align = alignments[i];
        void *ptr = wasm_runtime_aligned_alloc(align * 2, align);
        assert_non_null(ptr);
        assert_true(is_aligned(ptr, align));
        wasm_runtime_free(ptr);
    }

    wasm_runtime_destroy();
    free(init_args.mem_alloc_option.pool.heap_buf);
}

/* Test: Normal allocation with huge size (near upper limit) */
static void
test_normal_alloc_huge_size(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[1024 * 1024]; /* 1MB heap */
    void *ptr;
    size_t huge_size;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Try to allocate most of the heap */
    huge_size = sizeof(heap_buf) - 4096; /* Leave some overhead */
    ptr = mem_allocator_malloc(allocator, huge_size);

    /* May succeed or fail depending on internal fragmentation */
    if (ptr) {
        /* If it succeeds, verify it's properly allocated */
        assert_true(is_aligned(ptr, 8));
        mem_allocator_free(allocator, ptr);
    }

    /* Try allocation at exact upper limit - should handle gracefully */
    huge_size = SIZE_MAX - 1024;
    ptr = mem_allocator_malloc(allocator, huge_size);
    assert_null(ptr); /* Should fail gracefully, not crash */

    mem_allocator_destroy(allocator);
}

/* Test: Aligned allocation with huge size (near upper limit) */
static void
test_aligned_alloc_huge_size(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[1024 * 1024]; /* 1MB heap */
    void *ptr;
    size_t huge_size;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Try to allocate most of the heap with alignment */
    huge_size = 512 * 1024; /* Size must be multiple of alignment */
    ptr = mem_allocator_malloc_aligned(allocator, huge_size, 512);

    /* May succeed or fail depending on alignment overhead */
    if (ptr) {
        assert_true(is_aligned(ptr, 512));
        mem_allocator_free(allocator, ptr);
    }

    /* Try allocation at extreme size - should fail gracefully */
    huge_size = (SIZE_MAX / 2) & ~(size_t)4095; /* Aligned to 4096 */
    ptr = mem_allocator_malloc_aligned(allocator, huge_size, 4096);
    assert_null(ptr); /* Should fail gracefully, not crash */

    mem_allocator_destroy(allocator);
}

/* Test: Normal allocations until OOM */
static void
test_normal_alloc_until_oom(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[256 * 1024];
    void *ptrs[1000];
    int count = 0;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate until we run out of memory */
    for (int i = 0; i < 1000; i++) {
        ptrs[i] = mem_allocator_malloc(allocator, 1024);
        if (ptrs[i]) {
            count++;
        }
        else {
            /* OOM reached - this is expected */
            break;
        }
    }

    /* Should have allocated at least some blocks */
    assert_true(count > 10);
    assert_true(count < 1000); /* Should not have allocated all */

    /* Should still be able to free what we allocated */
    for (int i = 0; i < count; i++) {
        mem_allocator_free(allocator, ptrs[i]);
    }

    /* After freeing, should be able to allocate again */
    void *ptr = mem_allocator_malloc(allocator, 1024);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Aligned allocations until OOM */
static void
test_aligned_alloc_until_oom(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[512 * 1024];
    void *ptrs[500];
    int count = 0;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Allocate with alignment until we run out of memory */
    for (int i = 0; i < 500; i++) {
        /* Alternate between different alignments */
        int align = (i % 2 == 0) ? 64 : 128;
        ptrs[i] = mem_allocator_malloc_aligned(allocator, align * 20, align);
        if (ptrs[i]) {
            assert_true(is_aligned(ptrs[i], align));
            count++;
        }
        else {
            /* OOM reached - this is expected */
            break;
        }
    }

    /* Should have allocated at least some blocks */
    assert_true(count > 5);
    assert_true(count < 500); /* Should not have allocated all */

    /* Free all allocated blocks */
    for (int i = 0; i < count; i++) {
        mem_allocator_free(allocator, ptrs[i]);
    }

    /* After freeing, should be able to allocate again */
    void *ptr = mem_allocator_malloc_aligned(allocator, 256, 64);
    assert_non_null(ptr);
    mem_allocator_free(allocator, ptr);

    mem_allocator_destroy(allocator);
}

/* Test: Mixed normal and aligned allocations until OOM */
static void
test_mixed_alloc_until_oom(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[128 * 1024];
    void *ptrs[1000];
    bool is_aligned_alloc[1000];
    int count = 0;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Alternate between normal and aligned allocations until OOM */
    for (int i = 0; i < 1000; i++) {
        if (i % 3 == 0) {
            /* Aligned allocation */
            ptrs[i] = mem_allocator_malloc_aligned(allocator, 128, 64);
            is_aligned_alloc[i] = true;
        }
        else {
            /* Normal allocation */
            ptrs[i] = mem_allocator_malloc(allocator, 512);
            is_aligned_alloc[i] = false;
        }

        if (ptrs[i]) {
            if (is_aligned_alloc[i]) {
                assert_true(is_aligned(ptrs[i], 64));
            }
            count++;
        }
        else {
            /* OOM reached */
            break;
        }
    }

    /* Should have allocated a reasonable number of blocks */
    assert_true(count > 20);
    assert_true(count < 1000); /* Should not have allocated all */

    /* Free in random order (every other block first) */
    for (int i = 0; i < count; i += 2) {
        mem_allocator_free(allocator, ptrs[i]);
    }
    for (int i = 1; i < count; i += 2) {
        mem_allocator_free(allocator, ptrs[i]);
    }

    /* Verify allocator still works after OOM and free */
    void *ptr1 = mem_allocator_malloc(allocator, 1024);
    void *ptr2 = mem_allocator_malloc_aligned(allocator, 128, 64);
    assert_non_null(ptr1);
    assert_non_null(ptr2);
    mem_allocator_free(allocator, ptr1);
    mem_allocator_free(allocator, ptr2);

    mem_allocator_destroy(allocator);
}

/* Test: Memory usage statistics for in-place reallocations */
static void
test_memory_usage_statistics_in_place_realloc(void **state)
{
    mem_allocator_t allocator;
    char heap_buf[64 * 1024];
    void *ptr;
    void *realloc_ptr;
    mem_alloc_info_t info;

    allocator = mem_allocator_create(heap_buf, sizeof(heap_buf));
    assert_non_null(allocator);

    /* Initial memory usage statistics */
    mem_allocator_get_alloc_info(allocator, &info);
    assert_int_equal(info.total_size - info.total_free_size, 0);
    assert_int_equal(info.highmark_size, 0);

    /* Allocation */
    ptr = mem_allocator_malloc(allocator, 128);
    assert_non_null(ptr);

    /* Memory usage statistics after allocation */
    mem_allocator_get_alloc_info(allocator, &info);
    assert_int_equal(info.total_size - info.total_free_size,
                     GC_ALIGN_8(128 + OBJ_EXTRA_SIZE));
    assert_int_equal(info.highmark_size, GC_ALIGN_8(128 + OBJ_EXTRA_SIZE));

    /* Reallocation */
    realloc_ptr = mem_allocator_realloc(allocator, ptr, 256);
    assert_non_null(realloc_ptr);
    assert_ptr_equal(realloc_ptr, ptr); /* Should be in-place realloc */
    ptr = realloc_ptr;

    /* Memory usage statistics after reallocation */
    mem_allocator_get_alloc_info(allocator, &info);
    assert_int_equal(info.total_size - info.total_free_size,
                     GC_ALIGN_8(256 + OBJ_EXTRA_SIZE));
    assert_int_equal(info.highmark_size, GC_ALIGN_8(256 + OBJ_EXTRA_SIZE));

    /* Free */
    mem_allocator_free(allocator, ptr);

    /* Memory usage statistics after free */
    mem_allocator_get_alloc_info(allocator, &info);
    assert_int_equal(info.total_size - info.total_free_size, 0);
    assert_int_equal(info.highmark_size, GC_ALIGN_8(256 + OBJ_EXTRA_SIZE));

    mem_allocator_destroy(allocator);
}

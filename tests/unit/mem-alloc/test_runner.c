/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdint.h>
#include <cmocka.h>

/* Include test implementations */
#include "mem_alloc_test.c"

int
main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_normal_alloc_basic),
        cmocka_unit_test(test_aligned_alloc_valid_alignments),
        cmocka_unit_test(test_realloc_rejects_aligned),
        cmocka_unit_test(test_normal_realloc_works),
        cmocka_unit_test(test_aligned_alloc_invalid_not_power_of_2),
        cmocka_unit_test(test_aligned_alloc_size_not_multiple),
        cmocka_unit_test(test_mixed_alloc_interleaved),
        cmocka_unit_test(test_mixed_obj_to_hmu),
        cmocka_unit_test(test_aligned_alloc_many),
        cmocka_unit_test(test_mixed_alloc_many),
        cmocka_unit_test(test_free_freed_pointer),
        cmocka_unit_test(test_free_freed_pointer_aligned),
        cmocka_unit_test(test_free_ro_data),
        cmocka_unit_test(test_wasm_runtime_aligned_alloc_valid),
        cmocka_unit_test(test_wasm_runtime_aligned_alloc_zero_size),
        cmocka_unit_test(test_wasm_runtime_aligned_alloc_zero_alignment),
        cmocka_unit_test(test_wasm_runtime_aligned_alloc_system_mode),
        cmocka_unit_test(test_wasm_runtime_realloc_rejects_aligned),
        cmocka_unit_test(test_wasm_runtime_aligned_alloc_multiple_alignments),
        cmocka_unit_test(test_normal_alloc_huge_size),
        cmocka_unit_test(test_aligned_alloc_huge_size),
        cmocka_unit_test(test_normal_alloc_until_oom),
        cmocka_unit_test(test_aligned_alloc_until_oom),
        cmocka_unit_test(test_mixed_alloc_until_oom),
        cmocka_unit_test(test_memory_usage_statistics_in_place_realloc),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}

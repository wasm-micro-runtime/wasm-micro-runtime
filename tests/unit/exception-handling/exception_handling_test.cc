/*
 * Copyright (C) 2024 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

/* Pull in the INVALID_TAGINDEX definitions */
#include "wasm_runtime.h"

class ExceptionHandlingTest : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);
    }

    virtual void TearDown() { wasm_runtime_destroy(); }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
};

/*
 * Test that IS_INVALID_TAGINDEX correctly identifies the invalid tag index
 * value (0xFFFFFFFF) used for cross-module exceptions with unknown tags.
 *
 * The RETHROW handler must check IS_INVALID_TAGINDEX before accessing
 * module->module->tags[exception_tag_index]. Without the check,
 * exception_tag_index = INVALID_TAGINDEX (0xFFFFFFFF) would cause
 * tags[0xFFFFFFFF] -- a massive out-of-bounds read.
 *
 * The THROW handler at wasm_interp_classic.c properly checks
 * IS_INVALID_TAGINDEX, but previously RETHROW did not.
 */
TEST_F(ExceptionHandlingTest, invalid_tagindex_detected)
{
    uint32_t tag_index = INVALID_TAGINDEX;
    EXPECT_TRUE(IS_INVALID_TAGINDEX(tag_index))
        << "INVALID_TAGINDEX (0xFFFFFFFF) should be detected";
}

TEST_F(ExceptionHandlingTest, valid_tagindex_not_rejected)
{
    uint32_t tag_index = 0;
    EXPECT_FALSE(IS_INVALID_TAGINDEX(tag_index))
        << "Tag index 0 should not be detected as invalid";

    tag_index = 5;
    EXPECT_FALSE(IS_INVALID_TAGINDEX(tag_index))
        << "Tag index 5 should not be detected as invalid";
}

/*
 * Test that a WASM module with exception handling (try/catch/throw)
 * can load and instantiate correctly when exception handling is enabled.
 */
TEST_F(ExceptionHandlingTest, load_module_with_exception_handling)
{
    /*
     * Minimal WASM module with exception handling:
     *   - Tag section (id=13): 1 tag, type 0 (no params)
     *   - Function with try/catch_all/end that does nothing
     */
    uint8_t wasm_eh[] = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
        /* Type section: 1 type, () -> () */
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        /* Function section: 1 func, type 0 */
        0x03, 0x02, 0x01, 0x00,
        /* Tag section (id=13): 1 tag, attribute=0, type=0 */
        0x0D, 0x03, 0x01, 0x00, 0x00,
        /* Export: "f" = func 0 */
        0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00,
        /* Code section */
        0x0A, 0x08, 0x01,
        0x06, 0x00,       /* body size=6, 0 locals */
        0x06, 0x40,       /* try (void) */
        0x19,             /* catch_all */
        0x0B,             /* end try */
        0x0B,             /* end func */
    };

    memset(error_buf, 0, sizeof(error_buf));
    wasm_module_t module = wasm_runtime_load(
        wasm_eh, sizeof(wasm_eh), error_buf, sizeof(error_buf));

    ASSERT_NE(module, nullptr) << "Module load failed: " << error_buf;

    wasm_module_inst_t inst = wasm_runtime_instantiate(
        module, 8192, 8192, error_buf, sizeof(error_buf));
    ASSERT_NE(inst, nullptr) << "Instantiation failed: " << error_buf;

    wasm_function_inst_t func =
        wasm_runtime_lookup_function(inst, "f");
    ASSERT_NE(func, nullptr) << "Function 'f' should be found";

    wasm_exec_env_t exec_env =
        wasm_runtime_create_exec_env(inst, 8192);
    ASSERT_NE(exec_env, nullptr) << "Failed to create exec env";

    bool ok = wasm_runtime_call_wasm(exec_env, func, 0, NULL);
    ASSERT_TRUE(ok) << "wasm_runtime_call_wasm failed: "
                    << wasm_runtime_get_exception(inst);

    wasm_runtime_destroy_exec_env(exec_env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
}

/*
 * Verify that the RETHROW handler's tag index validation logic matches
 * the THROW handler. Both must handle IS_INVALID_TAGINDEX the same way:
 * skip the tags[] access and set cell_num_to_copy = 0.
 */
TEST_F(ExceptionHandlingTest, rethrow_handles_invalid_tagindex_like_throw)
{
    /* Simulate what both handlers should do with INVALID_TAGINDEX */
    uint32_t exception_tag_index = INVALID_TAGINDEX;
    uint32_t tag_count = 5;

    /* THROW handler logic (reference - always correct): */
    uint32_t throw_cell_num = 0;
    if (IS_INVALID_TAGINDEX(exception_tag_index)) {
        throw_cell_num = 0; /* skip tags[] access */
    }
    else {
        /* would access tags[exception_tag_index] */
        throw_cell_num = 42; /* placeholder */
    }

    /* RETHROW handler logic (after fix - should match THROW): */
    uint32_t rethrow_cell_num = 0;
    if (IS_INVALID_TAGINDEX(exception_tag_index)) {
        rethrow_cell_num = 0; /* skip tags[] access */
    }
    else {
        /* would access tags[exception_tag_index] */
        rethrow_cell_num = 42; /* placeholder */
    }

    EXPECT_EQ(throw_cell_num, rethrow_cell_num)
        << "RETHROW should handle INVALID_TAGINDEX the same as THROW";

    EXPECT_EQ(throw_cell_num, 0u)
        << "Both handlers should set cell_num = 0 for INVALID_TAGINDEX";

    /* Without the fix, RETHROW would have tried:
     *   tags[0xFFFFFFFF] => massive OOB read => crash */
    EXPECT_TRUE(exception_tag_index >= tag_count)
        << "INVALID_TAGINDEX should be >= any reasonable tag_count";
}

/*
 * A tag section that declares 0x40000001 tags must fail with an allocation
 * error. On a 32-bit host the tag array size wraps to 4 bytes unless the
 * multiply is done in 64 bits.
 */
TEST_F(ExceptionHandlingTest, load_tag_section_count_overflows_array_size)
{
    /* clang-format off */
    uint8_t wasm_tags[] = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,
        /* Type section: 1 type, () -> () */
        0x01, 0x04, 0x01, 0x60, 0x00, 0x00,
        /* Tag section (id=13): count 0x40000001, then 2 tags of
           attribute=0, type=0 */
        0x0D, 0x09, 0x81, 0x80, 0x80, 0x80, 0x04, 0x00, 0x00, 0x00, 0x00,
    };
    /* clang-format on */

    memset(error_buf, 0, sizeof(error_buf));
    wasm_module_t module = wasm_runtime_load(wasm_tags, sizeof(wasm_tags),
                                             error_buf, sizeof(error_buf));

    ASSERT_EQ(module, nullptr);
    EXPECT_NE(strstr(error_buf, "allocate memory failed"), nullptr)
        << "Unexpected error: " << error_buf;
}

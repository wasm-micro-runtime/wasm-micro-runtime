/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* wasm_frame_t: the accessors, wasm_frame_copy()/wasm_frame_delete(), and the
   clone of a vector of frames.  A trap only carries frames in a runtime built
   with WASM_ENABLE_DUMP_CALL_STACK, which this suite is not, so the frames
   here are hand-built. */

#include "c_api_test_fixture.h"

#include "bh_vector.h"

extern "C" {
void
wasm_frame_vec_clone_internal(Vector *src, Vector *out);
}

namespace {

class WasmCApiFrameTest : public CApiTestBase
{};

/* A frame with recognisable fields; instance is only ever compared, never
   dereferenced. */
wasm_frame_t
make_frame(void *instance, uint32_t module_offset, uint32_t func_index,
           uint32_t func_offset)
{
    wasm_frame_t frame = {};
    frame.instance = instance;
    frame.module_offset = module_offset;
    frame.func_index = func_index;
    frame.func_offset = func_offset;
    return frame;
}

TEST_F(WasmCApiFrameTest, accessors_read_the_fields)
{
    int marker = 0;
    wasm_frame_t frame = make_frame(&marker, 0x40, 3, 0x10);

    EXPECT_EQ((wasm_instance_t *)&marker, wasm_frame_instance(&frame));
    EXPECT_EQ(0x40u, wasm_frame_module_offset(&frame));
    EXPECT_EQ(3u, wasm_frame_func_index(&frame));
    EXPECT_EQ(0x10u, wasm_frame_func_offset(&frame));
}

TEST_F(WasmCApiFrameTest, accessors_of_null_frame_are_zero)
{
    EXPECT_EQ(nullptr, wasm_frame_instance(nullptr));
    EXPECT_EQ(0u, wasm_frame_module_offset(nullptr));
    EXPECT_EQ(0u, wasm_frame_func_index(nullptr));
    EXPECT_EQ(0u, wasm_frame_func_offset(nullptr));
}

TEST_F(WasmCApiFrameTest, copy_duplicates_the_fields)
{
    int marker = 0;
    wasm_frame_t frame = make_frame(&marker, 0x40, 3, 0x10);

    wasm_frame_t *copy = wasm_frame_copy(&frame);
    ASSERT_NE(nullptr, copy);
    EXPECT_NE(&frame, copy);
    EXPECT_EQ((wasm_instance_t *)&marker, wasm_frame_instance(copy));
    EXPECT_EQ(0x40u, wasm_frame_module_offset(copy));
    EXPECT_EQ(3u, wasm_frame_func_index(copy));
    EXPECT_EQ(0x10u, wasm_frame_func_offset(copy));

    wasm_frame_delete(copy);
}

TEST_F(WasmCApiFrameTest, copy_and_delete_of_null_frame_are_harmless)
{
    EXPECT_EQ(nullptr, wasm_frame_copy(nullptr));
    wasm_frame_delete(nullptr);
}

TEST_F(WasmCApiFrameTest, clone_of_a_frame_vector_copies_every_frame)
{
    Vector src, out;
    ASSERT_TRUE(bh_vector_init(&src, 2, sizeof(WASMCApiFrame), false));
    ASSERT_TRUE(bh_vector_init(&out, 1, sizeof(WASMCApiFrame), false));

    WASMCApiFrame frames[2] = {};
    frames[0].func_index = 1;
    frames[0].func_offset = 0x11;
    frames[1].func_index = 2;
    frames[1].func_offset = 0x22;
    memcpy(src.data, frames, sizeof(frames));
    src.num_elems = 2;

    wasm_frame_vec_clone_internal(&src, &out);

    ASSERT_EQ(2u, out.num_elems);
    EXPECT_NE(src.data, out.data);
    EXPECT_EQ(0, memcmp(src.data, out.data, sizeof(frames)));

    bh_vector_destroy(&src);
    bh_vector_destroy(&out);
}

TEST_F(WasmCApiFrameTest, clone_of_an_empty_vector_empties_the_destination)
{
    Vector src, out;
    ASSERT_TRUE(bh_vector_init(&src, 1, sizeof(WASMCApiFrame), false));
    ASSERT_TRUE(bh_vector_init(&out, 1, sizeof(WASMCApiFrame), false));
    src.num_elems = 0;
    out.num_elems = 1;

    wasm_frame_vec_clone_internal(&src, &out);

    EXPECT_EQ(nullptr, out.data);
    bh_vector_destroy(&src);
}

} // namespace

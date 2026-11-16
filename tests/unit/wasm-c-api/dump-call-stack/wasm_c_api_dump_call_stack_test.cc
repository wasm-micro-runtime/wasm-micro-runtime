/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The trap frames, which only exist with WASM_ENABLE_DUMP_CALL_STACK (the
   wasm-c-api/base suite covers the "no frames" side of the same cases):

   wasm_frame_new / _copy / _delete, wasm_frame_instance, wasm_frame_func_index,
   wasm_frame_func_offset, wasm_frame_module_offset,
   wasm_frame_vec_clone_internal and wasm_frame_vec_copy.

   The fixture is base/wasm-apps/simple.wat, whose exported `trap` function is
   an `unreachable`, so one call produces a trap with a real frame. */

#include "c_api_test_fixture.h"

namespace {

class WasmCApiDumpCallStackTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        fixture.init(store, "simple");

        imports = new HostImport(store, 1, 1, host_add);
        ASSERT_TRUE(imports->valid());

        instance = new FixtureInstance();
        instance->init(store, fixture.get(), &imports->as_vec());
        ASSERT_NE(nullptr, instance->get());
    }

    void TearDown() override
    {
        delete instance;
        delete imports;
        fixture.reset();
        CApiTestBase::TearDown();
    }

    FixtureModule fixture;
    HostImport *imports = nullptr;
    FixtureInstance *instance = nullptr;

    /* Call `trap` and hand back the trap; the caller releases it. */
    wasm_trap_t *call_trap()
    {
        wasm_func_t *trap_func =
            wasm_extern_as_func(instance->export_by_name("trap"));

        if (!trap_func)
            return nullptr;

        wasm_val_vec_t no_args = { 0 };
        wasm_val_vec_t no_results = { 0 };

        return wasm_func_call(trap_func, &no_args, &no_results);
    }
};

TEST_F(WasmCApiDumpCallStackTest, trap_carries_its_origin_frame)
{
    wasm_trap_t *trap = call_trap();

    ASSERT_NE(nullptr, trap);

    wasm_frame_t *origin = wasm_trap_origin(trap);
    ASSERT_NE(nullptr, origin);

    /* The frame belongs to the instance that trapped. */
    EXPECT_EQ(instance->get(), wasm_frame_instance(origin));

    /* The exported `trap` function is the last function of simple.wat. */
    EXPECT_GT(wasm_frame_func_index(origin), 0u);
    EXPECT_GE(wasm_frame_func_offset(origin), 0u);
    EXPECT_GE(wasm_frame_module_offset(origin), 0u);

    /* wasm_frame_copy() copies the four fields and nothing else. */
    wasm_frame_t *copy = wasm_frame_copy(origin);
    ASSERT_NE(nullptr, copy);
    EXPECT_EQ(wasm_frame_instance(origin), wasm_frame_instance(copy));
    EXPECT_EQ(wasm_frame_func_index(origin), wasm_frame_func_index(copy));
    EXPECT_EQ(wasm_frame_func_offset(origin), wasm_frame_func_offset(copy));
    EXPECT_EQ(wasm_frame_module_offset(origin), wasm_frame_module_offset(copy));
    wasm_frame_delete(copy);

    wasm_frame_delete(origin);
    wasm_trap_delete(trap);

    EXPECT_EQ(nullptr, wasm_frame_copy(nullptr));
    wasm_frame_delete(nullptr);
}

TEST_F(WasmCApiDumpCallStackTest, trap_trace_holds_every_frame)
{
    wasm_trap_t *trap = call_trap();

    ASSERT_NE(nullptr, trap);

    wasm_frame_vec_t trace = { 0 };
    wasm_trap_trace(trap, &trace);

    ASSERT_GT(trace.num_elems, 0u);

    for (size_t i = 0; i < trace.num_elems; i++) {
        ASSERT_NE(nullptr, trace.data[i]);
        EXPECT_GE(wasm_frame_func_index(trace.data[i]), 0u);
        EXPECT_EQ(instance->get(), wasm_frame_instance(trace.data[i]));
    }

    /* frame_vec_copy duplicates the vector through wasm_frame_copy(), so the
       two vectors own separate frames. */
    wasm_frame_vec_t copy = { 0 };
    wasm_frame_vec_copy(&copy, &trace);

    ASSERT_EQ(trace.num_elems, copy.num_elems);
    for (size_t i = 0; i < copy.num_elems; i++) {
        ASSERT_NE(nullptr, copy.data[i]);
        EXPECT_NE(trace.data[i], copy.data[i]);
        EXPECT_EQ(wasm_frame_func_index(trace.data[i]),
                  wasm_frame_func_index(copy.data[i]));
    }

    wasm_frame_vec_delete(&copy);
    wasm_frame_vec_delete(&trace);
    wasm_trap_delete(trap);
}

TEST_F(WasmCApiDumpCallStackTest, frames_outlive_the_trap_they_came_from)
{
    /* wasm_frame_copy() is what makes a frame independent: the trace of a
       released trap must not be touched, so copy first and use the copy. */
    wasm_trap_t *trap = call_trap();
    ASSERT_NE(nullptr, trap);

    wasm_frame_vec_t trace = { 0 };
    wasm_trap_trace(trap, &trace);
    ASSERT_GT(trace.num_elems, 0u);

    wasm_frame_t *saved = wasm_frame_copy(trace.data[0]);
    ASSERT_NE(nullptr, saved);

    uint32_t saved_index = wasm_frame_func_index(saved);

    wasm_frame_vec_delete(&trace);
    wasm_trap_delete(trap);

    EXPECT_EQ(saved_index, wasm_frame_func_index(saved));
    wasm_frame_delete(saved);
}

} // namespace

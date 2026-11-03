/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#if WASM_ENABLE_AOT != 0
static const char *fixture_name = "uninit_elem.aot";
#else
static const char *fixture_name = "uninit_elem.wasm";
#endif

static std::vector<uint8_t>
read_fixture(const std::string &name)
{
    std::ifstream file(get_test_binary_dir() + "/" + name, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}

class UninitElemTest : public testing::Test
{
  protected:
    WAMRRuntimeRAII<> runtime_;
    std::vector<uint8_t> buffer_;
    std::unique_ptr<WAMRModule> module_;
    std::unique_ptr<WAMRInstance> instance_;
    std::unique_ptr<WAMRExecEnv> exec_env_;

    void SetUp() override
    {
        buffer_ = read_fixture(fixture_name);
        ASSERT_FALSE(buffer_.empty()) << "Failed to read " << fixture_name;

        module_.reset(new WAMRModule(buffer_.data(), buffer_.size()));
        ASSERT_NE(nullptr, module_->get()) << "Failed to load " << fixture_name;

        instance_.reset(new WAMRInstance(*module_));
        ASSERT_NE(nullptr, instance_->get()) << "Failed to instantiate";

        exec_env_.reset(new WAMRExecEnv(*instance_));
        ASSERT_NE(nullptr, exec_env_->get()) << "Failed to create exec env";
    }

    /* Call an exported (i32) -> i32 function. Returns the exception text
       left by the runtime, or an empty string on success. */
    std::string call_i32(const char *name, uint32_t arg, uint32_t *result)
    {
        wasm_function_inst_t func =
            wasm_runtime_lookup_function(instance_->get(), name);
        EXPECT_NE(nullptr, func) << "Failed to lookup " << name;
        if (!func) {
            return "lookup failed";
        }

        uint32_t argv[1] = { arg };
        if (!wasm_runtime_call_wasm(exec_env_->get(), func, 1, argv)) {
            const char *exception =
                wasm_runtime_get_exception(instance_->get());
            return exception ? exception : "";
        }
        if (result) {
            *result = argv[0];
        }
        return "";
    }
};

/* T1: call_indirect on a slot that was never written */
TEST_F(UninitElemTest, CallIndirect_NeverWrittenSlot_TrapsUninitialized)
{
    EXPECT_EQ("Exception: uninitialized element",
              call_i32("call_indirect_i32", 3, nullptr));
}

/* T7: partially initialized table, only slot 0 is written by the elem seg */
TEST_F(UninitElemTest, CallIndirect_PartiallyInitialized_OnlyInitializedSlotRuns)
{
    uint32_t result = 0;
    EXPECT_EQ("", call_i32("call_indirect_i32", 0, &result));
    EXPECT_EQ(42u, result);

    EXPECT_EQ("Exception: uninitialized element",
              call_i32("call_indirect_i32", 1, nullptr));
}

/* T8: index beyond cur_size is reported as undefined, not uninitialized */
TEST_F(UninitElemTest, CallIndirect_OutOfBounds_TrapsUndefined)
{
    EXPECT_EQ("Exception: undefined element",
              call_i32("call_indirect_i32", 10, nullptr));
}

/* T4: table.get on an uninitialized slot yields a null reference */
TEST_F(UninitElemTest, TableGet_UninitializedSlot_IsNull)
{
    uint32_t is_null = 0;
    EXPECT_EQ("", call_i32("is_null_at", 2, &is_null));
    EXPECT_EQ(1u, is_null);
}

/* T5: an explicit ref.null written with table.set must trap the same way */
TEST_F(UninitElemTest, CallIndirect_TableSetNull_TrapsUninitialized)
{
    EXPECT_EQ("Exception: uninitialized element",
              call_i32("table_set_null_then_call", 2, nullptr));
}

/* T6: slots added by table.grow with a null init value are uninitialized */
TEST_F(UninitElemTest, CallIndirect_TableGrowNull_TrapsUninitialized)
{
    EXPECT_EQ("Exception: uninitialized element",
              call_i32("table_grow_null_then_call", 5, nullptr));
}

/* T2: the host API path, which calls through wasm_call_indirect() or
   aot_call_indirect(). On 64-bit this used to misreport the slot. */
TEST_F(UninitElemTest, HostCallIndirect_UninitializedSlot_ReturnsFalse)
{
    uint32_t argv[1] = { 0 };
    EXPECT_FALSE(wasm_runtime_call_indirect(exec_env_->get(), 3, 0, argv));
    EXPECT_STREQ("Exception: uninitialized element",
                 wasm_runtime_get_exception(instance_->get()));
}

/* T3: the C API path, which calls wasm_table_get_func_inst(). */
TEST_F(UninitElemTest, TableGetFuncInst_UninitializedSlot_ReturnsNull)
{
    wasm_table_inst_t table = { 0 };
    ASSERT_TRUE(wasm_runtime_get_export_table_inst(instance_->get(), "tbl",
                                                   &table));

    EXPECT_NE(nullptr, wasm_table_get_func_inst(instance_->get(), &table, 0));
    EXPECT_EQ(nullptr, wasm_table_get_func_inst(instance_->get(), &table, 3));
}

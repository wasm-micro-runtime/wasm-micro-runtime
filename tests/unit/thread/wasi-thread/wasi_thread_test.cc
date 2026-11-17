/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <chrono>
#include <thread>

#include "thread_test_common.h"

/* One fixture: whether wasi_thread_start has the signature that
   thread-spawn must accept */
struct SpawnCase
{
    const char *fixture;
    bool accepted;
};

class WasiThreadSpawnTest : public ThreadModuleFixture,
                            public testing::WithParamInterface<SpawnCase>
{};

TEST_P(WasiThreadSpawnTest, SpawnChecksThreadStartSignature)
{
    SpawnCase c = GetParam();

    ASSERT_TRUE(Instantiate(c.fixture));

    int32 ret = CallI32("spawn", {});
    if (!c.accepted) {
        EXPECT_EQ(ret, -1);
        EXPECT_EQ(CallI32("flag", {}), 0);
        return;
    }

    EXPECT_GE(ret, 0);
    /* The accepted start function sets the flag, poll for up to 5 seconds */
    int32 flag = 0;
    for (int i = 0; i < 500 && flag == 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        flag = CallI32("flag", {});
    }
    EXPECT_EQ(flag, 1);
}

INSTANTIATE_TEST_SUITE_P(
    Signature, WasiThreadSpawnTest,
    testing::Values(SpawnCase{ "start_valid", true },
                    SpawnCase{ "start_missing", false },
                    SpawnCase{ "start_result_i32", false },
                    SpawnCase{ "start_result_i32x3", false },
                    SpawnCase{ "start_result_i64x4", false },
                    SpawnCase{ "start_param_i64x2_result_i64x6", false },
                    SpawnCase{ "start_param_i32x3", false },
                    SpawnCase{ "start_param_i32", false },
                    SpawnCase{ "start_no_params", false }),
    [](const testing::TestParamInfo<SpawnCase> &info) {
        return std::string(info.param.fixture);
    });

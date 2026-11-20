/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * print_err() is the libc-builtin printf engine's out-of-memory path: it
 * writes "ERR" to the output when a temporary buffer cannot be allocated.
 * The runtime heap is a fixed pool, so the failure is provoked by taking the
 * whole pool before the call.
 */

#include "func_types.h"
#include "test_helper.h"
#include "wasm_export.h"
#include "gtest/gtest.h"

#include <string>
#include <vector>

extern "C" {
extern uint32_t
get_libc_builtin_export_apis(NativeSymbol **p_libc_builtin_apis);
}

namespace {

/* Takes every free byte of the runtime heap pool; gives it back on scope exit
 */
class PoolExhauster
{
  public:
    PoolExhauster()
    {
        for (uint32_t size = 64 * 1024; size > 0; size /= 2) {
            void *p;
            while ((p = wasm_runtime_malloc(size)) != nullptr) {
                blocks_.push_back(p);
            }
        }
    }

    ~PoolExhauster()
    {
        for (void *p : blocks_) {
            wasm_runtime_free(p);
        }
    }

    size_t block_count() const { return blocks_.size(); }

  private:
    std::vector<void *> blocks_;
};

} // namespace

class LibcBuiltinPrintErrTest : public testing::Test
{
  protected:
    void SetUp() override
    {
        n_native_symbols_ = get_libc_builtin_export_apis(&native_symbols_);
        printf_func_ = (printf_func_type)find("printf");
        ASSERT_NE(nullptr, printf_func_);
    }

    void *find(const char *name) const
    {
        for (uint32_t i = 0; i < n_native_symbols_; i++) {
            if (strcmp(native_symbols_[i].symbol, name) == 0) {
                return native_symbols_[i].func_ptr;
            }
        }
        return nullptr;
    }

    WAMRRuntimeRAII<512 * 1024> runtime;
    DummyExecEnv dummy_exec_env{ dummy_wasm_buffer, sizeof(dummy_wasm_buffer),
                                 64 * 1024 };
    NativeSymbol *native_symbols_ = nullptr;
    uint32_t n_native_symbols_ = 0;
    printf_func_type printf_func_ = nullptr;
};

TEST_F(LibcBuiltinPrintErrTest, long_conversion_spec_alloc_failure)
{
    /* A conversion specification of 32 bytes or more does not fit the
     * on-stack format buffer, so it is copied to a heap buffer. */
    const std::string format = "%" + std::string(40, '0') + "d";
    WAMRVaList va_list(dummy_exec_env.get());
    va_list.add(7);

    /* Control run: with free memory the conversion succeeds. */
    testing::internal::CaptureStdout();
    EXPECT_GT(printf_func_(dummy_exec_env.get(), format.c_str(), va_list.get()),
              0);
    EXPECT_EQ(testing::internal::GetCapturedStdout().find("ERR"),
              std::string::npos);

    WAMRVaList va_list_failing(dummy_exec_env.get());
    va_list_failing.add(7);

    PoolExhauster exhauster;
    ASSERT_GT(exhauster.block_count(), 0u);

    testing::internal::CaptureStdout();
    printf_func_(dummy_exec_env.get(), format.c_str(), va_list_failing.get());
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "ERR");
}

TEST_F(LibcBuiltinPrintErrTest, long_string_arg_alloc_failure)
{
    /* The %s output buffer is 128 bytes on the stack; a longer argument
     * needs a heap buffer of the argument length plus 64 bytes. */
    const std::string text(100, 'x');
    WAMRVaList va_list(dummy_exec_env.get());
    va_list.add(text.c_str());

    WAMRVaList va_list_failing(dummy_exec_env.get());
    va_list_failing.add(text.c_str());

    /* Control run: with free memory the whole string is printed. */
    testing::internal::CaptureStdout();
    EXPECT_EQ(printf_func_(dummy_exec_env.get(), "%s", va_list.get()),
              (int)text.size());
    EXPECT_EQ(testing::internal::GetCapturedStdout(), text);

    PoolExhauster exhauster;
    ASSERT_GT(exhauster.block_count(), 0u);

    testing::internal::CaptureStdout();
    printf_func_(dummy_exec_env.get(), "%s", va_list_failing.get());
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "ERR");
}

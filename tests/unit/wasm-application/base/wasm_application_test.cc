/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "wasm_export.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

/* The fixtures are addressed by one file name whose extension depends on the
   run mode: base/CMakeLists.txt selects ".aot" for the aot mode and ".wasm"
   for the interpreter modes. */
#ifndef WASM_APPLICATION_FIXTURE_EXT
#define WASM_APPLICATION_FIXTURE_EXT ".wasm"
#endif

static inline std::string
fixture_path(const std::string &name)
{
    return get_test_binary_dir() + "/" + name + WASM_APPLICATION_FIXTURE_EXT;
}

/* Owns the fixture bytes and the handles created from them.

   common/test_helper.h's WAMRModule/WAMRInstance are deliberately not used
   here: their destructors call wasm_runtime_unload()/
   wasm_runtime_deinstantiate() unconditionally, and both APIs dereference the
   handle, so a fixture that fails to load or instantiate would crash during
   cleanup.  The buffer stays alive for the whole test because an AOT image
   always reads from the caller's buffer, and the interpreter keeps a pointer
   to it in debug/JIT builds. */
class WasmAppFixture
{
  public:
    explicit WasmAppFixture(const std::string &name, uint32_t heap_size = 8192)
    {
        std::ifstream file(fixture_path(name), std::ios::binary);
        buf_.assign(std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>());

        if (!buf_.empty())
            module_ = wasm_runtime_load(buf_.data(), (uint32_t)buf_.size(),
                                        error_buf_, sizeof(error_buf_));

        if (module_)
            inst_ = wasm_runtime_instantiate(module_, 8192, heap_size,
                                             error_buf_, sizeof(error_buf_));
    }

    ~WasmAppFixture()
    {
        if (inst_)
            wasm_runtime_deinstantiate(inst_);
        if (module_)
            wasm_runtime_unload(module_);
    }

    wasm_module_inst_t get_inst() const { return inst_; }
    const char *get_error() const { return error_buf_; }

  private:
    std::vector<uint8_t> buf_;
    wasm_module_t module_ = nullptr;
    wasm_module_inst_t inst_ = nullptr;
    char error_buf_[128] = { 0 };
};

/* The runtime is created and destroyed around every case, so each case starts
   from a clean runtime and instance. */
class wasm_application_test_base : public testing::Test
{
  protected:
    WAMRRuntimeRAII<512 * 1024> runtime;
};

class wasm_application_execute_main_suite : public wasm_application_test_base
{};

class wasm_application_execute_func_suite : public wasm_application_test_base
{};

/* Every case checks all three of: the boolean the application API returns, the
   instance exception (NULL on success, an exact string on failure), and the
   value the wasm code produced (through argv, the app memory, or stdout). */
static inline void
expect_no_exception(wasm_module_inst_t inst)
{
    EXPECT_EQ(nullptr, wasm_runtime_get_exception(inst));
}

static inline void
expect_exception(wasm_module_inst_t inst, const char *expected)
{
    const char *actual = wasm_runtime_get_exception(inst);

    ASSERT_NE(nullptr, actual);
    EXPECT_STREQ(expected, actual);
}

/* The marshalled char **argv lives in the app heap and is freed before
   wasm_application_execute_main() returns, so the fixtures that need it copy
   what the cases check into a stable area of the linear memory: argc at 0,
   the first three char * entries at 8/12/16, the size of the string area at
   20, and the first three strings at 64/80/88. */
static inline uint32_t
app_u32(wasm_module_inst_t inst, uint32_t offset)
{
    void *p = wasm_runtime_addr_app_to_native(inst, offset);

    EXPECT_NE(nullptr, p);
    return p ? *(uint32_t *)p : 0;
}

static inline void
expect_app_string(wasm_module_inst_t inst, uint32_t offset,
                  const char *expected)
{
    const void *p = wasm_runtime_addr_app_to_native(inst, offset);

    ASSERT_NE(nullptr, p);
    EXPECT_STREQ(expected, (const char *)p);
}

/* wasm_application.c rejects every main function whose type is neither
   () -> () nor (i32, i32) -> (i32). */
static inline void
check_invalid_main_function_type(const std::string &fixture_name)
{
    WasmAppFixture fixture(fixture_name);

    if (!fixture.get_inst()) {
        ADD_FAILURE() << "load/instantiate " << fixture_name
                      << " failed: " << fixture.get_error();
        return;
    }

    EXPECT_FALSE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_exception(fixture.get_inst(),
                     "Exception: invalid function type of main function");
}

static inline void
check_execute_func_output(const std::string &fixture_name,
                          const char *func_name, int32_t argc, char *argv[],
                          const char *expected_output)
{
    WasmAppFixture fixture(fixture_name);

    if (!fixture.get_inst()) {
        ADD_FAILURE() << "load/instantiate " << fixture_name
                      << " failed: " << fixture.get_error();
        return;
    }

    /* wasm_application_execute_func() prints the results with os_printf().
       Nothing between CaptureStdout() and GetCapturedStdout() may return
       early, so the call itself is not asserted here. */
    testing::internal::CaptureStdout();
    bool ret = wasm_application_execute_func(fixture.get_inst(), func_name,
                                             argc, argv);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(ret);
    expect_no_exception(fixture.get_inst());
    EXPECT_EQ(expected_output, output);
}

static inline void
check_execute_func_failure(const std::string &fixture_name,
                           const char *func_name, int32_t argc, char *argv[],
                           const char *expected_exception)
{
    WasmAppFixture fixture(fixture_name);

    if (!fixture.get_inst()) {
        ADD_FAILURE() << "load/instantiate " << fixture_name
                      << " failed: " << fixture.get_error();
        return;
    }

    EXPECT_FALSE(wasm_application_execute_func(fixture.get_inst(), func_name,
                                               argc, argv));
    expect_exception(fixture.get_inst(), expected_exception);
}

/*
 * wasm_application_execute_main()
 */

TEST_F(wasm_application_execute_main_suite, main_without_params_succeeds)
{
    WasmAppFixture fixture("main_no_params");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    EXPECT_TRUE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_no_exception(fixture.get_inst());
}

TEST_F(wasm_application_execute_main_suite,
       main_return_value_is_written_to_argv)
{
    WasmAppFixture fixture("main_returns_i32");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    char *argv[] = { (char *)"prog" };

    EXPECT_TRUE(wasm_application_execute_main(fixture.get_inst(), 1, argv));
    expect_no_exception(fixture.get_inst());
    /* The i32 main() returned is copied over the first argv slot, which is how
       iwasm reads the application exit code; argv[0] must not be used again. */
    EXPECT_EQ(42, *(int *)argv);
}

TEST_F(wasm_application_execute_main_suite,
       main_without_argv_does_not_write_return_value)
{
    WasmAppFixture fixture("main_returns_i32");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    /* argc == 0 and argv == NULL are valid for a main function with a result:
       the return value simply has no place to go. */
    EXPECT_TRUE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_no_exception(fixture.get_inst());
}

TEST_F(wasm_application_execute_main_suite,
       main_receives_marshalled_argc_and_argv)
{
    WasmAppFixture fixture("main_argc_argv");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();
    char *argv[] = { (char *)"prog", (char *)"a", (char *)"bb" };

    EXPECT_TRUE(wasm_application_execute_main(inst, 3, argv));
    expect_no_exception(inst);
    EXPECT_EQ(3, *(int *)argv);

    /* The fixture records the marshalled arguments while they exist. */
    EXPECT_EQ(3u, app_u32(inst, 0));

    /* "prog" + "a" + "bb" is 10 bytes, aligned to 12, so the array of char *
       entries sits 12 bytes above the first string, and the entries are one
       string apart. */
    EXPECT_EQ(12u, app_u32(inst, 20));
    EXPECT_EQ(app_u32(inst, 8) + 5, app_u32(inst, 12));
    EXPECT_EQ(app_u32(inst, 12) + 2, app_u32(inst, 16));

    expect_app_string(inst, 64, "prog");
    expect_app_string(inst, 80, "a");
    expect_app_string(inst, 88, "bb");
}

TEST_F(wasm_application_execute_main_suite, main_with_2_params_and_no_args)
{
    WasmAppFixture fixture("main_argc_argv");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();

    /* A char **argv built from zero arguments still allocates the offset
       array, and argc is passed as 0. */
    EXPECT_TRUE(wasm_application_execute_main(inst, 0, NULL));
    expect_no_exception(inst);
    EXPECT_EQ(0u, app_u32(inst, 0));
}

TEST_F(wasm_application_execute_main_suite, extra_argv_entries_are_ignored)
{
    WasmAppFixture fixture("main_argc_argv");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();
    char *argv[] = { (char *)"prog", (char *)"extra" };

    EXPECT_TRUE(wasm_application_execute_main(inst, 1, argv));
    expect_no_exception(inst);
    EXPECT_EQ(1, *(int *)argv);

    EXPECT_EQ(1u, app_u32(inst, 0));
    /* Only "prog" plus its NUL is marshalled, aligned to 8 bytes. */
    EXPECT_EQ(8u, app_u32(inst, 20));
    /* The second slot was never touched, so the extra argv entry was
       ignored. */
    EXPECT_EQ(0u, app_u32(inst, 12));
    expect_app_string(inst, 64, "prog");
}

TEST_F(wasm_application_execute_main_suite,
       main_with_2_params_and_no_result_keeps_argv)
{
    WasmAppFixture fixture("main_argc_argv_void");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();
    char *argv[] = { (char *)"prog" };
    char *saved = argv[0];

    EXPECT_TRUE(wasm_application_execute_main(inst, 1, argv));
    expect_no_exception(inst);
    /* main() has no result, so the argv slot must be left untouched. */
    EXPECT_EQ(saved, argv[0]);
    EXPECT_EQ(1u, app_u32(inst, 0));
}

TEST_F(wasm_application_execute_main_suite, underscore_main_symbol_is_found)
{
    WasmAppFixture fixture("main_underscore");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    EXPECT_TRUE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_no_exception(fixture.get_inst());
}

TEST_F(wasm_application_execute_main_suite, main_argc_argv_symbol_is_found)
{
    WasmAppFixture fixture("main_argc_argv_symbol");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();
    char *argv[] = { (char *)"p", (char *)"q" };

    EXPECT_TRUE(wasm_application_execute_main(inst, 2, argv));
    expect_no_exception(inst);
    EXPECT_EQ(2, *(int *)argv);

    EXPECT_EQ(2u, app_u32(inst, 0));
    /* "p" plus "q" is 4 bytes, already 4-byte aligned, and the second
       argument starts right after the first one. */
    EXPECT_EQ(4u, app_u32(inst, 20));
    EXPECT_EQ(app_u32(inst, 8) + 2, app_u32(inst, 12));
    expect_app_string(inst, 64, "p");
    expect_app_string(inst, 80, "q");
}

TEST_F(wasm_application_execute_main_suite, main_is_preferred_over_argv_symbol)
{
    WasmAppFixture fixture("main_both_symbols");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();
    char *argv[] = { (char *)"p", (char *)"q" };

    EXPECT_TRUE(wasm_application_execute_main(inst, 2, argv));
    expect_no_exception(inst);
    EXPECT_EQ(42, *(int *)argv);

    /* The __main_argc_argv function must not have run: it is the one that
       stores argc at offset 0 and the first argument at offset 8. */
    EXPECT_EQ(0u, app_u32(inst, 0));
    EXPECT_EQ(0u, app_u32(inst, 8));
}

TEST_F(wasm_application_execute_main_suite, missing_entry_point_symbol)
{
    WasmAppFixture fixture("main_missing");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    EXPECT_FALSE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_exception(fixture.get_inst(),
                     "Exception: lookup the entry point symbol (like main, "
                     "_main, __main_argc_argv) failed");
}

TEST_F(wasm_application_execute_main_suite, imported_main_function)
{
    WasmAppFixture fixture("main_import");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    /* The module imports main from env; the import is unresolved (WAMR only
       warns about unlinked function imports), and an entry point that is an
       import cannot be executed. */
    EXPECT_FALSE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_exception(fixture.get_inst(),
                     "Exception: lookup main function failed");
}

TEST_F(wasm_application_execute_main_suite, main_with_one_param)
{
    check_invalid_main_function_type("main_bad_param_count");
}

TEST_F(wasm_application_execute_main_suite, main_with_i64_first_param)
{
    check_invalid_main_function_type("main_bad_param_types_lo");
}

TEST_F(wasm_application_execute_main_suite, main_with_i64_second_param)
{
    check_invalid_main_function_type("main_bad_param_types_hi");
}

TEST_F(wasm_application_execute_main_suite, main_with_i64_result)
{
    check_invalid_main_function_type("main_bad_result_type");
}

TEST_F(wasm_application_execute_main_suite, main_with_two_results)
{
    check_invalid_main_function_type("main_too_many_results");
}

TEST_F(wasm_application_execute_main_suite, trapping_main)
{
    WasmAppFixture fixture("main_trap");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    EXPECT_FALSE(wasm_application_execute_main(fixture.get_inst(), 0, NULL));
    expect_exception(fixture.get_inst(), "Exception: unreachable");
}

TEST_F(wasm_application_execute_main_suite, existing_exception_is_reported)
{
    WasmAppFixture fixture("main_no_params");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();

    wasm_runtime_set_exception(inst, "stale");
    /* wasm_application_execute_main() does not clear an exception set before
       the call; it only reports failure when one is set afterwards. */
    EXPECT_FALSE(wasm_application_execute_main(inst, 0, NULL));
    expect_exception(inst, "Exception: stale");
}

/*
 * wasm_application_execute_func()
 */

TEST_F(wasm_application_execute_func_suite, echo_i32_prints_result)
{
    char *argv[] = { (char *)"42" };

    check_execute_func_output("funcs", "echo_i32", 1, argv, "0x2a:i32\n");
}

TEST_F(wasm_application_execute_func_suite, negative_i32_argument_wraps)
{
    char *argv[] = { (char *)"-1" };

    check_execute_func_output("funcs", "echo_i32", 1, argv, "0xffffffff:i32\n");
}

TEST_F(wasm_application_execute_func_suite, hex_i32_argument)
{
    char *argv[] = { (char *)"0x10" };

    check_execute_func_output("funcs", "echo_i32", 1, argv, "0x10:i32\n");
}

TEST_F(wasm_application_execute_func_suite, trailing_underscore_is_accepted)
{
    char *argv[] = { (char *)"5_" };

    /* A trailing underscore is not part of the number, but
       wasm_application.c deliberately tolerates it. */
    check_execute_func_output("funcs", "echo_i32", 1, argv, "0x5:i32\n");
}

TEST_F(wasm_application_execute_func_suite, echo_i64_prints_result)
{
    char *argv[] = { (char *)"0x1122334455667788" };

    check_execute_func_output("funcs", "echo_i64", 1, argv,
                              "0x1122334455667788:i64\n");
}

TEST_F(wasm_application_execute_func_suite, echo_f32_prints_result)
{
    char *argv[] = { (char *)"1.5" };

    check_execute_func_output("funcs", "echo_f32", 1, argv, "1.5:f32\n");
}

TEST_F(wasm_application_execute_func_suite, echo_f64_prints_result)
{
    char *argv[] = { (char *)"0.1" };

    check_execute_func_output("funcs", "echo_f64", 1, argv, "0.1:f64\n");
}

TEST_F(wasm_application_execute_func_suite, two_results_are_comma_separated)
{
    char *argv[] = { (char *)"3", (char *)"4" };

    check_execute_func_output("funcs", "two_i32_results", 2, argv,
                              "0x3:i32,0x4:i32\n");
}

TEST_F(wasm_application_execute_func_suite, more_results_than_params)
{
    char *argv[] = { (char *)"5" };

    /* One parameter, three results: the argument buffer has to be sized for
       the result cells. */
    check_execute_func_output("funcs", "expand_i32", 1, argv,
                              "0x5:i32,0x6:i32,0x7:i32\n");
}

TEST_F(wasm_application_execute_func_suite, negative_nan_payload)
{
    char *argv[] = { (char *)"-nan:0x412345" };

    /* The "-nan:<payload>" syntax sets the sign and mantissa bits of the NaN
       the parser builds; the fixture returns them with i32.reinterpret_f32.
       The payload carries the quiet bit, because a signaling NaN is quieted
       on its way through the registers the AOT code uses on some targets. */
    check_execute_func_output("funcs", "f32_bits", 1, argv, "0xffc12345:i32\n");
}

TEST_F(wasm_application_execute_func_suite, f64_nan_payload)
{
    char *argv[] = { (char *)"nan:0x0008000012345678" };

    /* As above, with the quiet bit of the double NaN already set. */
    check_execute_func_output("funcs", "f64_bits", 1, argv,
                              "0x7ff8000012345678:i64\n");
}

TEST_F(wasm_application_execute_func_suite, function_without_arguments)
{
    check_execute_func_output("funcs", "void_func", 0, NULL, "\n");
}

TEST_F(wasm_application_execute_func_suite, argv_is_ignored_when_argc_is_zero)
{
    char *argv[] = { (char *)"x" };

    check_execute_func_output("funcs", "void_func", 0, argv, "\n");
}

TEST_F(wasm_application_execute_func_suite, extra_argv_entries_are_ignored)
{
    char *argv[] = { (char *)"42", (char *)"99" };

    check_execute_func_output("funcs", "echo_i32", 1, argv, "0x2a:i32\n");
}

TEST_F(wasm_application_execute_func_suite, argc_must_match_param_count)
{
    char *argv[] = { (char *)"x" };

    check_execute_func_failure("funcs", "echo_i32", 0, argv,
                               "Exception: invalid input argument count");
}

TEST_F(wasm_application_execute_func_suite, too_few_arguments)
{
    char *argv[] = { (char *)"1" };

    check_execute_func_failure("funcs", "add_i32", 1, argv,
                               "Exception: invalid input argument count");
}

TEST_F(wasm_application_execute_func_suite, too_many_arguments)
{
    char *argv[] = { (char *)"1", (char *)"2", (char *)"3" };

    check_execute_func_failure("funcs", "add_i32", 3, argv,
                               "Exception: invalid input argument count");
}

TEST_F(wasm_application_execute_func_suite, empty_argument)
{
    char *argv[] = { (char *)"" };

    check_execute_func_failure("funcs", "echo_i32", 1, argv,
                               "Exception: invalid input argument 0");
}

TEST_F(wasm_application_execute_func_suite, malformed_argument)
{
    char *argv[] = { (char *)"abc" };

    check_execute_func_failure("funcs", "echo_i32", 1, argv,
                               "Exception: invalid input argument 0: abc");
}

TEST_F(wasm_application_execute_func_suite, unknown_function)
{
    check_execute_func_failure("funcs", "nosuchfunc", 0, NULL,
                               "Exception: lookup function nosuchfunc failed");
}

TEST_F(wasm_application_execute_func_suite, long_function_name_is_truncated)
{
    WasmAppFixture fixture("funcs");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    std::string long_name(200, 'a');

    EXPECT_FALSE(wasm_application_execute_func(fixture.get_inst(),
                                               long_name.c_str(), 0, NULL));

    const char *exc = wasm_runtime_get_exception(fixture.get_inst());
    ASSERT_NE(nullptr, exc);

    /* The message is built in a 128 byte buffer and then copied into the
       128 byte exception buffer: it is truncated, never overrun. */
    static const char prefix[] = "Exception: lookup function ";
    EXPECT_EQ(127u, strlen(exc));
    EXPECT_EQ(0, strncmp(exc, prefix, sizeof(prefix) - 1));
}

TEST_F(wasm_application_execute_func_suite, trapping_function)
{
    check_execute_func_failure("funcs", "trap_func", 0, NULL,
                               "Exception: unreachable");
}

TEST_F(wasm_application_execute_func_suite, divide_by_zero)
{
    char *argv[] = { (char *)"1", (char *)"0" };

    check_execute_func_failure("funcs", "div_i32", 2, argv,
                               "Exception: integer divide by zero");
}

TEST_F(wasm_application_execute_func_suite, signed_division_overflow)
{
    char *argv[] = { (char *)"0x80000000", (char *)"-1" };

    check_execute_func_failure("funcs", "div_i32", 2, argv,
                               "Exception: integer overflow");
}

TEST_F(wasm_application_execute_func_suite, invalid_conversion_to_integer)
{
    char *argv[] = { (char *)"nan" };

    check_execute_func_failure("funcs", "trunc_f32_s", 1, argv,
                               "Exception: invalid conversion to integer");
}

TEST_F(wasm_application_execute_func_suite, out_of_bounds_memory_access)
{
    char *argv[] = { (char *)"0x7ffffffc" };

    check_execute_func_failure("funcs", "load_i32", 1, argv,
                               "Exception: out of bounds memory access");
}

TEST_F(wasm_application_execute_func_suite, unlinked_import_call)
{
    WasmAppFixture fixture("funcs_import");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    char *argv[] = { (char *)"7" };

    EXPECT_FALSE(wasm_application_execute_func(fixture.get_inst(),
                                               "call_import", 1, argv));

    const char *exc = wasm_runtime_get_exception(fixture.get_inst());
    ASSERT_NE(nullptr, exc);

    /* The interpreter appends the module and field names to the message, the
       AOT unlinked-import trap only sets the exception id, so compare the
       common prefix. */
    static const char prefix[] = "Exception: failed to call unlinked import "
                                 "function";
    EXPECT_EQ(0, strncmp(exc, prefix, sizeof(prefix) - 1));
}

TEST_F(wasm_application_execute_func_suite, existing_exception_is_cleared)
{
    WasmAppFixture fixture("funcs");
    ASSERT_NE(nullptr, fixture.get_inst()) << fixture.get_error();

    wasm_module_inst_t inst = fixture.get_inst();

    wasm_runtime_set_exception(inst, "stale");

    /* Unlike wasm_application_execute_main(), wasm_application_execute_func()
       clears an exception set before the call. */
    testing::internal::CaptureStdout();
    bool ret = wasm_application_execute_func(inst, "void_func", 0, NULL);
    std::string output = testing::internal::GetCapturedStdout();

    EXPECT_TRUE(ret);
    expect_no_exception(inst);
    EXPECT_EQ("\n", output);
}

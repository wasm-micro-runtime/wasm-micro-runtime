/*
 * Copyright (C) 2026 WAMR Community.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include "wasm_runtime_common.h"
#include "bh_platform.h"

/*
 * (module
 *   (import "env" "call_count" (func $call_count (param i32) (result i32)))
 *   (func $count (export "count") (param $n i32) (result i32)
 *     (local $i i32)
 *     (block $done
 *       (loop $top
 *         (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
 *         (local.set $i (i32.add (local.get $i) (i32.const 1)))
 *         (br $top)))
 *     (local.get $i))
 *   (func $fib (export "fib") (param $n i32) (result i64)
 *     (if (result i64) (i32.lt_u (local.get $n) (i32.const 2))
 *       (then (i64.extend_i32_u (local.get $n)))
 *       (else
 *         (i64.add
 *           (call $fib (i32.sub (local.get $n) (i32.const 1)))
 *           (call $fib (i32.sub (local.get $n) (i32.const 2)))))))
 *   (func (export "count_then_trap") (param $n i32) (result i32)
 *     (drop (call $count (local.get $n)))
 *     (unreachable))
 *   (func (export "count_from_native") (param $n i32) (result i32)
 *     (call $call_count (local.get $n))))
 */
static const uint8_t wasm_resume[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0B, 0x02, 0x60,
    0x01, 0x7F, 0x01, 0x7F, 0x60, 0x01, 0x7F, 0x01, 0x7E, 0x02, 0x12, 0x01,
    0x03, 0x65, 0x6E, 0x76, 0x0A, 0x63, 0x61, 0x6C, 0x6C, 0x5F, 0x63, 0x6F,
    0x75, 0x6E, 0x74, 0x00, 0x00, 0x03, 0x05, 0x04, 0x00, 0x01, 0x00, 0x00,
    0x07, 0x35, 0x04, 0x05, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x00, 0x01, 0x03,
    0x66, 0x69, 0x62, 0x00, 0x02, 0x0F, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x5F,
    0x74, 0x68, 0x65, 0x6E, 0x5F, 0x74, 0x72, 0x61, 0x70, 0x00, 0x03, 0x11,
    0x63, 0x6F, 0x75, 0x6E, 0x74, 0x5F, 0x66, 0x72, 0x6F, 0x6D, 0x5F, 0x6E,
    0x61, 0x74, 0x69, 0x76, 0x65, 0x00, 0x04, 0x0A, 0x4C, 0x04, 0x1C, 0x01,
    0x01, 0x7F, 0x02, 0x40, 0x03, 0x40, 0x20, 0x01, 0x20, 0x00, 0x4F, 0x0D,
    0x01, 0x20, 0x01, 0x41, 0x01, 0x6A, 0x21, 0x01, 0x0C, 0x00, 0x0B, 0x0B,
    0x20, 0x01, 0x0B, 0x1D, 0x00, 0x20, 0x00, 0x41, 0x02, 0x49, 0x04, 0x7E,
    0x20, 0x00, 0xAD, 0x05, 0x20, 0x00, 0x41, 0x01, 0x6B, 0x10, 0x02, 0x20,
    0x00, 0x41, 0x02, 0x6B, 0x10, 0x02, 0x7C, 0x0B, 0x0B, 0x08, 0x00, 0x20,
    0x00, 0x10, 0x01, 0x1A, 0x00, 0x0B, 0x06, 0x00, 0x20, 0x00, 0x10, 0x00,
    0x0B
};

/*
 * (module
 *   (func (export "count") (param $n i32) (result i32)
 *     (local $i i32)
 *     (block $done
 *       (loop $top
 *         (br_if $done (i32.ge_u (local.get $i) (local.get $n)))
 *         (local.set $i (i32.add (local.get $i) (i32.const 1)))
 *         (br $top)))
 *     (local.get $i)))
 */
static const uint8_t wasm_counter[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01,
    0x60, 0x01, 0x7F, 0x01, 0x7F, 0x03, 0x02, 0x01, 0x00, 0x07, 0x09,
    0x01, 0x05, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x00, 0x00, 0x0A, 0x1E,
    0x01, 0x1C, 0x01, 0x01, 0x7F, 0x02, 0x40, 0x03, 0x40, 0x20, 0x01,
    0x20, 0x00, 0x4F, 0x0D, 0x01, 0x20, 0x01, 0x41, 0x01, 0x6A, 0x21,
    0x01, 0x0C, 0x00, 0x0B, 0x0B, 0x20, 0x01, 0x0B
};

/*
 * (module
 *   (import "counter" "count" (func $count (param i32) (result i32)))
 *   (export "count_reexported" (func $count))
 *   (func (export "count_in_counter") (param $n i32) (result i32)
 *     (call $count (local.get $n))))
 */
static const uint8_t wasm_counter_user[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01, 0x60,
    0x01, 0x7F, 0x01, 0x7F, 0x02, 0x11, 0x01, 0x07, 0x63, 0x6F, 0x75, 0x6E,
    0x74, 0x65, 0x72, 0x05, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x00, 0x00, 0x03,
    0x02, 0x01, 0x00, 0x07, 0x27, 0x02, 0x10, 0x63, 0x6F, 0x75, 0x6E, 0x74,
    0x5F, 0x72, 0x65, 0x65, 0x78, 0x70, 0x6F, 0x72, 0x74, 0x65, 0x64, 0x00,
    0x00, 0x10, 0x63, 0x6F, 0x75, 0x6E, 0x74, 0x5F, 0x69, 0x6E, 0x5F, 0x63,
    0x6F, 0x75, 0x6E, 0x74, 0x65, 0x72, 0x00, 0x01, 0x0A, 0x08, 0x01, 0x06,
    0x00, 0x20, 0x00, 0x10, 0x00, 0x0B
};

/* Call the wasm function "count" from a native function. */
static int32_t
call_count_wrapper(wasm_exec_env_t exec_env, int32_t n)
{
    wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
    wasm_function_inst_t func =
        wasm_runtime_lookup_function(module_inst, "count");
    uint32_t argv[1] = { (uint32_t)n };

    if (!wasm_runtime_call_wasm(exec_env, func, 1, argv))
        return -1;
    return (int32_t)argv[0];
}

static NativeSymbol native_symbols[] = {
    { "call_count", (void *)call_count_wrapper, "(i)i", NULL },
};

class InstructionMeteringResumeTest : public testing::Test
{
  protected:
    virtual void SetUp()
    {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));
        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
        ASSERT_EQ(wasm_runtime_full_init(&init_args), true);
        ASSERT_TRUE(wasm_runtime_register_natives(
            "env", native_symbols,
            sizeof(native_symbols) / sizeof(native_symbols[0])));

        /* wasm_runtime_load() modifies the buffer, so load a copy. */
        bh_memcpy_s(wasm_buf, sizeof(wasm_buf), wasm_resume,
                    sizeof(wasm_resume));
        module = wasm_runtime_load(wasm_buf, sizeof(wasm_buf), error_buf,
                                   sizeof(error_buf));
        ASSERT_NE(module, nullptr) << error_buf;
        module_inst = wasm_runtime_instantiate(module, 8192, 0, error_buf,
                                               sizeof(error_buf));
        ASSERT_NE(module_inst, nullptr) << error_buf;
        exec_env = wasm_runtime_create_exec_env(module_inst, 8192);
        ASSERT_NE(exec_env, nullptr);
    }

    virtual void TearDown()
    {
        if (exec_env)
            wasm_runtime_destroy_exec_env(exec_env);
        if (module_inst)
            wasm_runtime_deinstantiate(module_inst);
        if (module)
            wasm_runtime_unload(module);
        wasm_runtime_destroy();
    }

    /* Call a function with the given limit, return false on a trap. */
    bool call(const char *name, uint32_t arg, int limit, uint32_t *argv)
    {
        wasm_function_inst_t func =
            wasm_runtime_lookup_function(module_inst, name);

        EXPECT_NE(func, nullptr) << name;
        argv[0] = arg;
        wasm_runtime_clear_exception(module_inst);
        wasm_runtime_set_instruction_count_limit(exec_env, limit);
        return wasm_runtime_call_wasm(exec_env, func, 1, argv);
    }

    /*
     * Call a function with the given limit, and resume it with the same
     * limit until it isn't suspended any more. Return false on a trap,
     * and the number of times the function was run in runs.
     */
    bool call_and_resume(const char *name, uint32_t arg, int limit,
                         uint32_t *argv, uint32_t *runs)
    {
        bool ok = call(name, arg, limit, argv);

        *runs = 1;
        while (!ok && wasm_runtime_can_resume_wasm(exec_env)) {
            EXPECT_TRUE(limit_exceeded());
            ok = wasm_runtime_resume_wasm(exec_env, 2, argv);
            (*runs)++;
        }
        return ok;
    }

    bool has_exception(const char *expected)
    {
        const char *exception = wasm_runtime_get_exception(module_inst);
        return exception && strstr(exception, expected);
    }

    bool limit_exceeded()
    {
        return has_exception("instruction limit exceeded");
    }

    static uint64_t to_u64(const uint32_t *argv)
    {
        uint64_t value;
        memcpy(&value, argv, sizeof(value));
        return value;
    }

  public:
    char global_heap_buf[512 * 1024];
    RuntimeInitArgs init_args;
    char error_buf[256];
    uint8_t wasm_buf[sizeof(wasm_resume)];
    wasm_module_t module = nullptr;
    wasm_module_inst_t module_inst = nullptr;
    wasm_exec_env_t exec_env = nullptr;
};

TEST_F(InstructionMeteringResumeTest, completed_call_cannot_be_resumed)
{
    uint32_t argv[2] = { 0 };

    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    ASSERT_TRUE(call("count", 10, -1, argv))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 10u);
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    EXPECT_FALSE(wasm_runtime_resume_wasm(exec_env, 2, argv));
    EXPECT_TRUE(has_exception("no suspended call to resume"));
}

TEST_F(InstructionMeteringResumeTest, resume_runs_each_instruction_once)
{
    uint32_t argv[2] = { 0 }, cost, runs;

    /* With a budget of one instruction, each run executes one of them. */
    ASSERT_TRUE(call_and_resume("count", 100, 1, argv, &cost))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 100u);
    EXPECT_GT(cost, 100u);

    for (uint32_t limit : { 2u, 7u, 100u, cost - 1, cost, cost + 1 }) {
        argv[0] = 0;
        ASSERT_TRUE(call_and_resume("count", 100, (int)limit, argv, &runs))
            << wasm_runtime_get_exception(module_inst);
        EXPECT_EQ(argv[0], 100u) << "limit " << limit;
        EXPECT_EQ(runs, (cost + limit - 1) / limit) << "limit " << limit;
        EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));
    }
}

TEST_F(InstructionMeteringResumeTest, resume_across_function_calls)
{
    uint32_t argv[2] = { 0 }, cost, runs;

    /* fib(15) keeps calling itself and returning, with an i64 result. */
    ASSERT_TRUE(call_and_resume("fib", 15, 1, argv, &cost))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(to_u64(argv), 610u);

    for (uint32_t limit : { 3u, 10u, 1000u }) {
        argv[0] = argv[1] = 0;
        ASSERT_TRUE(call_and_resume("fib", 15, (int)limit, argv, &runs))
            << wasm_runtime_get_exception(module_inst);
        EXPECT_EQ(to_u64(argv), 610u) << "limit " << limit;
        EXPECT_EQ(runs, (cost + limit - 1) / limit) << "limit " << limit;
    }
}

TEST_F(InstructionMeteringResumeTest, limit_can_be_changed_before_resuming)
{
    uint32_t argv[2] = { 0 };

    EXPECT_FALSE(call("count", 1000, 50, argv));
    EXPECT_TRUE(limit_exceeded());
    ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));

    /* Nothing is executed without any budget. */
    wasm_runtime_set_instruction_count_limit(exec_env, 0);
    EXPECT_FALSE(wasm_runtime_resume_wasm(exec_env, 2, argv));
    EXPECT_TRUE(limit_exceeded());
    ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));

    wasm_runtime_set_instruction_count_limit(exec_env, -1);
    EXPECT_TRUE(wasm_runtime_resume_wasm(exec_env, 2, argv))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 1000u);
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));
}

TEST_F(InstructionMeteringResumeTest, failed_resume_ends_the_call)
{
    uint32_t argv[2] = { 0 }, runs;

    EXPECT_FALSE(call("fib", 15, 50, argv));
    ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));

    /* The i64 result of fib takes two cells. A resume which fails for
       another reason than the limit doesn't leave a call to resume, so
       that a loop on wasm_runtime_can_resume_wasm() ends. */
    EXPECT_FALSE(wasm_runtime_resume_wasm(exec_env, 1, argv));
    EXPECT_TRUE(has_exception("invalid argument count"));
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    /* The exec env can still be used. */
    ASSERT_TRUE(call_and_resume("fib", 15, 50, argv, &runs))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(to_u64(argv), 610u);
}

TEST_F(InstructionMeteringResumeTest, resume_does_not_hide_another_exception)
{
    uint32_t argv[2] = { 0 };

    EXPECT_FALSE(call("count", 1000, 50, argv));
    ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));

    /* e.g. the instance was terminated while the call was suspended */
    wasm_runtime_set_exception(module_inst, "terminated by user");
    wasm_runtime_set_instruction_count_limit(exec_env, -1);
    EXPECT_FALSE(wasm_runtime_resume_wasm(exec_env, 2, argv));
    EXPECT_TRUE(has_exception("terminated by user"));
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));
}

TEST_F(InstructionMeteringResumeTest, exception_can_be_cleared_before_resuming)
{
    uint32_t argv[2] = { 0 };

    EXPECT_FALSE(call("count", 1000, 50, argv));
    wasm_runtime_clear_exception(module_inst);
    ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));

    wasm_runtime_set_instruction_count_limit(exec_env, -1);
    EXPECT_TRUE(wasm_runtime_resume_wasm(exec_env, 2, argv))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 1000u);
}

TEST_F(InstructionMeteringResumeTest, new_call_discards_suspended_call)
{
    uint32_t argv[2] = { 0 };

    /* The frames of the suspended calls would overflow the wasm stack if
       they weren't freed by the next call. */
    for (int i = 0; i < 10000; i++) {
        ASSERT_FALSE(call("fib", 15, 50, argv));
        ASSERT_TRUE(wasm_runtime_can_resume_wasm(exec_env));
    }

    ASSERT_TRUE(call("count", 10, -1, argv))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 10u);
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    EXPECT_FALSE(wasm_runtime_resume_wasm(exec_env, 2, argv));
    EXPECT_TRUE(has_exception("no suspended call to resume"));
}

TEST_F(InstructionMeteringResumeTest, trap_after_resume_ends_the_call)
{
    uint32_t argv[2] = { 0 }, runs;

    EXPECT_FALSE(call_and_resume("count_then_trap", 100, 30, argv, &runs));
    EXPECT_GT(runs, 1u);
    EXPECT_TRUE(has_exception("unreachable"));
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    /* The exec env can still be used. */
    ASSERT_TRUE(call_and_resume("fib", 15, 30, argv, &runs))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(to_u64(argv), 610u);
}

TEST_F(InstructionMeteringResumeTest, call_from_native_cannot_be_resumed)
{
    uint32_t argv[2] = { 0 };

    /* The native function which called "count" has returned when the
       limit is reported, there is nothing to resume. */
    EXPECT_FALSE(call("count_from_native", 1000, 50, argv));
    EXPECT_TRUE(limit_exceeded());
    EXPECT_FALSE(wasm_runtime_can_resume_wasm(exec_env));

    ASSERT_TRUE(call("count_from_native", 1000, -1, argv))
        << wasm_runtime_get_exception(module_inst);
    EXPECT_EQ(argv[0], 1000u);
}

TEST_F(InstructionMeteringResumeTest, call_in_another_module_cannot_be_resumed)
{
    static uint8_t counter_buf[sizeof(wasm_counter)];
    static uint8_t user_buf[sizeof(wasm_counter_user)];
    wasm_module_t counter, user;
    wasm_module_inst_t user_inst;
    wasm_exec_env_t user_exec_env;
    wasm_function_inst_t func;
    uint32_t argv[2] = { 0 };

    bh_memcpy_s(counter_buf, sizeof(counter_buf), wasm_counter,
                sizeof(wasm_counter));
    counter = wasm_runtime_load(counter_buf, sizeof(counter_buf), error_buf,
                                sizeof(error_buf));
    ASSERT_NE(counter, nullptr) << error_buf;
    /* The registered module is unloaded when the runtime is destroyed. */
    ASSERT_TRUE(wasm_runtime_register_module("counter", counter, error_buf,
                                             sizeof(error_buf)))
        << error_buf;

    bh_memcpy_s(user_buf, sizeof(user_buf), wasm_counter_user,
                sizeof(wasm_counter_user));
    user = wasm_runtime_load(user_buf, sizeof(user_buf), error_buf,
                             sizeof(error_buf));
    ASSERT_NE(user, nullptr) << error_buf;
    user_inst =
        wasm_runtime_instantiate(user, 8192, 0, error_buf, sizeof(error_buf));
    ASSERT_NE(user_inst, nullptr) << error_buf;
    user_exec_env = wasm_runtime_create_exec_env(user_inst, 8192);
    ASSERT_NE(user_exec_env, nullptr);

    /* The function of the other module instance is run by another
       invocation of the interpreter, whether it is called by the host or
       by a wasm function: its frames can't be resumed in this instance. */
    for (const char *name : { "count_reexported", "count_in_counter" }) {
        func = wasm_runtime_lookup_function(user_inst, name);
        ASSERT_NE(func, nullptr) << name;

        argv[0] = 1000;
        wasm_runtime_clear_exception(user_inst);
        wasm_runtime_set_instruction_count_limit(user_exec_env, 50);
        if (!wasm_runtime_call_wasm(user_exec_env, func, 1, argv)) {
            EXPECT_NE(strstr(wasm_runtime_get_exception(user_inst),
                             "instruction limit exceeded"),
                      nullptr)
                << name;
        }
        else {
            /* The fast interpreter doesn't pass the argument to a
               re-exported function called by the host, which then
               returns before the limit. */
            EXPECT_STREQ(name, "count_reexported");
            EXPECT_EQ(WASM_ENABLE_FAST_INTERP, 1);
        }
        EXPECT_FALSE(wasm_runtime_can_resume_wasm(user_exec_env)) << name;
        EXPECT_FALSE(wasm_runtime_resume_wasm(user_exec_env, 2, argv)) << name;
    }

    wasm_runtime_destroy_exec_env(user_exec_env);
    wasm_runtime_deinstantiate(user_inst);
    wasm_runtime_unload(user);
}

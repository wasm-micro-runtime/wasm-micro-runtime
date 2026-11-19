/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * wasm_runtime_common.c APIs that need a module instance or an execution
 * environment: the exported globals/tables, function types, indirect and
 * variadic calls, the user/custom data slots, termination, and the
 * module-heap helpers.  The cases run on api.wasm and, when the runtime
 * is built with AOT, on api.aot too.
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"

#include <string>
#include <vector>

namespace {

std::vector<std::string>
fixture_files()
{
    std::string dir = get_test_binary_dir();
    std::vector<std::string> files = { dir + "/api.wasm" };
#if WASM_ENABLE_AOT != 0
    files.push_back(dir + "/api.aot");
#endif
    return files;
}

/* What env.host_add saw when the guest called it. */
struct HostCallRecord {
    void *attachment = nullptr;
    void *user_data = nullptr;
    wasm_module_t module = nullptr;
    int calls = 0;
};

HostCallRecord g_host_call;
int g_attachment_marker;

int
host_add_native(wasm_exec_env_t exec_env, int a, int b)
{
    g_host_call.attachment = wasm_runtime_get_function_attachment(exec_env);
    g_host_call.user_data = wasm_runtime_get_user_data(exec_env);
    g_host_call.module = wasm_exec_env_get_module(exec_env);
    g_host_call.calls++;
    return a + b;
}

/* Raw natives get their arguments as 64-bit slots and return in slot 0. */
void
host_raw_add_native(wasm_exec_env_t exec_env, uint64_t *args)
{
    args[0] = (uint32_t)args[0] + (uint32_t)args[1];
}

void
host_raw_ptr_native(wasm_exec_env_t exec_env, uint64_t *args)
{
    /* The signature "(*)i" makes the runtime pass a native address. */
    args[0] = *(uint32_t *)(uintptr_t)args[0] + 1;
}

NativeSymbol host_raw_symbols[] = {
    { "host_raw", (void *)host_raw_add_native, "(ii)i", nullptr },
    { "host_raw_ptr", (void *)host_raw_ptr_native, "(*)i", nullptr }
};

NativeSymbol host_symbols[] = { { "host_add", (void *)host_add_native, "(ii)i",
                                  &g_attachment_marker } };

} // namespace

/* api.wasm / api.aot loaded, instantiated, with one execution environment. */
class ApiInstance
{
  public:
    explicit ApiInstance(const std::string &path)
    {
        buf_ = (uint8_t *)bh_read_file_to_buffer(path.c_str(), &size_);
        if (!buf_)
            return;
        module_ = wasm_runtime_load(buf_, size_, error_, sizeof(error_));
        if (!module_)
            return;
        inst_ = wasm_runtime_instantiate(module_, 16 * 1024, 16 * 1024, error_,
                                         sizeof(error_));
        if (!inst_)
            return;
        exec_env_ = wasm_runtime_create_exec_env(inst_, 16 * 1024);
    }

    ~ApiInstance()
    {
        if (exec_env_)
            wasm_runtime_destroy_exec_env(exec_env_);
        if (inst_)
            wasm_runtime_deinstantiate(inst_);
        if (module_)
            wasm_runtime_unload(module_);
        if (buf_)
            wasm_runtime_free(buf_);
    }

    bool ok() const { return exec_env_ != nullptr; }
    const char *error() const { return error_; }
    wasm_module_t module() const { return module_; }
    wasm_module_inst_t inst() const { return inst_; }
    wasm_exec_env_t exec_env() const { return exec_env_; }

    wasm_function_inst_t func(const char *name) const
    {
        return wasm_runtime_lookup_function(inst_, name);
    }

  private:
    uint8_t *buf_ = nullptr;
    uint32_t size_ = 0;
    wasm_module_t module_ = nullptr;
    wasm_module_inst_t inst_ = nullptr;
    wasm_exec_env_t exec_env_ = nullptr;
    char error_[128] = { 0 };
};

class wasm_runtime_common_instance_api_suite : public testing::Test
{
  protected:
    void SetUp() override
    {
        g_host_call = HostCallRecord();
        ASSERT_TRUE(wasm_runtime_register_natives("env", host_symbols, 1));
        ASSERT_TRUE(
            wasm_runtime_register_natives_raw("env", host_raw_symbols, 2));
    }

    void TearDown() override
    {
        wasm_runtime_unregister_natives("env", host_raw_symbols);
        wasm_runtime_unregister_natives("env", host_symbols);
    }

    WAMRRuntimeRAII<512 * 1024> runtime;
};

#define FOR_EACH_FIXTURE(app)                                      \
    for (const std::string &path_ : fixture_files())               \
        for (bool once_ = true; once_;)                            \
            for (SCOPED_TRACE(path_); once_; once_ = false)        \
                for (ApiInstance app(path_); once_; once_ = false) \
                    if (!app.ok()) {                               \
                        ADD_FAILURE() << app.error();              \
                    }                                              \
                    else

TEST_F(wasm_runtime_common_instance_api_suite, module_and_custom_data)
{
    FOR_EACH_FIXTURE(app)
    {
        int custom = 0;

        EXPECT_EQ(app.module(), wasm_runtime_get_module(app.inst()));

        EXPECT_EQ(nullptr, wasm_runtime_get_custom_data(app.inst()));
        wasm_runtime_set_custom_data(app.inst(), &custom);
        EXPECT_EQ(&custom, wasm_runtime_get_custom_data(app.inst()));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, native_call_environment)
{
    FOR_EACH_FIXTURE(app)
    {
        int user = 0;
        g_host_call = HostCallRecord();
        wasm_runtime_set_user_data(app.exec_env(), &user);
        EXPECT_EQ(&user, wasm_runtime_get_user_data(app.exec_env()));

        /* Rebinding the execution environment to its own instance is a
         * no-op that must keep the environment usable. */
        wasm_runtime_set_module_inst(app.exec_env(), app.inst());
        EXPECT_EQ(app.inst(), wasm_runtime_get_module_inst(app.exec_env()));

        uint32_t argv[2] = { 20, 22 };
        ASSERT_TRUE(wasm_runtime_call_wasm(app.exec_env(),
                                           app.func("call_host"), 2, argv))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(42u, argv[0]);

        /* The native function saw the attachment registered with it, the
         * user data of the environment and the module of the caller. */
        EXPECT_EQ(1, g_host_call.calls);
        EXPECT_EQ(&g_attachment_marker, g_host_call.attachment);
        EXPECT_EQ(&user, g_host_call.user_data);
        EXPECT_EQ(app.module(), g_host_call.module);
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, raw_native_call)
{
    FOR_EACH_FIXTURE(app)
    {
        uint32_t argv[2] = { 40, 2 };
        ASSERT_TRUE(wasm_runtime_call_wasm(app.exec_env(), app.func("call_raw"),
                                           2, argv))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(42u, argv[0]);

        /* The guest's linear memory holds 42 at offset 16. */
        argv[0] = 16;
        ASSERT_TRUE(wasm_runtime_call_wasm(app.exec_env(),
                                           app.func("call_raw_ptr"), 1, argv))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(43u, argv[0]);

        /* An address outside the linear memory is refused before the native
         * function runs. */
        argv[0] = 0x7ffffff0;
        EXPECT_FALSE(wasm_runtime_call_wasm(app.exec_env(),
                                            app.func("call_raw_ptr"), 1, argv));
        EXPECT_NE(nullptr, wasm_runtime_get_exception(app.inst()));
        wasm_runtime_clear_exception(app.inst());
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, export_global_inst)
{
    FOR_EACH_FIXTURE(app)
    {
        wasm_global_inst_t global = {};

        ASSERT_TRUE(
            wasm_runtime_get_export_global_inst(app.inst(), "g_mut", &global));
        EXPECT_EQ(WASM_I32, global.kind);
        EXPECT_TRUE(global.is_mutable);
        EXPECT_EQ(7, *(int32_t *)global.global_data);

        ASSERT_TRUE(wasm_runtime_get_export_global_inst(app.inst(), "g_const",
                                                        &global));
        EXPECT_EQ(WASM_I64, global.kind);
        EXPECT_FALSE(global.is_mutable);
        EXPECT_EQ(9, *(int64_t *)global.global_data);

        EXPECT_FALSE(
            wasm_runtime_get_export_global_inst(app.inst(), "mem", &global));
        EXPECT_FALSE(wasm_runtime_get_export_global_inst(app.inst(), "missing",
                                                         &global));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, export_table_inst)
{
    FOR_EACH_FIXTURE(app)
    {
        wasm_table_inst_t table = {};

        ASSERT_TRUE(
            wasm_runtime_get_export_table_inst(app.inst(), "tbl", &table));
        EXPECT_EQ(WASM_FUNCREF, table.elem_kind);
        EXPECT_EQ(4u, table.cur_size);
        /* A runtime built with SHRUNK_MEMORY lowers the maximum of a table
         * that is never grown to its size, so only the bound is checked. */
        EXPECT_GE(table.max_size, table.cur_size);
        EXPECT_LE(table.max_size, 8u);

        EXPECT_FALSE(
            wasm_runtime_get_export_table_inst(app.inst(), "missing", &table));
        EXPECT_FALSE(
            wasm_runtime_get_export_table_inst(app.inst(), "g_mut", &table));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, table_function_lookup)
{
    FOR_EACH_FIXTURE(app)
    {
        wasm_table_inst_t table = {};
        ASSERT_TRUE(
            wasm_runtime_get_export_table_inst(app.inst(), "tbl", &table));

        /* Slots 0 and 1 hold add and noop.  An index past the table end is
         * a bh_assert() in a debug build, so it is not probed here. */
        wasm_function_inst_t add =
            wasm_table_get_func_inst(app.inst(), &table, 0);
        ASSERT_NE(nullptr, add);
        EXPECT_NE(nullptr, wasm_table_get_func_inst(app.inst(), &table, 1));

        uint32_t argv[2] = { 3, 4 };
        ASSERT_TRUE(wasm_runtime_call_wasm(app.exec_env(), add, 2, argv));
        EXPECT_EQ(7u, argv[0]);

        /* The default-table variant resolves the same slots, and answers
         * NULL for an empty slot or an index past the end. */
        EXPECT_NE(nullptr, wasm_runtime_get_table_func_inst(app.inst(), 0));
        EXPECT_NE(nullptr, wasm_runtime_get_table_func_inst(app.inst(), 1));
        EXPECT_EQ(nullptr, wasm_runtime_get_table_func_inst(app.inst(), 2));
        EXPECT_EQ(nullptr, wasm_runtime_get_table_func_inst(app.inst(), 100));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, function_signature)
{
    FOR_EACH_FIXTURE(app)
    {
        wasm_function_inst_t mix = app.func("mix");
        ASSERT_NE(nullptr, mix);

        ASSERT_EQ(4u, wasm_func_get_param_count(mix, app.inst()));
        ASSERT_EQ(1u, wasm_func_get_result_count(mix, app.inst()));

        wasm_valkind_t params[4] = {};
        wasm_func_get_param_types(mix, app.inst(), params);
        EXPECT_EQ(WASM_I32, params[0]);
        EXPECT_EQ(WASM_I64, params[1]);
        EXPECT_EQ(WASM_F32, params[2]);
        EXPECT_EQ(WASM_F64, params[3]);

        wasm_valkind_t results[1] = {};
        wasm_func_get_result_types(mix, app.inst(), results);
        EXPECT_EQ(WASM_F64, results[0]);

        wasm_function_inst_t noop = app.func("noop");
        ASSERT_NE(nullptr, noop);
        EXPECT_EQ(0u, wasm_func_get_param_count(noop, app.inst()));
        EXPECT_EQ(0u, wasm_func_get_result_count(noop, app.inst()));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, call_wasm_variadic)
{
    FOR_EACH_FIXTURE(app)
    {
        wasm_val_t results[1] = {};

        ASSERT_TRUE(wasm_runtime_call_wasm_v(app.exec_env(), app.func("add"), 1,
                                             results, 2, 4u, 5u))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(WASM_I32, results[0].kind);
        EXPECT_EQ(9, results[0].of.i32);

        /* One argument of every value type; the float is passed promoted to
         * double, as a C variadic call does. */
        ASSERT_TRUE(wasm_runtime_call_wasm_v(app.exec_env(), app.func("mix"), 1,
                                             results, 4, 1u, (uint64_t)2, 3.0,
                                             4.5))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(WASM_F64, results[0].kind);
        EXPECT_EQ(4.5, results[0].of.f64);

        /* The number of arguments must match the signature. */
        EXPECT_FALSE(wasm_runtime_call_wasm_v(app.exec_env(), app.func("add"),
                                              1, results, 1, 4u));
        wasm_runtime_clear_exception(app.inst());
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, call_indirect)
{
    FOR_EACH_FIXTURE(app)
    {
        uint32_t argv[2] = { 2, 3 };

        ASSERT_TRUE(wasm_runtime_call_indirect(app.exec_env(), 0, 2, argv))
            << wasm_runtime_get_exception(app.inst());
        EXPECT_EQ(5u, argv[0]);

        /* Slot 1 is noop: no arguments, no result. */
        EXPECT_TRUE(wasm_runtime_call_indirect(app.exec_env(), 1, 0, argv));

        /* Slot 2 is empty and slot 4 is past the table end. */
        EXPECT_FALSE(wasm_runtime_call_indirect(app.exec_env(), 2, 0, argv));
        wasm_runtime_clear_exception(app.inst());
        EXPECT_FALSE(wasm_runtime_call_indirect(app.exec_env(), 4, 0, argv));
        wasm_runtime_clear_exception(app.inst());
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, terminate)
{
    FOR_EACH_FIXTURE(app)
    {
        EXPECT_EQ(nullptr, wasm_runtime_get_exception(app.inst()));

        wasm_runtime_terminate(app.inst());

        const char *exception = wasm_runtime_get_exception(app.inst());
        ASSERT_NE(nullptr, exception);
        EXPECT_NE(nullptr, strstr(exception, "terminated"));

        wasm_runtime_clear_exception(app.inst());
        EXPECT_EQ(nullptr, wasm_runtime_get_exception(app.inst()));
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, module_heap_helpers)
{
    FOR_EACH_FIXTURE(app)
    {
        void *native = nullptr;

        uint64_t offset = wasm_runtime_module_malloc_internal(
            app.inst(), app.exec_env(), 64, &native);
        ASSERT_NE(0u, offset);
        ASSERT_NE(nullptr, native);
        memset(native, 0x5a, 64);

        uint64_t grown = wasm_runtime_module_realloc_internal(
            app.inst(), app.exec_env(), offset, 256, &native);
        ASSERT_NE(0u, grown);
        ASSERT_NE(nullptr, native);
        /* The first 64 bytes survive the move. */
        EXPECT_EQ(0x5a, ((uint8_t *)native)[0]);
        EXPECT_EQ(0x5a, ((uint8_t *)native)[63]);

        wasm_runtime_module_free_internal(app.inst(), app.exec_env(), grown);
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, native_stack_overflow_check)
{
    FOR_EACH_FIXTURE(app)
    {
        EXPECT_TRUE(wasm_runtime_detect_native_stack_overflow_size(
            app.exec_env(), 1024));

        /* A request larger than any thread stack must either be refused with
         * an exception, or accepted when the platform cannot tell the stack
         * boundary. */
        bool accepted = wasm_runtime_detect_native_stack_overflow_size(
            app.exec_env(), 0xFFFFFF00u);
        if (!accepted) {
            const char *exception = wasm_runtime_get_exception(app.inst());
            ASSERT_NE(nullptr, exception);
            EXPECT_NE(nullptr, strstr(exception, "native stack overflow"));
            wasm_runtime_clear_exception(app.inst());
        }
    }
}

TEST_F(wasm_runtime_common_instance_api_suite, underlying_binary_freeable)
{
    FOR_EACH_FIXTURE(app)
    {
        bool freeable =
            wasm_runtime_is_underlying_binary_freeable(app.module());
#if WASM_ENABLE_FAST_INTERP == 0
        /* The classic interpreter runs the bytecode in place. */
        if (wasm_runtime_get_module_package_type(app.module())
            == Wasm_Module_Bytecode) {
            EXPECT_FALSE(freeable);
        }
#endif
        (void)freeable;
    }
}

#ifdef OS_ENABLE_HW_BOUND_CHECK
TEST_F(wasm_runtime_common_instance_api_suite,
       access_exce_check_guard_page_idle)
{
    FOR_EACH_FIXTURE(app)
    {
        /* No wasm function is running on this thread, so there is no guard
         * page to touch; the call must return without faulting. */
        wasm_runtime_access_exce_check_guard_page();
        EXPECT_EQ(nullptr, wasm_runtime_get_exception(app.inst()));
    }
}
#endif

TEST_F(wasm_runtime_common_instance_api_suite, instance_extra_common)
{
    FOR_EACH_FIXTURE(app)
    {
        EXPECT_NE(nullptr, GetModuleInstanceExtraCommon(
                               (struct WASMModuleInstance *)app.inst()));
    }
}

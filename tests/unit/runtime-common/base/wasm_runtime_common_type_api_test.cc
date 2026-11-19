/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * wasm_runtime_common.c APIs that read a module, not an instance: the
 * package type/version, the import/export descriptors and the type
 * accessors, plus the logging, version and instantiation-argument setters.
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "bh_read_file.h"
#include "version.h"
#include "wasm_export.h"
#include "wasm_runtime_common.h"

#include <string>
#include <vector>

namespace {

/* The fixture formats of this run mode: the wasm file always, the aot file
 * when the runtime is built with AOT. */
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

int
host_add_native(wasm_exec_env_t exec_env, int a, int b)
{
    return a + b;
}

void
host_raw_native(wasm_exec_env_t exec_env, uint64_t *args)
{
}

NativeSymbol host_symbols[] = { { "host_add", (void *)host_add_native, "(ii)i",
                                  nullptr } };
NativeSymbol host_raw_symbols[] = {
    { "host_raw", (void *)host_raw_native, "(ii)i", nullptr },
    { "host_raw_ptr", (void *)host_raw_native, "(*)i", nullptr }
};

} // namespace

/* Loads a fixture file; the buffer lives as long as the object. */
class LoadedModule
{
  public:
    explicit LoadedModule(const std::string &path)
    {
        buf_ = (uint8_t *)bh_read_file_to_buffer(path.c_str(), &size_);
        if (buf_) {
            module_ = wasm_runtime_load(buf_, size_, error_, sizeof(error_));
        }
    }

    ~LoadedModule()
    {
        if (module_) {
            wasm_runtime_unload(module_);
        }
        if (buf_) {
            wasm_runtime_free(buf_);
        }
    }

    wasm_module_t get() const { return module_; }
    const uint8_t *buf() const { return buf_; }
    uint32_t size() const { return size_; }
    const char *error() const { return error_; }

  private:
    uint8_t *buf_ = nullptr;
    uint32_t size_ = 0;
    wasm_module_t module_ = nullptr;
    char error_[128] = { 0 };
};

class wasm_runtime_common_type_api_suite : public testing::Test
{
  protected:
    void SetUp() override
    {
        /* The imports of api.wasm; they are only resolved, never called. */
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

TEST_F(wasm_runtime_common_type_api_suite, set_log_level)
{
    /* The level is only recorded; every level must be accepted. */
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_ERROR);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_DEBUG);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_VERBOSE);
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_WARNING);
}

TEST_F(wasm_runtime_common_type_api_suite, get_version)
{
    uint32_t major = 0, minor = 0, patch = 0;

    wasm_runtime_get_version(&major, &minor, &patch);
    EXPECT_EQ((uint32_t)WAMR_VERSION_MAJOR, major);
    EXPECT_EQ((uint32_t)WAMR_VERSION_MINOR, minor);
    EXPECT_EQ((uint32_t)WAMR_VERSION_PATCH, patch);
}

TEST_F(wasm_runtime_common_type_api_suite, thread_env_inited)
{
    EXPECT_TRUE(wasm_runtime_init_thread_env());
    EXPECT_TRUE(wasm_runtime_thread_env_inited());
    wasm_runtime_destroy_thread_env();
}

TEST_F(wasm_runtime_common_type_api_suite, current_package_version)
{
    EXPECT_EQ((uint32_t)WASM_CURRENT_VERSION,
              wasm_runtime_get_current_package_version(Wasm_Module_Bytecode));
    EXPECT_EQ((uint32_t)AOT_CURRENT_VERSION,
              wasm_runtime_get_current_package_version(Wasm_Module_AoT));
    EXPECT_EQ(0u,
              wasm_runtime_get_current_package_version(Package_Type_Unknown));
}

TEST_F(wasm_runtime_common_type_api_suite, package_type_and_version)
{
    for (const std::string &path : fixture_files()) {
        SCOPED_TRACE(path);
        LoadedModule loaded(path);
        ASSERT_NE(nullptr, loaded.get()) << loaded.error();

        bool is_aot = path.size() > 4 && path.substr(path.size() - 4) == ".aot";
        package_type_t expected_type =
            is_aot ? Wasm_Module_AoT : Wasm_Module_Bytecode;

        EXPECT_EQ(expected_type, wasm_runtime_get_file_package_type(
                                     loaded.buf(), loaded.size()));
        EXPECT_EQ(expected_type,
                  wasm_runtime_get_module_package_type(loaded.get()));

        uint32_t current =
            wasm_runtime_get_current_package_version(expected_type);
        EXPECT_EQ(current, wasm_runtime_get_file_package_version(
                               loaded.buf(), loaded.size()));
        EXPECT_EQ(current,
                  wasm_runtime_get_module_package_version(loaded.get()));
    }
}

TEST_F(wasm_runtime_common_type_api_suite, package_type_of_garbage)
{
    const uint8_t garbage[8] = { 'n', 'o', 't', 'w', 'a', 's', 'm', 0 };

    EXPECT_EQ(Package_Type_Unknown,
              wasm_runtime_get_file_package_type(garbage, sizeof(garbage)));
    EXPECT_EQ(Package_Type_Unknown,
              wasm_runtime_get_file_package_type(garbage, 0));
}

TEST_F(wasm_runtime_common_type_api_suite, resolve_symbols)
{
    for (const std::string &path : fixture_files()) {
        SCOPED_TRACE(path);
        LoadedModule loaded(path);
        ASSERT_NE(nullptr, loaded.get()) << loaded.error();
        EXPECT_TRUE(wasm_runtime_resolve_symbols(loaded.get()));
    }
}

TEST_F(wasm_runtime_common_type_api_suite, instantiation_args_setters)
{
    struct InstantiationArgs2 *args = nullptr;
    int custom_data = 0;

    ASSERT_TRUE(wasm_runtime_instantiation_args_create(&args));
    wasm_runtime_instantiation_args_set_default_stack_size(args, 16 * 1024);
    wasm_runtime_instantiation_args_set_host_managed_heap_size(args, 8 * 1024);
    wasm_runtime_instantiation_args_set_max_memory_pages(args, 2);
    wasm_runtime_instantiation_args_set_custom_data(args, &custom_data);

    for (const std::string &path : fixture_files()) {
        SCOPED_TRACE(path);
        LoadedModule loaded(path);
        ASSERT_NE(nullptr, loaded.get()) << loaded.error();

        char error[128] = { 0 };
        wasm_module_inst_t inst = wasm_runtime_instantiate_ex2(
            loaded.get(), args, error, sizeof(error));
        ASSERT_NE(nullptr, inst) << error;

        /* The custom data given through the arguments reaches the instance,
         * and the page cap (the fixture's memory is 1..3 pages) is accepted. */
        EXPECT_EQ(&custom_data, wasm_runtime_get_custom_data(inst));
        wasm_runtime_deinstantiate(inst);
    }

    wasm_runtime_instantiation_args_destroy(args);
}

TEST_F(wasm_runtime_common_type_api_suite, import_descriptors)
{
    for (const std::string &path : fixture_files()) {
        SCOPED_TRACE(path);
        LoadedModule loaded(path);
        ASSERT_NE(nullptr, loaded.get()) << loaded.error();

        ASSERT_EQ(3, wasm_runtime_get_import_count(loaded.get()));

        /* All three imports are registered, the raw ones included. */
        for (int32_t i = 0; i < 3; i++) {
            wasm_import_t each = {};
            wasm_runtime_get_import_type(loaded.get(), i, &each);
            EXPECT_TRUE(each.linked) << each.name;
        }

        wasm_import_t import = {};
        wasm_runtime_get_import_type(loaded.get(), 0, &import);
        EXPECT_STREQ("env", import.module_name);
        EXPECT_STREQ("host_add", import.name);
        EXPECT_EQ(WASM_IMPORT_EXPORT_KIND_FUNC, import.kind);
        EXPECT_TRUE(import.linked);

        ASSERT_NE(nullptr, import.u.func_type);
        EXPECT_EQ(2u, wasm_func_type_get_param_count(import.u.func_type));
        EXPECT_EQ(WASM_I32,
                  wasm_func_type_get_param_valkind(import.u.func_type, 0));
        EXPECT_EQ(WASM_I32,
                  wasm_func_type_get_param_valkind(import.u.func_type, 1));
        EXPECT_EQ(1u, wasm_func_type_get_result_count(import.u.func_type));
        EXPECT_EQ(WASM_I32,
                  wasm_func_type_get_result_valkind(import.u.func_type, 0));
    }
}

TEST_F(wasm_runtime_common_type_api_suite, import_not_linked_without_natives)
{
    /* The linked flag is decided when the module is loaded. */
    wasm_runtime_unregister_natives("env", host_symbols);
    LoadedModule loaded(get_test_binary_dir() + "/api.wasm");
    ASSERT_NE(nullptr, loaded.get()) << loaded.error();

    wasm_import_t import = {};
    wasm_runtime_get_import_type(loaded.get(), 0, &import);
    EXPECT_FALSE(import.linked);
}

TEST_F(wasm_runtime_common_type_api_suite, import_global_not_linked)
{
    EXPECT_FALSE(wasm_runtime_is_import_global_linked("env", "no_such_global"));
}

TEST_F(wasm_runtime_common_type_api_suite, export_descriptors)
{
    for (const std::string &path : fixture_files()) {
        SCOPED_TRACE(path);
        LoadedModule loaded(path);
        ASSERT_NE(nullptr, loaded.get()) << loaded.error();

        /* mem, tbl, g_mut, g_const, add, mix, noop, trap, call_host, grow,
         * call_raw, call_raw_ptr */
        ASSERT_EQ(12, wasm_runtime_get_export_count(loaded.get()));

        int seen_funcs = 0;
        for (int32_t i = 0; i < 12; i++) {
            wasm_export_t export_type = {};
            wasm_runtime_get_export_type(loaded.get(), i, &export_type);
            std::string name = export_type.name;

            switch (export_type.kind) {
                case WASM_IMPORT_EXPORT_KIND_FUNC:
                    seen_funcs++;
                    if (name == "mix") {
                        wasm_func_type_t type = export_type.u.func_type;
                        ASSERT_NE(nullptr, type);
                        ASSERT_EQ(4u, wasm_func_type_get_param_count(type));
                        EXPECT_EQ(WASM_I32,
                                  wasm_func_type_get_param_valkind(type, 0));
                        EXPECT_EQ(WASM_I64,
                                  wasm_func_type_get_param_valkind(type, 1));
                        EXPECT_EQ(WASM_F32,
                                  wasm_func_type_get_param_valkind(type, 2));
                        EXPECT_EQ(WASM_F64,
                                  wasm_func_type_get_param_valkind(type, 3));
                        ASSERT_EQ(1u, wasm_func_type_get_result_count(type));
                        EXPECT_EQ(WASM_F64,
                                  wasm_func_type_get_result_valkind(type, 0));
                    }
                    else if (name == "noop") {
                        EXPECT_EQ(0u, wasm_func_type_get_param_count(
                                          export_type.u.func_type));
                        EXPECT_EQ(0u, wasm_func_type_get_result_count(
                                          export_type.u.func_type));
                    }
                    break;
                case WASM_IMPORT_EXPORT_KIND_MEMORY:
                    EXPECT_EQ("mem", name);
                    EXPECT_FALSE(
                        wasm_memory_type_get_shared(export_type.u.memory_type));
                    EXPECT_EQ(1u, wasm_memory_type_get_init_page_count(
                                      export_type.u.memory_type));
                    EXPECT_EQ(3u, wasm_memory_type_get_max_page_count(
                                      export_type.u.memory_type));
                    break;
                case WASM_IMPORT_EXPORT_KIND_TABLE:
                    EXPECT_EQ("tbl", name);
                    EXPECT_EQ(WASM_FUNCREF, wasm_table_type_get_elem_kind(
                                                export_type.u.table_type));
                    EXPECT_FALSE(
                        wasm_table_type_get_shared(export_type.u.table_type));
                    EXPECT_EQ(4u, wasm_table_type_get_init_size(
                                      export_type.u.table_type));
                    EXPECT_EQ(8u, wasm_table_type_get_max_size(
                                      export_type.u.table_type));
                    break;
                case WASM_IMPORT_EXPORT_KIND_GLOBAL:
                    if (name == "g_mut") {
                        EXPECT_EQ(WASM_I32, wasm_global_type_get_valkind(
                                                export_type.u.global_type));
                        EXPECT_TRUE(wasm_global_type_get_mutable(
                            export_type.u.global_type));
                    }
                    else {
                        EXPECT_EQ("g_const", name);
                        EXPECT_EQ(WASM_I64, wasm_global_type_get_valkind(
                                                export_type.u.global_type));
                        EXPECT_FALSE(wasm_global_type_get_mutable(
                            export_type.u.global_type));
                    }
                    break;
                default:
                    ADD_FAILURE() << "unexpected export kind for " << name;
            }
        }
        EXPECT_EQ(8, seen_funcs);
    }
}

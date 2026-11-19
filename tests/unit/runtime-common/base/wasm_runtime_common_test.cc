/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_helper.h"
#include "gtest/gtest.h"

#include "platform_common.h"
#include "wasm_runtime_common.h"
#include "bh_read_file.h"
#include "wasm_runtime.h"
#include "bh_platform.h"
#include "wasm_export.h"
#if WASM_ENABLE_AOT != 0
#include "aot_runtime.h"
#endif

using namespace std;

extern "C" {
uint32
wasm_runtime_module_realloc(WASMModuleInstanceCommon *module_inst, uint32 ptr,
                            uint32 size, void **p_native_addr);
bool
wasm_runtime_create_exec_env_and_call_wasm(
    WASMModuleInstanceCommon *module_inst, WASMFunctionInstanceCommon *function,
    uint32 argc, uint32 argv[]);
}

static std::string CWD;
static std::string MAIN_WASM = "/main.wasm";
#if WASM_ENABLE_AOT != 0
static std::string MAIN_AOT = "/main.aot";
#endif
static char *WASM_FILE_1;
#if WASM_ENABLE_AOT != 0
static char *AOT_FILE_1;
#endif

class wasm_runtime_common_test_suite : public testing::Test
{
  protected:
    // You should make the members protected s.t. they can be
    // accessed from sub-classes.

    // virtual void SetUp() will be called before each test is run.  You
    // should define it if you need to initialize the variables.
    // Otherwise, this can be skipped.
    virtual void SetUp() {}

    static void SetUpTestCase()
    {
        CWD = get_test_binary_dir();
        WASM_FILE_1 = strdup((CWD + MAIN_WASM).c_str());
#if WASM_ENABLE_AOT != 0
        AOT_FILE_1 = strdup((CWD + MAIN_AOT).c_str());
#endif
    }

    // virtual void TearDown() will be called after each test is run.
    // You should define it if there is cleanup work to do.  Otherwise,
    // you don't have to provide it.
    //
    virtual void TearDown() {}

    static void TearDownTestCase()
    {
        free(WASM_FILE_1);
#if WASM_ENABLE_AOT != 0
        free(AOT_FILE_1);
#endif
    }

    WAMRRuntimeRAII<512 * 1024> runtime;
};

TEST_F(wasm_runtime_common_test_suite, wasm_runtime_destroy)
{
    wasm_runtime_init();
    wasm_runtime_destroy();
}

/* TODO: add thread safety test. */

TEST_F(wasm_runtime_common_test_suite, wasm_runtime_is_built_in_module)
{
    EXPECT_EQ(true, wasm_runtime_is_built_in_module("env"));
    EXPECT_EQ(true, wasm_runtime_is_built_in_module("wasi_unstable"));
    EXPECT_EQ(true, wasm_runtime_is_built_in_module("wasi_snapshot_preview1"));
    EXPECT_EQ(true, wasm_runtime_is_built_in_module(""));
    EXPECT_EQ(false, wasm_runtime_is_built_in_module("test"));
}

TEST_F(wasm_runtime_common_test_suite, wasm_runtime_read_v128)
{
    unsigned char buf[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                              '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
    unsigned char ret1[8] = { 0 };
    unsigned char ret2[8] = { 0 };

    wasm_runtime_read_v128((const uint8 *)buf, (uint64 *)ret1, (uint64 *)ret2);
    EXPECT_EQ(0, strncmp("01234567", (const char *)ret1, 8));
    EXPECT_EQ(0, strncmp("89ABCDEF", (const char *)ret2, 8));
}

TEST_F(wasm_runtime_common_test_suite,
       wasm_runtime_show_app_heap_corrupted_prompt)
{
    wasm_runtime_show_app_heap_corrupted_prompt();
}

#if WASM_ENABLE_AOT != 0
TEST_F(wasm_runtime_common_test_suite, wasm_runtime_is_xip_file)
{
    // WASM file.
    const char *wasm_file = WASM_FILE_1;
    unsigned int wasm_file_size = 0;
    unsigned char *wasm_file_buf = nullptr;

    wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(wasm_file, &wasm_file_size);
    EXPECT_NE(wasm_file_buf, nullptr);
    EXPECT_EQ(false, wasm_runtime_is_xip_file(wasm_file_buf, wasm_file_size));

    // AoT file.
    const char *aot_file = AOT_FILE_1;
    unsigned int aot_file_size = 0;
    unsigned char *aot_file_buf = nullptr;

    aot_file_buf =
        (unsigned char *)bh_read_file_to_buffer(aot_file, &aot_file_size);
    EXPECT_NE(aot_file_buf, nullptr);
    EXPECT_EQ(false, wasm_runtime_is_xip_file(aot_file_buf, aot_file_size));
}
#endif

TEST_F(wasm_runtime_common_test_suite, get_package_type)
{
    const char *wasm_file = WASM_FILE_1;
    unsigned int wasm_file_size = 0;
    unsigned char *wasm_file_buf = nullptr;

    // WASM file.
    wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(wasm_file, &wasm_file_size);
    EXPECT_NE(wasm_file_buf, nullptr);
    EXPECT_EQ(Wasm_Module_Bytecode,
              get_package_type(wasm_file_buf, wasm_file_size));

    // WASM file. Abnormally.
    wasm_file_buf[3] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[2] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[1] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[0] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));

    EXPECT_EQ(Package_Type_Unknown, get_package_type(wasm_file_buf, 0));
    EXPECT_EQ(Package_Type_Unknown, get_package_type(nullptr, 0));

#if WASM_ENABLE_AOT != 0
    // AoT file.
    const char *wasm_file_aot = AOT_FILE_1;
    wasm_file_buf =
        (unsigned char *)bh_read_file_to_buffer(wasm_file_aot, &wasm_file_size);
    EXPECT_NE(wasm_file_buf, nullptr);
    EXPECT_EQ(Wasm_Module_AoT, get_package_type(wasm_file_buf, wasm_file_size));

    // AoT file. Abnormally.
    wasm_file_buf[3] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[2] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[1] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));
    wasm_file_buf[0] = -1;
    EXPECT_EQ(Package_Type_Unknown,
              get_package_type(wasm_file_buf, wasm_file_size));

    EXPECT_EQ(Package_Type_Unknown, get_package_type(wasm_file_buf, 0));
    EXPECT_EQ(Package_Type_Unknown, get_package_type(nullptr, 0));
#endif
}

/* The module exports malloc/free, so the app heap is disabled and
   wasm_runtime_module_malloc()/free() call the guest functions. The guest
   malloc returns its argument as the offset, and the guest free stores its
   argument at address 0. */
static const uint64 MODULE_MALLOC_MEM_SIZE = 65536;

static wasm_module_inst_t
instantiate_module_malloc(const std::string &file, wasm_module_t *p_module,
                          unsigned char **p_buf)
{
    char error_buf[128] = { 0 };
    uint32 buf_size = 0;

    *p_buf = (unsigned char *)bh_read_file_to_buffer(file.c_str(), &buf_size);
    if (!*p_buf)
        return nullptr;

    *p_module =
        wasm_runtime_load(*p_buf, buf_size, error_buf, sizeof(error_buf));
    if (!*p_module)
        return nullptr;

    return wasm_runtime_instantiate(*p_module, 8192, 0, error_buf,
                                    sizeof(error_buf));
}

static void
check_module_malloc_guest_offset(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;
    void *native_addr = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    /* In bounds: the offset is returned as is, including the last byte. */
    EXPECT_EQ((uint64)16, wasm_runtime_module_malloc(module_inst, 16,
                                                     &native_addr));
    EXPECT_EQ(wasm_runtime_addr_app_to_native(module_inst, 16), native_addr);
    EXPECT_EQ(nullptr, wasm_runtime_get_exception(module_inst));

    EXPECT_EQ((uint64)MODULE_MALLOC_MEM_SIZE / 2,
              wasm_runtime_module_malloc(module_inst,
                                         MODULE_MALLOC_MEM_SIZE / 2,
                                         &native_addr));
    EXPECT_EQ(wasm_runtime_addr_app_to_native(module_inst,
                                              MODULE_MALLOC_MEM_SIZE / 2),
              native_addr);
    EXPECT_EQ(nullptr, wasm_runtime_get_exception(module_inst));

    /* Offset at or past the end of linear memory, or offset + size past the
       end: the allocation fails and no native address is returned. */
    const uint64 bad_sizes[] = { MODULE_MALLOC_MEM_SIZE,
                                 MODULE_MALLOC_MEM_SIZE - 1, 0x80000000ULL };
    for (uint64 size : bad_sizes) {
        native_addr = nullptr;
        EXPECT_EQ((uint64)0, wasm_runtime_module_malloc(module_inst, size,
                                                        &native_addr))
            << "size " << size;
        EXPECT_EQ(nullptr, native_addr) << "size " << size;
        EXPECT_STREQ("Exception: allocate memory failed",
                     wasm_runtime_get_exception(module_inst));
        wasm_runtime_clear_exception(module_inst);
    }

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

static void
check_module_free_guest_offset(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    uint32 *guest_word0 =
        (uint32 *)wasm_runtime_addr_app_to_native(module_inst, 0);
    ASSERT_NE(nullptr, guest_word0);
    EXPECT_EQ(0u, *guest_word0);

    /* Out of range: the guest free must not be called. */
    wasm_runtime_module_free(module_inst, 0x80000000ULL);
    EXPECT_EQ(0u, *guest_word0);

    /* One past the end of linear memory: the guest free must not be called. */
    wasm_runtime_module_free(module_inst, MODULE_MALLOC_MEM_SIZE);
    EXPECT_EQ(0u, *guest_word0);

    /* In range: the guest free is called with the offset. */
    wasm_runtime_module_free(module_inst, 16);
    EXPECT_EQ(16u, *guest_word0);

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

TEST_F(wasm_runtime_common_test_suite, module_malloc_guest_offset_wasm)
{
    check_module_malloc_guest_offset(CWD + "/module_malloc.wasm");
}

#if WASM_ENABLE_AOT != 0
TEST_F(wasm_runtime_common_test_suite, module_malloc_guest_offset_aot)
{
    check_module_malloc_guest_offset(CWD + "/module_malloc.aot");
}
#endif

TEST_F(wasm_runtime_common_test_suite, module_free_guest_offset_wasm)
{
    check_module_free_guest_offset(CWD + "/module_malloc.wasm");
}

#if WASM_ENABLE_AOT != 0
TEST_F(wasm_runtime_common_test_suite, module_free_guest_offset_aot)
{
    check_module_free_guest_offset(CWD + "/module_malloc.aot");
}
#endif

/* module_malloc_offset.wasm reads the offset from guest address 0, and grows
   the memory inside malloc when the word at guest address 4 is set.  The
   offset is therefore set by the case, not derived from the size. */
static void
set_guest_word(wasm_module_inst_t module_inst, uint64 app_addr, uint32 value)
{
    uint32 *word =
        (uint32 *)wasm_runtime_addr_app_to_native(module_inst, app_addr);
    ASSERT_NE(nullptr, word);
    *word = value;
}

static void
check_offset_last_byte_in_bounds(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;
    void *native_addr = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    /* Offset 65535 with size 1 is the last byte of the memory: it succeeds
       and the byte is usable. */
    set_guest_word(module_inst, 0, 65535);
    EXPECT_EQ((uint64)65535,
              wasm_runtime_module_malloc(module_inst, 1, &native_addr));
    EXPECT_EQ(wasm_runtime_addr_app_to_native(module_inst, 65535), native_addr);
    EXPECT_EQ(nullptr, wasm_runtime_get_exception(module_inst));

    *(uint8 *)native_addr = 0x5a;
    EXPECT_EQ(0x5a, *(uint8 *)wasm_runtime_addr_app_to_native(module_inst,
                                                               65535));

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

static void
check_offset_after_grow(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;
    void *native_addr = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    /* The offset 65536 + 16 is past the end of the one-page memory before the
       call.  malloc grows the memory first, so the allocation succeeds only
       against the refreshed memory. */
    set_guest_word(module_inst, 4, 1);
    set_guest_word(module_inst, 0, 65536 + 16);
    EXPECT_EQ((uint64)65536 + 16,
              wasm_runtime_module_malloc(module_inst, 16, &native_addr));
    EXPECT_EQ(wasm_runtime_addr_app_to_native(module_inst, 65536 + 16),
              native_addr);
    EXPECT_EQ(nullptr, wasm_runtime_get_exception(module_inst));

    *(uint8 *)native_addr = 0x5a;
    EXPECT_EQ(0x5a, *(uint8 *)wasm_runtime_addr_app_to_native(module_inst,
                                                               65536 + 16));

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

static void
check_offset_size_crosses_end(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;
    void *native_addr = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    /* Offset 65535 is in range, but offset + size = 65537 is past the end. */
    set_guest_word(module_inst, 0, 65535);
    EXPECT_EQ((uint64)0,
              wasm_runtime_module_malloc(module_inst, 2, &native_addr));
    EXPECT_EQ(nullptr, native_addr);
    EXPECT_STREQ("Exception: allocate memory failed",
                 wasm_runtime_get_exception(module_inst));
    wasm_runtime_clear_exception(module_inst);

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

static void
check_offset_zero(const std::string &file)
{
    wasm_module_t module = nullptr;
    unsigned char *buf = nullptr;
    void *native_addr = nullptr;

    wasm_module_inst_t module_inst =
        instantiate_module_malloc(file, &module, &buf);
    ASSERT_NE(nullptr, module_inst);

    /* A zero offset means the guest malloc failed. */
    set_guest_word(module_inst, 0, 0);
    EXPECT_EQ((uint64)0,
              wasm_runtime_module_malloc(module_inst, 16, &native_addr));
    EXPECT_EQ(nullptr, native_addr);
    EXPECT_STREQ("Exception: allocate memory failed",
                 wasm_runtime_get_exception(module_inst));
    wasm_runtime_clear_exception(module_inst);

    wasm_runtime_deinstantiate(module_inst);
    wasm_runtime_unload(module);
    if (buf)
        wasm_runtime_free(buf);
}

static void
check_module_malloc_offset_independent(const std::string &file)
{
    check_offset_last_byte_in_bounds(file);
    check_offset_after_grow(file);
    check_offset_size_crosses_end(file);
    check_offset_zero(file);
}

TEST_F(wasm_runtime_common_test_suite, module_malloc_offset_independent_wasm)
{
    check_module_malloc_offset_independent(CWD + "/module_malloc_offset.wasm");
}

#if WASM_ENABLE_AOT != 0
TEST_F(wasm_runtime_common_test_suite, module_malloc_offset_independent_aot)
{
    check_module_malloc_offset_independent(CWD + "/module_malloc_offset.aot");
}
#endif

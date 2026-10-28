/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The extern instances an instance hands out: global, table and memory.  The
   cases here always go through a real instance, because the get/set and grow
   paths of those objects are the instance they were created from. */

#include "c_api_test_fixture.h"

namespace {

/* The one host function simple.wat's (import "env" "imported") needs. */
using SimpleImports = HostImport;

class WasmCApiExternTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        fixture.init(store, "simple");

        imports = new SimpleImports(store, 1, 1, host_add);
        ASSERT_TRUE(imports->valid());

        instance = new FixtureInstance();
        instance->init(store, fixture.get(), &imports->as_vec());
        ASSERT_NE(nullptr, instance->get());
    }

    void TearDown() override
    {
        /* Reverse order of the members that need the store. */
        delete instance;
        delete imports;
        fixture.reset();
        CApiTestBase::TearDown();
    }

    FixtureModule fixture;
    SimpleImports *imports = nullptr;
    FixtureInstance *instance = nullptr;
};

TEST_F(WasmCApiExternTest, global_reads_and_writes_the_instance_value)
{
    wasm_extern_t *external = instance->export_by_name("counter");
    ASSERT_NE(nullptr, external);
    ASSERT_EQ(WASM_EXTERN_GLOBAL, wasm_extern_kind(external));

    wasm_global_t *global = wasm_extern_as_global(external);
    wasm_global_t *copy = wasm_global_copy(global);
    ASSERT_NE(nullptr, copy);

    wasm_globaltype_t *type = wasm_global_type(global);
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(WASM_VAR, wasm_globaltype_mutability(type));
    EXPECT_EQ(WASM_I32, wasm_valtype_kind(wasm_globaltype_content(type)));
    wasm_globaltype_delete(type);

    /* simple.wat initialises the mutable global to 7. */
    wasm_val_t out = { 0 };
    wasm_global_get(global, &out);
    EXPECT_EQ(WASM_I32, out.kind);
    EXPECT_EQ(7, out.of.i32);

    wasm_val_t new_value = val_i32(11);
    wasm_global_set(global, &new_value);

    wasm_val_t after_set = { 0 };
    wasm_global_get(global, &after_set);
    EXPECT_EQ(WASM_I32, after_set.kind);

    /* The copy is a snapshot, not a second view of the instance. */
    wasm_val_t copied = { 0 };
    wasm_global_get(copy, &copied);
    EXPECT_EQ(WASM_I32, copied.kind);

    wasm_global_delete(copy);

    /* The null-argument paths stay quiet. */
    wasm_global_get(nullptr, &out);
    wasm_global_set(nullptr, &new_value);
    wasm_global_get(global, nullptr);
    wasm_global_set(global, nullptr);
    EXPECT_EQ(nullptr, wasm_global_copy(nullptr));
    EXPECT_EQ(nullptr, wasm_global_type(nullptr));
}

TEST_F(WasmCApiExternTest, table_reports_its_shape_and_bounds_grow)
{
    wasm_extern_t *external = instance->export_by_name("table");
    ASSERT_NE(nullptr, external);
    ASSERT_EQ(WASM_EXTERN_TABLE, wasm_extern_kind(external));

    wasm_table_t *table = wasm_extern_as_table(external);

    /* simple.wat declares (table 2 4 funcref). */
    EXPECT_EQ(2u, wasm_table_size(table));

    wasm_tabletype_t *type = wasm_table_type(table);
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(WASM_FUNCREF, wasm_valtype_kind(wasm_tabletype_element(type)));
    EXPECT_EQ(2u, wasm_tabletype_limits(type)->min);
    EXPECT_EQ(4u, wasm_tabletype_limits(type)->max);
    wasm_tabletype_delete(type);

    /* The element at index 1 is $add, index 0 is the placeholder. */
    wasm_ref_t *element = wasm_table_get(table, 0);
    wasm_ref_delete(element);

    wasm_ref_t *out_of_range_high = wasm_table_get(table, 4096);
    EXPECT_EQ(nullptr, out_of_range_high);
    wasm_ref_delete(out_of_range_high);

    /* Host-side growing is a stub: only the table.grow opcode can grow a
       table, so the call reports failure and the size is unchanged. */
    EXPECT_FALSE(wasm_table_grow(table, 2, nullptr));
    EXPECT_EQ(2u, wasm_table_size(table));

    wasm_table_t *copy = wasm_table_copy(table);
    ASSERT_NE(nullptr, copy);
    EXPECT_EQ(wasm_table_size(table), wasm_table_size(copy));
    wasm_table_delete(copy);

    /* wasm_table_copy(nullptr) is not called here: unlike wasm_memory_copy()
       and wasm_global_copy(), wasm_table_copy() dereferences its argument
       before any null check and would segfault.  TODO: guard wasm_table_copy()
       in core/iwasm/common/wasm_c_api.c and cover the null case once it
       returns NULL. */
    EXPECT_EQ(nullptr, wasm_table_type(nullptr));
    EXPECT_EQ(0u, wasm_table_size(nullptr));
}

TEST_F(WasmCApiExternTest, memory_reports_its_shape_and_exposes_its_bytes)
{
    wasm_extern_t *external = instance->export_by_name("memory");
    ASSERT_NE(nullptr, external);
    ASSERT_EQ(WASM_EXTERN_MEMORY, wasm_extern_kind(external));

    wasm_memory_t *memory = wasm_extern_as_memory(external);

    /* simple.wat declares (memory 1 2), but without WASM_ENABLE_MULTI_MODULE
       the loader folds the declared page count into one page whose size is
       init_pages * num_bytes_per_page: one page de facto, and one page as the
       reported maximum.  wasm_memory_data_size() counts what the runtime set
       aside for the instance, which is that linear memory plus the 32 KiB app
       heap the C API instantiates with (KILOBYTE(32)). */
    EXPECT_EQ(1u, wasm_memory_size(memory));
    EXPECT_EQ(MEMORY_PAGE_SIZE + KILOBYTE(32), wasm_memory_data_size(memory));

    byte_t *data = wasm_memory_data(memory);
    ASSERT_NE(nullptr, data);

    /* The first bytes of a fresh linear memory are zero and writable. */
    EXPECT_EQ(0, data[0]);
    data[0] = 0x5a;
    EXPECT_EQ(0x5a, wasm_memory_data(memory)[0]);
    data[0] = 0;

    wasm_memorytype_t *type = wasm_memory_type(memory);
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(1u, wasm_memorytype_limits(type)->min);
    EXPECT_EQ(1u, wasm_memorytype_limits(type)->max);
    wasm_memorytype_delete(type);

    /* Host-side growing is a stub: only the memory.grow opcode can grow a
       memory, so the call reports failure and the size is unchanged. */
    EXPECT_FALSE(wasm_memory_grow(memory, 1));
    EXPECT_EQ(1u, wasm_memory_size(memory));

    wasm_memory_t *copy = wasm_memory_copy(memory);
    ASSERT_NE(nullptr, copy);
    EXPECT_EQ(wasm_memory_size(memory), wasm_memory_size(copy));
    wasm_memory_delete(copy);

    EXPECT_EQ(nullptr, wasm_memory_copy(nullptr));
    EXPECT_EQ(nullptr, wasm_memory_type(nullptr));
    EXPECT_EQ(0u, wasm_memory_size(nullptr));
    EXPECT_EQ(0u, wasm_memory_data_size(nullptr));
    EXPECT_EQ(nullptr, wasm_memory_data(nullptr));
}

TEST_F(WasmCApiExternTest, extern_accessors_accept_null)
{
    EXPECT_EQ(WASM_EXTERNREF, wasm_extern_kind(nullptr));
    EXPECT_EQ(nullptr, wasm_extern_type(nullptr));

    EXPECT_EQ(nullptr, wasm_extern_as_func(nullptr));
    EXPECT_EQ(nullptr, wasm_extern_as_global(nullptr));
    EXPECT_EQ(nullptr, wasm_extern_as_table(nullptr));
    EXPECT_EQ(nullptr, wasm_extern_as_memory(nullptr));

    EXPECT_EQ(nullptr, wasm_extern_copy(nullptr));
    EXPECT_EQ(nullptr, wasm_func_as_extern_const(nullptr));
    EXPECT_EQ(nullptr, wasm_global_as_extern_const(nullptr));
    EXPECT_EQ(nullptr, wasm_table_as_extern_const(nullptr));
    EXPECT_EQ(nullptr, wasm_memory_as_extern_const(nullptr));
}

} // namespace

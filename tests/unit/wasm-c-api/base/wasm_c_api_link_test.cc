/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Linking and identity: a global import linked at instantiation,
   wasm_global_same(), and writing a function reference into a table with
   wasm_table_set(). */

#include "c_api_test_fixture.h"

namespace {

class WasmCApiLinkTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        simple.init(store, "simple");
    }

    void TearDown() override
    {
        simple_instance.reset();
        simple.reset();
        CApiTestBase::TearDown();
    }

    /* Instantiate simple.wat, whose one import is a host function. */
    void instantiate_simple()
    {
        host.reset(new HostImport(store, 1, 1, host_double));
        ASSERT_TRUE(host->valid());
        ASSERT_NE(nullptr, simple.get());
        simple_instance.init(store, simple.get(), &host->as_vec());
        ASSERT_NE(nullptr, simple_instance.get());
    }

    FixtureModule simple;
    FixtureInstance simple_instance;
    std::unique_ptr<HostImport> host;
};

/* A global of `kind` holding `init`, in an extern vector that owns it. */
class GlobalImport
{
  public:
    GlobalImport(wasm_store_t *store, wasm_valkind_t kind, wasm_val_t init)
    {
        wasm_globaltype_t *type =
            wasm_globaltype_new(wasm_valtype_new(kind), WASM_CONST);
        if (!type)
            return;
        global_ = wasm_global_new(store, type, &init);
        wasm_globaltype_delete(type);
        if (global_) {
            list_[0] = wasm_global_as_extern(global_);
            wasm_extern_vec_new(&imports_, 1, list_);
        }
    }

    ~GlobalImport() { wasm_extern_vec_delete(&imports_); }

    GlobalImport(const GlobalImport &) = delete;
    GlobalImport &operator=(const GlobalImport &) = delete;

    const wasm_extern_vec_t &as_vec() const { return imports_; }
    bool valid() const { return global_ != nullptr; }

  private:
    wasm_global_t *global_ = nullptr;
    wasm_extern_t *list_[1] = { nullptr };
    wasm_extern_vec_t imports_ = { 0, nullptr, 0, 0, nullptr };
};

TEST_F(WasmCApiLinkTest, global_import_is_linked_by_value)
{
    FixtureModule module;
    module.init(store, "global_import");
    ASSERT_NE(nullptr, module.get());

    GlobalImport import(store, WASM_I32, val_i32(42));
    ASSERT_TRUE(import.valid());

    FixtureInstance instance;
    instance.init(store, module.get(), &import.as_vec());
    ASSERT_NE(nullptr, instance.get());

    wasm_func_t *read = wasm_extern_as_func(instance.export_by_name("read"));
    ASSERT_NE(nullptr, read);

    wasm_val_t result = { 0 };
    wasm_val_vec_t args = { 0, nullptr, 0, 0, nullptr };
    wasm_val_vec_t results = { 1, &result, 1, sizeof(wasm_val_t), nullptr };
    EXPECT_EQ(nullptr, wasm_func_call(read, &args, &results));
    EXPECT_EQ(WASM_I32, result.kind);
    EXPECT_EQ(42, result.of.i32);
}

TEST_F(WasmCApiLinkTest, global_import_of_another_type_is_refused)
{
    FixtureModule module;
    module.init(store, "global_import");
    ASSERT_NE(nullptr, module.get());

    /* The module imports an i32 global; an i64 one does not match. */
    GlobalImport import(store, WASM_I64, val_i64(42));
    ASSERT_TRUE(import.valid());

    FixtureInstance instance;
    instance.init(store, module.get(), &import.as_vec());
    EXPECT_EQ(nullptr, instance.get());
}

TEST_F(WasmCApiLinkTest, global_same_compares_identity)
{
    instantiate_simple();
    wasm_global_t *counter =
        wasm_extern_as_global(simple_instance.export_by_name("counter"));
    ASSERT_NE(nullptr, counter);

    EXPECT_TRUE(wasm_global_same(counter, counter));
    EXPECT_TRUE(wasm_global_same(nullptr, nullptr));
    EXPECT_FALSE(wasm_global_same(counter, nullptr));
    EXPECT_FALSE(wasm_global_same(nullptr, counter));
}

TEST_F(WasmCApiLinkTest, table_set_stores_a_function_reference)
{
    instantiate_simple();
    wasm_table_t *table =
        wasm_extern_as_table(simple_instance.export_by_name("table"));
    ASSERT_NE(nullptr, table);

    /* Slots 0 and 1 hold $placeholder and $add. */
    wasm_ref_t *add_ref = wasm_table_get(table, 1);
    ASSERT_NE(nullptr, add_ref);

    /* The table takes the reference it is given. */
    wasm_ref_t *add_ref_to_store = wasm_ref_copy(add_ref);
    ASSERT_NE(nullptr, add_ref_to_store);
    EXPECT_TRUE(wasm_table_set(table, 0, add_ref_to_store));

    wasm_ref_t *slot0 = wasm_table_get(table, 0);
    ASSERT_NE(nullptr, slot0);
    EXPECT_TRUE(wasm_ref_same(slot0, add_ref));
    wasm_ref_delete(slot0);
    wasm_ref_delete(add_ref);

    /* A null reference empties the slot. */
    EXPECT_TRUE(wasm_table_set(table, 0, nullptr));
    EXPECT_EQ(nullptr, wasm_table_get(table, 0));
}

TEST_F(WasmCApiLinkTest, table_set_refuses_what_does_not_fit)
{
    instantiate_simple();
    wasm_table_t *table =
        wasm_extern_as_table(simple_instance.export_by_name("table"));
    ASSERT_NE(nullptr, table);

    EXPECT_FALSE(wasm_table_set(nullptr, 0, nullptr));

    /* The table has 2 slots. */
    EXPECT_FALSE(wasm_table_set(table, 2, nullptr));

    /* A reference of another kind is not accepted by a funcref table, and
       the caller keeps ownership of it. */
    wasm_foreign_t *foreign = wasm_foreign_new(store);
    ASSERT_NE(nullptr, foreign);
    wasm_ref_t *foreign_ref = wasm_foreign_as_ref(foreign);
    ASSERT_NE(nullptr, foreign_ref);
    EXPECT_FALSE(wasm_table_set(table, 0, foreign_ref));
    wasm_ref_delete(foreign_ref);
    wasm_foreign_delete(foreign);
}

} // namespace

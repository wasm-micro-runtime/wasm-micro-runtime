/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Module lifecycle and enumeration: wasm_module_new/validate/imports/exports,
   the module name, sharing and serialization.  The cases here only need a
   module, never an instance. */

#include "c_api_test_fixture.h"

namespace {

class WasmCApiModuleTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        fixture.init(store, "simple");
    }

    void TearDown() override
    {
        /* The module must go before the engine; see CApiTestBase. */
        fixture.reset();
        CApiTestBase::TearDown();
    }

    FixtureModule fixture;
};

class WasmCApiEmptyModuleTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        fixture.init(store, "empty");
    }

    void TearDown() override
    {
        fixture.reset();
        CApiTestBase::TearDown();
    }

    FixtureModule fixture;
};

TEST_F(WasmCApiModuleTest, new_module_from_fixture_bytes)
{
    ASSERT_FALSE(fixture.binary().size == 0);
    ASSERT_NE(nullptr, fixture.get());
    EXPECT_TRUE(wasm_module_is_underlying_binary_freeable(fixture.get()));
}

TEST_F(WasmCApiModuleTest, delete_null_module)
{
    wasm_module_delete(nullptr);
}

TEST_F(WasmCApiModuleTest, new_module_rejects_null_and_empty_binary)
{
    wasm_byte_vec_t empty = { 0 };

    EXPECT_EQ(nullptr, wasm_module_new(store, nullptr));
    EXPECT_EQ(nullptr, wasm_module_new(store, &empty));
    wasm_byte_vec_delete(&empty);
}

TEST_F(WasmCApiModuleTest, new_module_rejects_malformed_binary)
{
    /* Right length, wrong content: the magic and the version are what the
       loader checks first. */
    const char not_wasm[] = "this is not a wasm module, not at all";
    wasm_byte_vec_t binary = { 0 };

    wasm_byte_vec_new(&binary, sizeof(not_wasm) - 1, not_wasm);
    EXPECT_EQ(nullptr, wasm_module_new(store, &binary));
    wasm_byte_vec_delete(&binary);
}

TEST_F(WasmCApiModuleTest, validate_accepts_fixture_and_rejects_junk)
{
    wasm_byte_vec_t truncated = { 0 };
    wasm_byte_vec_t not_wasm = { 0 };

    EXPECT_TRUE(wasm_module_validate(store, &fixture.binary()));

    /* A truncated copy of a valid module must not validate. */
    wasm_byte_vec_new(&truncated, fixture.binary().size / 2,
                      const_cast<wasm_byte_t *>(fixture.binary().data));
    EXPECT_FALSE(wasm_module_validate(store, &truncated));

    wasm_byte_vec_new(&not_wasm, 4, "nope");
    EXPECT_FALSE(wasm_module_validate(store, &not_wasm));

    EXPECT_FALSE(wasm_module_validate(nullptr, &fixture.binary()));
    EXPECT_FALSE(wasm_module_validate(store, nullptr));

    wasm_byte_vec_delete(&truncated);
    wasm_byte_vec_delete(&not_wasm);
}

TEST_F(WasmCApiModuleTest, imports_are_reported_with_module_and_field_names)
{
    wasm_importtype_vec_t imports = { 0 };

    wasm_module_imports(fixture.get(), &imports);

    /* simple.wat imports exactly one function from "env"."imported". */
    ASSERT_EQ(1u, imports.num_elems);

    const wasm_importtype_t *imported = imports.data[0];
    const wasm_name_t *module_name = wasm_importtype_module(imported);
    const wasm_name_t *field_name = wasm_importtype_name(imported);
    const wasm_externtype_t *extern_type = wasm_importtype_type(imported);

    ASSERT_NE(nullptr, module_name);
    ASSERT_NE(nullptr, field_name);
    ASSERT_NE(nullptr, extern_type);

    EXPECT_EQ(std::string("env"), std::string(module_name->data));
    EXPECT_EQ(std::string("imported"), std::string(field_name->data));
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(extern_type));

    const wasm_functype_t *func_type =
        wasm_externtype_as_functype_const(extern_type);
    ASSERT_NE(nullptr, func_type);
    EXPECT_EQ(1u, wasm_functype_params(func_type)->num_elems);
    EXPECT_EQ(1u, wasm_functype_results(func_type)->num_elems);

    EXPECT_FALSE(wasm_importtype_is_linked(imported));

    wasm_importtype_vec_delete(&imports);
}

TEST_F(WasmCApiEmptyModuleTest, empty_module_reports_no_imports_and_no_exports)
{
    wasm_importtype_vec_t imports = { 0 };
    wasm_exporttype_vec_t exports = { 0 };

    ASSERT_NE(nullptr, fixture.get());

    wasm_module_imports(fixture.get(), &imports);
    wasm_module_exports(fixture.get(), &exports);

    EXPECT_EQ(0u, imports.num_elems);
    EXPECT_EQ(0u, exports.num_elems);

    wasm_importtype_vec_delete(&imports);
    wasm_exporttype_vec_delete(&exports);
}

/* The export type called `name` of a module's export list, or nullptr. */
static const wasm_externtype_t *
export_type_of(const wasm_exporttype_vec_t &exports, const std::string &name)
{
    for (size_t i = 0; i < exports.num_elems; i++) {
        const wasm_name_t *export_name = wasm_exporttype_name(exports.data[i]);

        if (export_name && export_name->size == name.size() + 1
            && memcmp(export_name->data, name.c_str(), name.size() + 1) == 0)
            return wasm_exporttype_type(exports.data[i]);
    }
    return nullptr;
}

TEST_F(WasmCApiModuleTest, exports_are_reported_with_names_and_kinds)
{
    wasm_exporttype_vec_t exports = { 0 };

    wasm_module_exports(fixture.get(), &exports);
    ASSERT_EQ(6u, exports.num_elems);

    /* Every name a case looks up is present, and the kind it reports is the
       one simple.wat declares. */
    const wasm_externtype_t *add = export_type_of(exports, "add");
    const wasm_externtype_t *trap = export_type_of(exports, "trap");
    const wasm_externtype_t *call_imported =
        export_type_of(exports, "call_imported");
    const wasm_externtype_t *counter = export_type_of(exports, "counter");
    const wasm_externtype_t *memory = export_type_of(exports, "memory");
    const wasm_externtype_t *table = export_type_of(exports, "table");

    ASSERT_NE(nullptr, add);
    ASSERT_NE(nullptr, trap);
    ASSERT_NE(nullptr, call_imported);
    ASSERT_NE(nullptr, counter);
    ASSERT_NE(nullptr, memory);
    ASSERT_NE(nullptr, table);

    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(add));
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(trap));
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(call_imported));

    /* Every other name is reported as a missing export. */
    EXPECT_EQ(nullptr, export_type_of(exports, "no_such_export"));

    ASSERT_EQ(WASM_EXTERN_GLOBAL, wasm_externtype_kind(counter));
    const wasm_globaltype_t *global_type =
        wasm_externtype_as_globaltype_const(counter);
    ASSERT_NE(nullptr, global_type);
    EXPECT_EQ(WASM_VAR, wasm_globaltype_mutability(global_type));
    EXPECT_EQ(WASM_I32,
              wasm_valtype_kind(wasm_globaltype_content(global_type)));

    ASSERT_EQ(WASM_EXTERN_MEMORY, wasm_externtype_kind(memory));
    const wasm_memorytype_t *memory_type =
        wasm_externtype_as_memorytype_const(memory);
    ASSERT_NE(nullptr, memory_type);
    /* simple.wat declares (memory 1 2); without WASM_ENABLE_MULTI_MODULE the
       loader folds that into one page, so the reported maximum is one as
       well. */
    EXPECT_EQ(1u, wasm_memorytype_limits(memory_type)->min);
    EXPECT_EQ(1u, wasm_memorytype_limits(memory_type)->max);

    ASSERT_EQ(WASM_EXTERN_TABLE, wasm_externtype_kind(table));
    const wasm_tabletype_t *table_type =
        wasm_externtype_as_tabletype_const(table);
    ASSERT_NE(nullptr, table_type);
    EXPECT_EQ(WASM_FUNCREF,
              wasm_valtype_kind(wasm_tabletype_element(table_type)));
    EXPECT_EQ(2u, wasm_tabletype_limits(table_type)->min);
    EXPECT_EQ(4u, wasm_tabletype_limits(table_type)->max);

    wasm_exporttype_vec_delete(&exports);
}

TEST_F(WasmCApiModuleTest, module_name_round_trip)
{
    /* wasm_module_new() leaves the module without a name. */
    EXPECT_STREQ("", wasm_module_get_name(fixture.get()));

    EXPECT_TRUE(wasm_module_set_name(fixture.get(), "simple_module"));
    EXPECT_STREQ("simple_module", wasm_module_get_name(fixture.get()));

    EXPECT_FALSE(wasm_module_set_name(nullptr, "ignored"));
    EXPECT_STREQ("", wasm_module_get_name(nullptr));
}

TEST_F(WasmCApiModuleTest, share_and_obtain_keep_the_module_alive)
{
    wasm_shared_module_t *shared = wasm_module_share(fixture.get());

    ASSERT_NE(nullptr, shared);
    EXPECT_STREQ("", wasm_module_get_name((wasm_module_t *)shared));

    wasm_module_t *obtained = wasm_module_obtain(store, shared);
    ASSERT_NE(nullptr, obtained);
    EXPECT_EQ(shared, obtained);

    /* Two references are now held by the caller: the shared one and the one
       obtain returned.  Releasing both must not release the module the fixture
       still owns. */
    wasm_shared_module_delete(shared);
    wasm_module_delete(obtained);

    EXPECT_EQ(nullptr, wasm_module_share(nullptr));
    EXPECT_EQ(nullptr, wasm_module_obtain(nullptr, nullptr));
}

TEST_F(WasmCApiModuleTest, serialize_is_unavailable_without_the_jit)
{
    wasm_byte_vec_t serialized = { 0 };

    wasm_module_serialize(fixture.get(), &serialized);

    /* Only "JIT with eager compilation" implements serialization; every other
       build logs the fact and leaves the output vector empty. */
    EXPECT_EQ(0u, serialized.num_elems);
    EXPECT_EQ(nullptr, wasm_module_deserialize(store, &serialized));

    wasm_byte_vec_delete(&serialized);
}

} // namespace

/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The two families of functions the C API generates with macros, plus the
   const spelling of the wasm_*type_t -> wasm_externtype_t casts:

   - wasm_<name>_vec_new / _new_empty / _new_uninitialized / _copy / _delete,
     instantiated once per vector type (WASM_DEFINE_VEC_PLAIN /
     WASM_DEFINE_VEC_OWN in wasm_c_api.c),
   - wasm_<name>_as_ref{,_const}, wasm_ref_as_<name>{,_const},
     wasm_<name>_same and wasm_<name>_get/set_host_info{,_with_finalizer},
     instantiated once per reference type (WASM_DEFINE_REF).

   One case per family walks every instance of it, so the macro-generated
   entry points are all executed instead of only the one type a case happens
   to need.

   The global reference type is taken from a wasm_global_new() creation rather
   than from an instance export: wasm_global_copy() (used by
   wasm_instance_exports()) never copies the store, so an exported global has a
   NULL store and wasm_global_as_ref() answers NULL for it.  See
   global_copy_of_export_loses_its_store below. */

#include "c_api_test_fixture.h"

namespace {

/* Number of times the finalizer below ran, to check the host_info lifecycle
   without depending on the process exit. */
static int host_info_finalizer_calls;

static void
count_host_info_finalizer(void *info)
{
    if (info)
        host_info_finalizer_calls++;
}

static wasm_trap_t *
env_callback(const wasm_val_vec_t *args, wasm_val_vec_t *results)
{
    return nullptr;
}

class WasmCApiRefTest : public CApiTestBase
{
  protected:
    void SetUp() override
    {
        CApiTestBase::SetUp();
        fixture.init(store, "simple");

        imports = new HostImport(store, 1, 1, host_add);
        ASSERT_TRUE(imports->valid());

        instance = new FixtureInstance();
        instance->init(store, fixture.get(), &imports->as_vec());
        ASSERT_NE(nullptr, instance->get());

        host_info_finalizer_calls = 0;
    }

    void TearDown() override
    {
        delete instance;
        delete imports;
        fixture.reset();
        CApiTestBase::TearDown();
    }

    FixtureModule fixture;
    HostImport *imports = nullptr;
    FixtureInstance *instance = nullptr;
};

TEST_F(WasmCApiRefTest, ref_round_trip_over_every_reference_type)
{
    /* func: an exported function carries its store. */
    wasm_func_t *func = wasm_extern_as_func(instance->export_by_name("add"));
    ASSERT_NE(nullptr, func);

    wasm_ref_t *func_ref = wasm_func_as_ref(func);
    ASSERT_NE(nullptr, func_ref);
    wasm_func_t *func_from_ref = wasm_ref_as_func(func_ref);
    ASSERT_NE(nullptr, func_from_ref);
    EXPECT_TRUE(wasm_func_same(func, func_from_ref));
    wasm_func_delete(func_from_ref);

    /* The const spelling builds its own reference. */
    const wasm_ref_t *func_ref_const = wasm_func_as_ref_const(func);
    ASSERT_NE(nullptr, func_ref_const);
    ASSERT_NE(nullptr, wasm_ref_as_func_const(func_ref_const));
    wasm_ref_delete(const_cast<wasm_ref_t *>(func_ref_const));

    wasm_ref_delete(func_ref);

    /* global: a host-created global has to stand in, because an exported one
       goes through wasm_global_copy() and loses its store.  Only the
       global -> ref direction can be exercised: wasm_ref_as_global() rebuilds
       the global through wasm_global_new_internal(), which needs an instance,
       and wasm_global_new() does not register one (see
       global_refs_cannot_be_rebuilt_from_a_ref below). */
    wasm_globaltype_t *global_type =
        wasm_globaltype_new(wasm_valtype_new(WASM_I32), WASM_VAR);
    ASSERT_NE(nullptr, global_type);

    wasm_val_t global_init = val_i32(7);
    wasm_global_t *global = wasm_global_new(store, global_type, &global_init);
    ASSERT_NE(nullptr, global);

    wasm_ref_t *global_ref = wasm_global_as_ref(global);
    ASSERT_NE(nullptr, global_ref);
    EXPECT_EQ(nullptr, wasm_ref_as_global(global_ref));

    const wasm_ref_t *global_ref_const = wasm_global_as_ref_const(global);
    ASSERT_NE(nullptr, global_ref_const);
    EXPECT_EQ(nullptr, wasm_ref_as_global_const(global_ref_const));

    /* wasm_ref_delete() of a global reference now falls back to deleting this
       global's storage, and the global is released explicitly below, so
       neither reference is released here. */

    /* memory */
    wasm_memory_t *memory =
        wasm_extern_as_memory(instance->export_by_name("memory"));
    ASSERT_NE(nullptr, memory);

    wasm_ref_t *memory_ref = wasm_memory_as_ref(memory);
    ASSERT_NE(nullptr, memory_ref);
    wasm_memory_t *memory_from_ref = wasm_ref_as_memory(memory_ref);
    ASSERT_NE(nullptr, memory_from_ref);
    EXPECT_TRUE(wasm_memory_same(memory, memory_from_ref));
    wasm_memory_delete(memory_from_ref);

    const wasm_ref_t *memory_ref_const = wasm_memory_as_ref_const(memory);
    ASSERT_NE(nullptr, memory_ref_const);
    ASSERT_NE(nullptr, wasm_ref_as_memory_const(memory_ref_const));
    wasm_ref_delete(const_cast<wasm_ref_t *>(memory_ref_const));

    wasm_ref_delete(memory_ref);

    /* table */
    wasm_table_t *table =
        wasm_extern_as_table(instance->export_by_name("table"));
    ASSERT_NE(nullptr, table);

    wasm_ref_t *table_ref = wasm_table_as_ref(table);
    ASSERT_NE(nullptr, table_ref);
    wasm_table_t *table_from_ref = wasm_ref_as_table(table_ref);
    ASSERT_NE(nullptr, table_from_ref);
    EXPECT_TRUE(wasm_table_same(table, table_from_ref));
    wasm_table_delete(table_from_ref);

    const wasm_ref_t *table_ref_const = wasm_table_as_ref_const(table);
    ASSERT_NE(nullptr, table_ref_const);
    ASSERT_NE(nullptr, wasm_ref_as_table_const(table_ref_const));
    wasm_ref_delete(const_cast<wasm_ref_t *>(table_ref_const));

    wasm_ref_delete(table_ref);

    /* foreign: the only reference type with no instance behind it. */
    wasm_foreign_t *foreign = wasm_foreign_new(store);
    ASSERT_NE(nullptr, foreign);

    wasm_ref_t *foreign_ref = wasm_foreign_as_ref(foreign);
    ASSERT_NE(nullptr, foreign_ref);
    wasm_foreign_t *foreign_from_ref = wasm_ref_as_foreign(foreign_ref);
    ASSERT_NE(nullptr, foreign_from_ref);
    EXPECT_TRUE(wasm_foreign_same(foreign, foreign_from_ref));

    const wasm_ref_t *foreign_ref_const = wasm_foreign_as_ref_const(foreign);
    ASSERT_NE(nullptr, foreign_ref_const);
    ASSERT_NE(nullptr, wasm_ref_as_foreign_const(foreign_ref_const));
    wasm_ref_delete(const_cast<wasm_ref_t *>(foreign_ref_const));
    wasm_ref_delete(foreign_ref);

    /* A reference of one kind is not silently accepted by another kind. */
    wasm_ref_t *func_ref_again = wasm_func_as_ref(func);
    ASSERT_NE(nullptr, func_ref_again);
    EXPECT_EQ(nullptr, wasm_ref_as_global(func_ref_again));
    EXPECT_EQ(nullptr, wasm_ref_as_memory(func_ref_again));
    EXPECT_EQ(nullptr, wasm_ref_as_table(func_ref_again));
    EXPECT_EQ(nullptr, wasm_ref_as_foreign(func_ref_again));
    wasm_ref_delete(func_ref_again);

    wasm_global_delete(global);
    wasm_globaltype_delete(global_type);
}

TEST_F(WasmCApiRefTest, host_info_round_trip_over_every_reference_type)
{
    int info_a = 1, info_b = 2;

    wasm_func_t *func = wasm_extern_as_func(instance->export_by_name("add"));
    wasm_globaltype_t *global_type =
        wasm_globaltype_new(wasm_valtype_new(WASM_I32), WASM_VAR);
    wasm_memory_t *memory =
        wasm_extern_as_memory(instance->export_by_name("memory"));
    wasm_table_t *table =
        wasm_extern_as_table(instance->export_by_name("table"));
    wasm_foreign_t *foreign = wasm_foreign_new(store);

    ASSERT_NE(nullptr, func);
    ASSERT_NE(nullptr, global_type);
    ASSERT_NE(nullptr, memory);
    ASSERT_NE(nullptr, table);
    ASSERT_NE(nullptr, foreign);

    wasm_val_t global_init = val_i32(1);
    wasm_global_t *global = wasm_global_new(store, global_type, &global_init);
    ASSERT_NE(nullptr, global);

    /* Each reference type has its own host_info slot. */
    EXPECT_EQ(nullptr, wasm_func_get_host_info(func));
    wasm_func_set_host_info(func, &info_a);
    EXPECT_EQ(&info_a, wasm_func_get_host_info(func));
    wasm_func_set_host_info_with_finalizer(func, &info_b,
                                           count_host_info_finalizer);

    EXPECT_EQ(nullptr, wasm_global_get_host_info(global));
    wasm_global_set_host_info(global, &info_a);
    EXPECT_EQ(&info_a, wasm_global_get_host_info(global));
    wasm_global_set_host_info_with_finalizer(global, &info_b,
                                             count_host_info_finalizer);

    EXPECT_EQ(nullptr, wasm_memory_get_host_info(memory));
    wasm_memory_set_host_info(memory, &info_a);
    EXPECT_EQ(&info_a, wasm_memory_get_host_info(memory));
    wasm_memory_set_host_info_with_finalizer(memory, &info_b,
                                             count_host_info_finalizer);

    EXPECT_EQ(nullptr, wasm_table_get_host_info(table));
    wasm_table_set_host_info(table, &info_a);
    EXPECT_EQ(&info_a, wasm_table_get_host_info(table));
    wasm_table_set_host_info_with_finalizer(table, &info_b,
                                            count_host_info_finalizer);

    EXPECT_EQ(nullptr, wasm_foreign_get_host_info(foreign));
    wasm_foreign_set_host_info(foreign, &info_a);
    EXPECT_EQ(&info_a, wasm_foreign_get_host_info(foreign));
    wasm_foreign_set_host_info_with_finalizer(foreign, &info_b,
                                              count_host_info_finalizer);

    /* The plain wasm_ref_t has its own slot; releasing it runs the finalizer
       exactly once. */
    wasm_ref_t *ref = wasm_func_as_ref(func);
    ASSERT_NE(nullptr, ref);

    EXPECT_EQ(nullptr, wasm_ref_get_host_info(ref));
    wasm_ref_set_host_info(ref, &info_a);
    EXPECT_EQ(&info_a, wasm_ref_get_host_info(ref));
    wasm_ref_set_host_info_with_finalizer(ref, &info_b,
                                          count_host_info_finalizer);
    wasm_ref_delete(ref);

    EXPECT_EQ(1, host_info_finalizer_calls);

    wasm_global_delete(global);
    wasm_globaltype_delete(global_type);
}

TEST_F(WasmCApiRefTest, host_info_of_a_standalone_creation_is_usable)
{
    /* A function created by the host carries host_info from birth. */
    wasm_functype_t *type = i32_functype(1, 1);
    ASSERT_NE(nullptr, type);

    wasm_func_t *func = wasm_func_new(store, type, env_callback);
    ASSERT_NE(nullptr, func);

    EXPECT_EQ(nullptr, wasm_func_get_host_info(func));
    wasm_func_set_host_info(func, &host_info_finalizer_calls);
    EXPECT_EQ(&host_info_finalizer_calls, wasm_func_get_host_info(func));

    wasm_func_delete(func);
    wasm_functype_delete(type);
}

TEST_F(WasmCApiRefTest, global_copy_of_export_loses_its_store)
{
    /* wasm_instance_exports() hands out copied externs, and wasm_global_copy()
       copies every field of a wasm_global_t except its store.  The copy cannot
       build a reference or report host info.

       TODO: copy `store` in wasm_global_copy() (core/iwasm/common/
       wasm_c_api.c), then let this case do the round trip the other reference
       types get above. */
    wasm_extern_t *external = instance->export_by_name("counter");
    ASSERT_NE(nullptr, external);

    wasm_global_t *global = wasm_extern_as_global(external);
    ASSERT_NE(nullptr, global);

    EXPECT_EQ(nullptr, wasm_global_as_ref(global));
    EXPECT_EQ(nullptr, wasm_global_get_host_info(global));
}

TEST_F(WasmCApiRefTest, global_refs_cannot_be_rebuilt_from_a_ref)
{
    /* The mirror side of the same gap: wasm_ref_as_global() calls
       wasm_global_new_internal(), which needs a module instance to read the
       global's type and initial value from, and wasm_global_new() never
       registers a host-created global with one.  wasm_global_get() therefore
       has nothing to read either, and only global -> ref works.

       TODO: give a host-created global an instance (or a fallback that reads
       from wasm_global_t->type/init) in core/iwasm/common/wasm_c_api.c. */
    wasm_globaltype_t *global_type =
        wasm_globaltype_new(wasm_valtype_new(WASM_I32), WASM_VAR);
    ASSERT_NE(nullptr, global_type);

    wasm_val_t init = val_i32(3);
    wasm_global_t *global = wasm_global_new(store, global_type, &init);
    ASSERT_NE(nullptr, global);

    wasm_val_t out = { 0 };
    wasm_global_get(global, &out);
    EXPECT_EQ(0, out.kind);

    wasm_ref_t *ref = wasm_global_as_ref(global);
    ASSERT_NE(nullptr, ref);
    EXPECT_EQ(nullptr, wasm_ref_as_global(ref));

    wasm_global_delete(global);
    wasm_globaltype_delete(global_type);
}

} // namespace

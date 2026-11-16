/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Instantiation and execution: wasm_instance_new (with its argument-taking
   variants), the export list of an instance, and wasm_func_call with its
   result, type-mismatch and trap paths. */

#include "c_api_test_fixture.h"

namespace {

/* The one host function simple.wat's (import "env" "imported") needs. */
using SimpleImports = HostImport;

class WasmCApiInstanceTest : public CApiTestBase
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

class WasmCApiEmptyInstanceTest : public CApiTestBase
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

/* The exported function `name` of `instance`, or nullptr; the instance must
   outlive the use. */
static wasm_func_t *
exported_func(FixtureInstance &instance, const std::string &name)
{
    wasm_extern_t *external = instance.export_by_name(name);

    if (!external || wasm_extern_kind(external) != WASM_EXTERN_FUNC)
        return nullptr;
    return wasm_extern_as_func(external);
}

TEST_F(WasmCApiInstanceTest, instantiate_with_matching_import)
{
    SimpleImports imports(store, 1, 1, host_add);

    ASSERT_TRUE(imports.valid());

    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());
    EXPECT_EQ(nullptr, instance.trap());
    EXPECT_GT(instance.export_count(), 0u);
}

TEST_F(WasmCApiInstanceTest, missing_host_import_surfaces_when_it_is_called)
{
    wasm_extern_vec_t no_imports = { 0 };
    FixtureInstance instance;

    /* simple.wat declares (import "env" "imported"), but a module whose host
       symbol is not supplied still instantiates: the missing import is only
       reported when the wasm code reaches it. */
    instance.init(store, fixture.get(), &no_imports);
    ASSERT_NE(nullptr, instance.get());

    wasm_func_t *call_imported = exported_func(instance, "call_imported");
    ASSERT_NE(nullptr, call_imported);

    wasm_val_t args[1] = { val_i32(1) };
    wasm_val_vec_t args_vec = { 0 };
    wasm_val_vec_t results = { 0 };

    wasm_val_vec_new(&args_vec, 1, args);
    wasm_val_vec_new_uninitialized(&results, 1);

    wasm_trap_t *trap = wasm_func_call(call_imported, &args_vec, &results);

    ASSERT_NE(nullptr, trap);

    wasm_message_t message = { 0 };
    wasm_trap_message(trap, &message);
    EXPECT_GT(message.size, 0u);
    wasm_byte_vec_delete(&message);

    wasm_trap_delete(trap);
    wasm_val_vec_delete(&results);
    wasm_val_vec_delete(&args_vec);
}

TEST_F(WasmCApiInstanceTest, instantiation_fails_on_mismatched_import_type)
{
    /* The module wants (i32) -> i32; hand it a two-parameter function. */
    SimpleImports wrong_type(store, 2, 1, host_add);
    wasm_trap_t *trap = nullptr;

    ASSERT_TRUE(wrong_type.valid());

    EXPECT_EQ(nullptr, wasm_instance_new(store, fixture.get(),
                                         &wrong_type.as_vec(), &trap));
    EXPECT_NE(nullptr, trap);

    wasm_trap_delete(trap);
}

TEST_F(WasmCApiInstanceTest, null_import_element_fails_the_link)
{
    wasm_extern_t *null_extern = nullptr;
    wasm_extern_vec_t imports_with_null = { 0 };
    wasm_trap_t *trap = nullptr;

    wasm_extern_vec_new(&imports_with_null, 1, &null_extern);

    EXPECT_EQ(nullptr, wasm_instance_new(store, fixture.get(),
                                         &imports_with_null, &trap));
    EXPECT_NE(nullptr, trap);

    wasm_trap_delete(trap);
    wasm_extern_vec_delete(&imports_with_null);
}

TEST_F(WasmCApiEmptyInstanceTest, empty_module_instantiates_with_no_exports)
{
    wasm_extern_vec_t no_imports = { 0 };
    wasm_trap_t *trap = nullptr;
    wasm_instance_t *instance = nullptr;

    instance = wasm_instance_new(store, fixture.get(), &no_imports, &trap);

    ASSERT_NE(nullptr, instance);
    EXPECT_EQ(nullptr, trap);

    wasm_extern_vec_t exports = { 0 };
    wasm_instance_exports(instance, &exports);
    EXPECT_EQ(0u, exports.num_elems);
    wasm_extern_vec_delete(&exports);

    wasm_instance_delete(instance);
}

TEST_F(WasmCApiInstanceTest, exports_carry_the_declared_kinds)
{
    SimpleImports imports(store, 1, 1, host_add);
    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());

    wasm_extern_t *add = instance.export_by_name("add");
    wasm_extern_t *counter = instance.export_by_name("counter");
    wasm_extern_t *memory = instance.export_by_name("memory");
    wasm_extern_t *table = instance.export_by_name("table");

    ASSERT_NE(nullptr, add);
    ASSERT_NE(nullptr, counter);
    ASSERT_NE(nullptr, memory);
    ASSERT_NE(nullptr, table);

    /* The instance reports the kind the module declares for that name.  The
       kinds are read one by one because wasm_exporttype_vec_t and the
       instance's wasm_extern_vec_t are in the same order. */
    wasm_exporttype_vec_t module_exports = { 0 };
    wasm_module_exports(fixture.get(), &module_exports);
    ASSERT_EQ(module_exports.num_elems, instance.export_count());

    for (size_t i = 0; i < module_exports.num_elems; i++) {
        const wasm_externtype_t *type =
            wasm_exporttype_type(module_exports.data[i]);
        wasm_extern_t *external = instance.export_at(i);

        ASSERT_NE(nullptr, type);
        ASSERT_NE(nullptr, external);
        EXPECT_EQ(wasm_externtype_kind(type), wasm_extern_kind(external));
    }
    wasm_exporttype_vec_delete(&module_exports);

    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_extern_kind(add));
    EXPECT_EQ(WASM_EXTERN_GLOBAL, wasm_extern_kind(counter));
    EXPECT_EQ(WASM_EXTERN_MEMORY, wasm_extern_kind(memory));
    EXPECT_EQ(WASM_EXTERN_TABLE, wasm_extern_kind(table));

    /* The const accessors describe the same object (a cast, not a copy). */
    EXPECT_EQ(add, wasm_func_as_extern_const(wasm_extern_as_func(add)));
    EXPECT_EQ(counter,
              wasm_global_as_extern_const(wasm_extern_as_global(counter)));
    EXPECT_EQ(memory,
              wasm_memory_as_extern_const(wasm_extern_as_memory(memory)));
    EXPECT_EQ(table, wasm_table_as_extern_const(wasm_extern_as_table(table)));

    wasm_externtype_t *extern_type = wasm_extern_type(add);
    ASSERT_NE(nullptr, extern_type);
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(extern_type));
    wasm_externtype_delete(extern_type);

    EXPECT_EQ(nullptr, instance.export_by_name("no_such_export"));
    EXPECT_EQ(nullptr, instance.export_at(4096));
}

TEST_F(WasmCApiInstanceTest, call_added_values)
{
    SimpleImports imports(store, 1, 1, host_add);
    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());

    wasm_func_t *add = exported_func(instance, "add");
    ASSERT_NE(nullptr, add);
    EXPECT_EQ(2u, wasm_func_param_arity(add));
    EXPECT_EQ(1u, wasm_func_result_arity(add));

    wasm_val_t args[2] = { val_i32(2), val_i32(3) };
    wasm_val_vec_t args_vec = { 0 };
    wasm_val_vec_t results = { 0 };

    wasm_val_vec_new(&args_vec, 2, args);
    /* One result slot: the vector's capacity is what wasm_func_call() writes
       into and what it overwrites with the real element count afterwards. */
    wasm_val_vec_new_uninitialized(&results, 1);

    wasm_trap_t *trap = wasm_func_call(add, &args_vec, &results);

    EXPECT_EQ(nullptr, trap);
    ASSERT_EQ(1u, results.num_elems);
    EXPECT_EQ(WASM_I32, results.data[0].kind);
    EXPECT_EQ(5, results.data[0].of.i32);

    wasm_val_vec_delete(&results);
    wasm_val_vec_delete(&args_vec);
    if (trap)
        wasm_trap_delete(trap);
}

TEST_F(WasmCApiInstanceTest,
       calling_an_imported_function_uses_the_host_callback)
{
    SimpleImports imports(store, 1, 1, host_double);
    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());

    /* simple.wat's call_imported() forwards its argument to the imported
       function; the host callback host_double doubles it, so 21 comes back as
       42. */
    wasm_func_t *call_imported = exported_func(instance, "call_imported");
    ASSERT_NE(nullptr, call_imported);

    wasm_val_t args[1] = { val_i32(21) };
    wasm_val_vec_t args_vec = { 0 };
    wasm_val_vec_t results = { 0 };

    wasm_val_vec_new(&args_vec, 1, args);
    wasm_val_vec_new_uninitialized(&results, 1);

    wasm_trap_t *trap = wasm_func_call(call_imported, &args_vec, &results);

    EXPECT_EQ(nullptr, trap);
    ASSERT_EQ(1u, results.num_elems);
    EXPECT_EQ(WASM_I32, results.data[0].kind);
    EXPECT_EQ(42, results.data[0].of.i32);

    wasm_val_vec_delete(&results);
    wasm_val_vec_delete(&args_vec);
    if (trap)
        wasm_trap_delete(trap);
}

TEST_F(WasmCApiInstanceTest, call_propagates_a_trap_with_frames)
{
    SimpleImports imports(store, 1, 1, host_add);
    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());

    wasm_func_t *trap_func = exported_func(instance, "trap");
    ASSERT_NE(nullptr, trap_func);
    EXPECT_EQ(0u, wasm_func_param_arity(trap_func));
    EXPECT_EQ(0u, wasm_func_result_arity(trap_func));

    wasm_val_vec_t no_args = { 0 };
    wasm_val_vec_t no_results = { 0 };
    wasm_trap_t *trap = wasm_func_call(trap_func, &no_args, &no_results);

    ASSERT_NE(nullptr, trap);

    wasm_message_t message = { 0 };
    wasm_trap_message(trap, &message);
    EXPECT_GT(message.size, 0u);
    wasm_byte_vec_delete(&message);

    /* The origin is the frame that trapped, and the trace contains it.  Frames
       are only recorded with WASM_ENABLE_DUMP_CALL_STACK, which the default
       feature set leaves off, so without it the trap carries none. */
#if WASM_ENABLE_DUMP_CALL_STACK != 0
    wasm_frame_t *origin = wasm_trap_origin(trap);
    ASSERT_NE(nullptr, origin);
    EXPECT_EQ(instance.get(), wasm_frame_instance(origin));

    wasm_frame_t *origin_copy = wasm_frame_copy(origin);
    ASSERT_NE(nullptr, origin_copy);
    EXPECT_EQ(wasm_frame_func_index(origin),
              wasm_frame_func_index(origin_copy));
    EXPECT_EQ(wasm_frame_module_offset(origin),
              wasm_frame_module_offset(origin_copy));
    EXPECT_EQ(wasm_frame_func_offset(origin),
              wasm_frame_func_offset(origin_copy));
    wasm_frame_delete(origin_copy);
    wasm_frame_delete(origin);

    wasm_frame_vec_t trace = { 0 };
    wasm_trap_trace(trap, &trace);
    EXPECT_GT(trace.num_elems, 0u);
    wasm_frame_vec_delete(&trace);
#else
    EXPECT_EQ(nullptr, wasm_trap_origin(trap));

    wasm_frame_vec_t trace = { 0 };
    wasm_trap_trace(trap, &trace);
    EXPECT_EQ(0u, trace.num_elems);
    wasm_frame_vec_delete(&trace);
#endif

    wasm_trap_delete(trap);
}

TEST_F(WasmCApiInstanceTest, instance_new_with_args_accepts_other_limits)
{
    SimpleImports imports(store, 1, 1, host_add);
    wasm_trap_t *trap = nullptr;
    wasm_instance_t *instance = nullptr;

    ASSERT_TRUE(imports.valid());

    instance =
        wasm_instance_new_with_args(store, fixture.get(), &imports.as_vec(),
                                    &trap, KILOBYTE(64), KILOBYTE(64));
    ASSERT_NE(nullptr, instance);
    EXPECT_EQ(nullptr, trap);

    wasm_instance_delete(instance);

    /* The _ex form takes the whole InstantiationArgs struct instead. */
    InstantiationArgs args = { 0 };
    args.default_stack_size = KILOBYTE(64);
    args.host_managed_heap_size = KILOBYTE(64);

    instance = wasm_instance_new_with_args_ex(store, fixture.get(),
                                              &imports.as_vec(), &trap, &args);
    ASSERT_NE(nullptr, instance);
    EXPECT_EQ(nullptr, trap);

    wasm_instance_delete(instance);
}

TEST_F(WasmCApiInstanceTest, exec_time_queries_answer_without_a_profiler)
{
    SimpleImports imports(store, 1, 1, host_add);
    FixtureInstance instance;
    instance.init(store, fixture.get(), &imports.as_vec());

    ASSERT_NE(nullptr, instance.get());

    /* Without WASM_ENABLE_PERF_PROFILING both queries report -1.0 rather than
       a measured time; the case is here for the out-of-range access paths. */
    EXPECT_DOUBLE_EQ(-1.0, wasm_instance_sum_wasm_exec_time(instance.get()));
    EXPECT_DOUBLE_EQ(
        -1.0, wasm_instance_get_wasm_func_exec_time(instance.get(), "add"));
}

} // namespace

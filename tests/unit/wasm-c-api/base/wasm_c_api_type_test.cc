/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The type representations that are built by hand rather than read out of a
   module: wasm_importtype_t / wasm_exporttype_t, the four wasm_*type_t behind
   wasm_externtype_t, wasm_val_t, references and foreign objects, and traps that
   no instance produced. */

#include "c_api_test_fixture.h"

namespace {

/* A zero-terminated name the way the C API's *_new functions expect it: the
   size counts the terminator (see wasm_name_new_from_string_nt). */
static wasm_name_t
name_of(const std::string &text)
{
    wasm_name_t name = { 0 };

    wasm_byte_vec_new(&name, text.size() + 1, text.c_str());
    return name;
}

class WasmCApiTypeTest : public CApiTestBase
{};

TEST_F(WasmCApiTypeTest, importtype_round_trip)
{
    wasm_name_t module_name = name_of("env");
    wasm_name_t field_name = name_of("imported");
    wasm_functype_t *func_type = i32_functype(1, 1);

    ASSERT_NE(nullptr, func_type);

    wasm_importtype_t *import_type = wasm_importtype_new(
        &module_name, &field_name, wasm_functype_as_externtype(func_type));

    ASSERT_NE(nullptr, import_type);

    const wasm_name_t *got_module = wasm_importtype_module(import_type);
    const wasm_name_t *got_field = wasm_importtype_name(import_type);
    const wasm_externtype_t *got_type = wasm_importtype_type(import_type);

    ASSERT_NE(nullptr, got_module);
    ASSERT_NE(nullptr, got_field);
    ASSERT_NE(nullptr, got_type);

    EXPECT_EQ(std::string("env"), std::string(got_module->data));
    EXPECT_EQ(std::string("imported"), std::string(got_field->data));
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(got_type));

    wasm_importtype_delete(import_type);
}

TEST_F(WasmCApiTypeTest, importtype_copy_is_independent)
{
    wasm_name_t module_name = name_of("env");
    wasm_name_t field_name = name_of("imported");
    wasm_functype_t *func_type = i32_functype(1, 1);

    ASSERT_NE(nullptr, func_type);

    wasm_importtype_t *import_type = wasm_importtype_new(
        &module_name, &field_name, wasm_functype_as_externtype(func_type));
    ASSERT_NE(nullptr, import_type);

    wasm_importtype_t *copy = wasm_importtype_copy(import_type);
    ASSERT_NE(nullptr, copy);
    EXPECT_NE(import_type, copy);
    EXPECT_EQ(wasm_importtype_module(import_type)->size,
              wasm_importtype_module(copy)->size);
    EXPECT_EQ(0, memcmp(wasm_importtype_module(import_type)->data,
                        wasm_importtype_module(copy)->data,
                        wasm_importtype_module(copy)->size));

    /* Deleting the original first must not disturb the copy. */
    wasm_importtype_delete(import_type);
    EXPECT_STREQ("imported", wasm_importtype_name(copy)->data);
    wasm_importtype_delete(copy);
}

TEST_F(WasmCApiTypeTest, importtype_rejects_missing_parts)
{
    wasm_name_t module_name = name_of("env");

    EXPECT_EQ(nullptr, wasm_importtype_new(nullptr, &module_name, nullptr));
    EXPECT_EQ(nullptr, wasm_importtype_new(&module_name, nullptr, nullptr));

    /* Nothing took ownership, so the name is still ours to release. */
    wasm_byte_vec_delete(&module_name);
}

TEST_F(WasmCApiTypeTest, importtype_is_linked_reports_false_for_unknown_symbols)
{
    wasm_name_t module_name = name_of("env");
    wasm_name_t field_name = name_of("not_registered_anywhere");
    wasm_functype_t *func_type = i32_functype(0, 0);

    ASSERT_NE(nullptr, func_type);

    wasm_importtype_t *import_type = wasm_importtype_new(
        &module_name, &field_name, wasm_functype_as_externtype(func_type));
    ASSERT_NE(nullptr, import_type);

    EXPECT_FALSE(wasm_importtype_is_linked(import_type));
    EXPECT_FALSE(wasm_importtype_is_linked(nullptr));

    wasm_importtype_delete(import_type);
}

TEST_F(WasmCApiTypeTest, exporttype_round_trip)
{
    wasm_name_t name = name_of("answer");
    wasm_functype_t *func_type = i32_functype(0, 1);

    ASSERT_NE(nullptr, func_type);

    wasm_exporttype_t *export_type =
        wasm_exporttype_new(&name, wasm_functype_as_externtype(func_type));

    ASSERT_NE(nullptr, export_type);

    const wasm_name_t *got_name = wasm_exporttype_name(export_type);
    const wasm_externtype_t *got_type = wasm_exporttype_type(export_type);

    ASSERT_NE(nullptr, got_name);
    ASSERT_NE(nullptr, got_type);
    EXPECT_EQ(std::string("answer"), std::string(got_name->data));
    EXPECT_EQ(WASM_EXTERN_FUNC, wasm_externtype_kind(got_type));

    wasm_exporttype_t *copy = wasm_exporttype_copy(export_type);
    ASSERT_NE(nullptr, copy);
    EXPECT_STREQ("answer", wasm_exporttype_name(copy)->data);

    wasm_exporttype_delete(copy);
    wasm_exporttype_delete(export_type);

    EXPECT_EQ(nullptr, wasm_exporttype_new(nullptr, nullptr));
    EXPECT_EQ(nullptr, wasm_exporttype_name(nullptr));
    EXPECT_EQ(nullptr, wasm_exporttype_type(nullptr));
}

TEST_F(WasmCApiTypeTest, externtype_kind_and_copy)
{
    wasm_functype_t *func_type = i32_functype(1, 1);
    wasm_globaltype_t *global_type =
        wasm_globaltype_new(wasm_valtype_new(WASM_I32), WASM_CONST);
    wasm_limits_t memory_limits = { 1, 2 };
    wasm_limits_t table_limits = { 2, 4 };
    wasm_memorytype_t *memory_type = wasm_memorytype_new(&memory_limits);
    wasm_tabletype_t *table_type =
        wasm_tabletype_new(wasm_valtype_new(WASM_FUNCREF), &table_limits);

    ASSERT_NE(nullptr, func_type);
    ASSERT_NE(nullptr, global_type);
    ASSERT_NE(nullptr, memory_type);
    ASSERT_NE(nullptr, table_type);

    wasm_externtype_t *extern_types[4] = {
        wasm_functype_as_externtype(func_type),
        wasm_globaltype_as_externtype(global_type),
        wasm_memorytype_as_externtype(memory_type),
        wasm_tabletype_as_externtype(table_type),
    };
    const wasm_externkind_t expected[4] = { WASM_EXTERN_FUNC,
                                            WASM_EXTERN_GLOBAL,
                                            WASM_EXTERN_MEMORY,
                                            WASM_EXTERN_TABLE };

    for (size_t i = 0; i < 4; i++) {
        EXPECT_EQ(expected[i], wasm_externtype_kind(extern_types[i]));

        /* The *_as_* accessors are casts, so they must round-trip; the copy
           is what actually has to be released separately. */
        wasm_externtype_t *copy = wasm_externtype_copy(extern_types[i]);
        ASSERT_NE(nullptr, copy);
        EXPECT_EQ(expected[i], wasm_externtype_kind(copy));
        wasm_externtype_delete(copy);
    }

    ASSERT_NE(nullptr, wasm_externtype_as_functype(extern_types[0]));
    ASSERT_NE(nullptr, wasm_externtype_as_globaltype(extern_types[1]));
    ASSERT_NE(nullptr, wasm_externtype_as_memorytype(extern_types[2]));
    ASSERT_NE(nullptr, wasm_externtype_as_tabletype(extern_types[3]));

    /* The non-const wasm_*type_as_externtype spellings are exercised here too:
       they are casts to the same object, so nothing extra has to be freed. */
    EXPECT_EQ(extern_types[0], wasm_functype_as_externtype(func_type));
    EXPECT_EQ(extern_types[1], wasm_globaltype_as_externtype(global_type));
    EXPECT_EQ(extern_types[2], wasm_memorytype_as_externtype(memory_type));
    EXPECT_EQ(extern_types[3], wasm_tabletype_as_externtype(table_type));

    /* The four *_as_externtype_const casts are separate definitions from the
       non-const ones, so they need their own call. */
    EXPECT_EQ(extern_types[0], wasm_functype_as_externtype_const(func_type));
    EXPECT_EQ(extern_types[1],
              wasm_globaltype_as_externtype_const(global_type));
    EXPECT_EQ(extern_types[2],
              wasm_memorytype_as_externtype_const(memory_type));
    EXPECT_EQ(extern_types[3], wasm_tabletype_as_externtype_const(table_type));

    EXPECT_EQ(1u, wasm_functype_params(
                      wasm_externtype_as_functype_const(extern_types[0]))
                      ->num_elems);
    EXPECT_EQ(WASM_CONST,
              wasm_globaltype_mutability(
                  wasm_externtype_as_globaltype_const(extern_types[1])));
    EXPECT_EQ(1u, wasm_memorytype_limits(
                      wasm_externtype_as_memorytype_const(extern_types[2]))
                      ->min);
    EXPECT_EQ(2u, wasm_tabletype_limits(
                      wasm_externtype_as_tabletype_const(extern_types[3]))
                      ->min);

    wasm_externtype_delete(wasm_functype_as_externtype(func_type));
    wasm_externtype_delete(wasm_globaltype_as_externtype(global_type));
    wasm_externtype_delete(wasm_memorytype_as_externtype(memory_type));
    wasm_externtype_delete(wasm_tabletype_as_externtype(table_type));
}

TEST_F(WasmCApiTypeTest, val_copy_copies_every_kind)
{
    wasm_val_t src[4] = { val_i32(-7), val_i64(1LL << 40), val_f32(0.5f),
                          val_f64(0.25) };
    wasm_valkind_t kinds[4] = { WASM_I32, WASM_I64, WASM_F32, WASM_F64 };

    for (size_t i = 0; i < 4; i++) {
        wasm_val_t dst = { 0 };

        wasm_val_copy(&dst, &src[i]);
        EXPECT_EQ(kinds[i], dst.kind);
        EXPECT_EQ(0, memcmp(&src[i], &dst, sizeof(wasm_val_t)));

        /* Copying nothing is a no-op, not a crash. */
        wasm_val_copy(&dst, nullptr);
        wasm_val_copy(nullptr, &src[i]);
    }
}

TEST_F(WasmCApiTypeTest, foreign_refs_round_trip)
{
    wasm_foreign_t *foreign = wasm_foreign_new(store);

    ASSERT_NE(nullptr, foreign);

    wasm_ref_t *ref = wasm_foreign_as_ref(foreign);
    ASSERT_NE(nullptr, ref);
    EXPECT_EQ(foreign, wasm_ref_as_foreign(ref));
    EXPECT_TRUE(wasm_foreign_same(foreign, wasm_ref_as_foreign(ref)));

    wasm_ref_t *ref_copy = wasm_ref_copy(ref);
    ASSERT_NE(nullptr, ref_copy);
    EXPECT_TRUE(wasm_ref_same(ref, ref_copy));
    EXPECT_EQ(nullptr, wasm_ref_copy(nullptr));

    wasm_ref_delete(ref_copy);
    wasm_ref_delete(ref);

    /* The reference count of a foreign object only reaches one through
       wasm_foreign_new_internal(), so a foreign created by wasm_foreign_new()
       is never released by wasm_foreign_delete(); the case only checks that
       the call is safe. */
    wasm_foreign_delete(foreign);
    wasm_foreign_delete(nullptr);

    EXPECT_EQ(nullptr, wasm_foreign_new(nullptr));
}

TEST_F(WasmCApiTypeTest, trap_holds_the_message_it_was_given)
{
    wasm_message_t message = name_of("hand made trap");
    wasm_trap_t *trap = wasm_trap_new(store, &message);

    ASSERT_NE(nullptr, trap);

    wasm_message_t out = { 0 };
    wasm_trap_message(trap, &out);
    EXPECT_EQ(message.size, out.size);
    EXPECT_EQ(0, memcmp(message.data, out.data, out.size));
    wasm_byte_vec_delete(&out);

    /* Without WASM_ENABLE_DUMP_CALL_STACK a trap has no frames, so there is no
       origin and the trace is empty rather than NULL. */
    EXPECT_EQ(nullptr, wasm_trap_origin(trap));

    wasm_frame_vec_t trace = { 0 };
    wasm_trap_trace(trap, &trace);
    EXPECT_EQ(0u, trace.num_elems);
    wasm_frame_vec_delete(&trace);

    EXPECT_EQ(nullptr, wasm_trap_new(nullptr, &message));

    wasm_trap_delete(trap);
    wasm_byte_vec_delete(&message);
}

TEST_F(WasmCApiTypeTest, null_arguments_are_tolerated)
{
    wasm_message_t out = { 0 };
    wasm_frame_vec_t trace = { 0 };

    wasm_trap_message(nullptr, &out);
    wasm_trap_trace(nullptr, &trace);
    EXPECT_EQ(0u, out.num_elems);
    EXPECT_EQ(0u, trace.num_elems);

    wasm_trap_delete(nullptr);
    wasm_frame_vec_delete(&trace);
}

/* ---------------------------------------------------------------------- */
/* The wasm_<name>_vec_* entry points.  They are generated by
   WASM_DEFINE_VEC_PLAIN / WASM_DEFINE_VEC_OWN in wasm_c_api.c, one set per
   vector type; each case below walks one family so that the generated
   functions are executed, not only the ones a case happens to need. */

TEST_F(WasmCApiTypeTest, byte_vec_lifecycle)
{
    const char payload[] = "six";
    wasm_byte_vec_t vec = { 0 };

    wasm_byte_vec_new_empty(&vec);
    EXPECT_EQ(0u, vec.num_elems);
    wasm_byte_vec_delete(&vec);

    wasm_byte_vec_new_uninitialized(&vec, 3);
    ASSERT_EQ(3u, vec.size);
    ASSERT_NE(nullptr, vec.data);
    wasm_byte_vec_delete(&vec);

    wasm_byte_vec_new(&vec, sizeof(payload) - 1, payload);
    ASSERT_EQ(3u, vec.num_elems);
    EXPECT_EQ(0, memcmp(vec.data, payload, 3));

    wasm_byte_vec_t copy = { 0 };
    wasm_byte_vec_copy(&copy, &vec);
    ASSERT_EQ(vec.num_elems, copy.num_elems);
    EXPECT_EQ(0, memcmp(copy.data, vec.data, vec.num_elems));

    wasm_byte_vec_delete(&copy);
    wasm_byte_vec_delete(&vec);
}

TEST_F(WasmCApiTypeTest, val_vec_lifecycle)
{
    wasm_val_t values[2] = { val_i32(1), val_i32(2) };
    wasm_val_vec_t vec = { 0 };

    wasm_val_vec_new_empty(&vec);
    EXPECT_EQ(0u, vec.num_elems);
    wasm_val_vec_delete(&vec);

    wasm_val_vec_new_uninitialized(&vec, 2);
    wasm_val_vec_delete(&vec);

    wasm_val_vec_new(&vec, 2, values);
    ASSERT_EQ(2u, vec.num_elems);
    EXPECT_EQ(2, vec.data[1].of.i32);

    wasm_val_vec_t copy = { 0 };
    wasm_val_vec_copy(&copy, &vec);
    EXPECT_EQ(vec.num_elems, copy.num_elems);
    wasm_val_vec_delete(&copy);

    wasm_val_vec_delete(&vec);
}

TEST_F(WasmCApiTypeTest, functype_vec_lifecycle)
{
    wasm_functype_t *types[2] = { i32_functype(0, 0), i32_functype(1, 1) };
    wasm_functype_vec_t vec = { 0 };

    ASSERT_NE(nullptr, types[0]);
    ASSERT_NE(nullptr, types[1]);

    wasm_functype_vec_new_empty(&vec);
    EXPECT_EQ(0u, vec.num_elems);
    wasm_functype_vec_delete(&vec);

    wasm_functype_vec_new_uninitialized(&vec, 2);
    wasm_functype_vec_delete(&vec);

    /* Ownership moves to the vector, so the elements are not deleted again. */
    wasm_functype_vec_new(&vec, 2, types);
    ASSERT_EQ(2u, vec.num_elems);
    EXPECT_EQ(WASM_EXTERN_FUNC,
              wasm_externtype_kind(wasm_functype_as_externtype(vec.data[0])));

    wasm_functype_vec_t copy = { 0 };
    wasm_functype_vec_copy(&copy, &vec);
    ASSERT_EQ(vec.num_elems, copy.num_elems);
    EXPECT_EQ(1u, wasm_functype_params(copy.data[1])->num_elems);

    wasm_functype_vec_delete(&copy);
    wasm_functype_vec_delete(&vec);
}

TEST_F(WasmCApiTypeTest, importtype_and_exporttype_vec_lifecycle)
{
    wasm_name_t module_name = name_of("env");
    wasm_name_t field_name = name_of("imported");
    wasm_functype_t *func_type = i32_functype(1, 1);

    ASSERT_NE(nullptr, func_type);

    wasm_importtype_t *imports[1] = { wasm_importtype_new(
        &module_name, &field_name, wasm_functype_as_externtype(func_type)) };
    ASSERT_NE(nullptr, imports[0]);

    wasm_importtype_vec_t import_vec = { 0 };
    wasm_importtype_vec_new_empty(&import_vec);
    EXPECT_EQ(0u, import_vec.num_elems);
    wasm_importtype_vec_delete(&import_vec);

    wasm_importtype_vec_new(&import_vec, 1, imports);
    ASSERT_EQ(1u, import_vec.num_elems);

    wasm_importtype_vec_t import_copy = { 0 };
    wasm_importtype_vec_copy(&import_copy, &import_vec);
    ASSERT_EQ(1u, import_copy.num_elems);
    EXPECT_STREQ("env", wasm_importtype_module(import_copy.data[0])->data);
    wasm_importtype_vec_delete(&import_copy);
    wasm_importtype_vec_delete(&import_vec);

    wasm_name_t export_name = name_of("answer");
    wasm_functype_t *export_type_rt = i32_functype(0, 1);
    ASSERT_NE(nullptr, export_type_rt);

    wasm_exporttype_t *exports[1] = { wasm_exporttype_new(
        &export_name, wasm_functype_as_externtype(export_type_rt)) };
    ASSERT_NE(nullptr, exports[0]);

    wasm_exporttype_vec_t export_vec = { 0 };
    wasm_exporttype_vec_new_empty(&export_vec);
    EXPECT_EQ(0u, export_vec.num_elems);
    wasm_exporttype_vec_delete(&export_vec);

    wasm_exporttype_vec_new(&export_vec, 1, exports);
    ASSERT_EQ(1u, export_vec.num_elems);

    wasm_exporttype_vec_t export_copy = { 0 };
    wasm_exporttype_vec_copy(&export_copy, &export_vec);
    ASSERT_EQ(1u, export_copy.num_elems);
    EXPECT_STREQ("answer", wasm_exporttype_name(export_copy.data[0])->data);
    wasm_exporttype_vec_delete(&export_copy);
    wasm_exporttype_vec_delete(&export_vec);
}

TEST_F(WasmCApiTypeTest, extern_vec_lifecycle)
{
    wasm_extern_vec_t vec = { 0 };
    wasm_extern_vec_new_empty(&vec);
    EXPECT_EQ(0u, vec.num_elems);
    wasm_extern_vec_delete(&vec);
}

TEST_F(WasmCApiTypeTest, frame_vec_lifecycle)
{
    wasm_frame_vec_t vec = { 0 };
    wasm_frame_vec_new_empty(&vec);
    wasm_frame_vec_delete(&vec);

    /* frame_vec_copy on an empty source is the documented no-op. */
    wasm_frame_vec_t copy = { 0 };
    wasm_frame_vec_copy(&copy, &vec);
    EXPECT_EQ(0u, copy.num_elems);
    wasm_frame_vec_delete(&copy);
}

TEST_F(WasmCApiTypeTest, instance_copy_is_a_stub)
{
    /* wasm_instance_copy() is part of the public header but has no
       implementation: it logs and returns NULL.  The module and store
       spellings are not even declared, so they cannot be called. */
    EXPECT_EQ(nullptr, wasm_instance_copy(nullptr));
}

} // namespace

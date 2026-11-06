/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "c_api_test_fixture.h"

namespace {
struct CallbackEnv {
    wasm_store_t *store;
    unsigned calls;
};

auto
with_context(void *opaque, const wasm_val_vec_t *args, wasm_val_vec_t *results)
    -> wasm_trap_t *
{
    auto *env = static_cast<CallbackEnv *>(opaque);
    env->calls++;
    EXPECT_EQ(1u, args->size);
    EXPECT_EQ(1u, results->size);
    if (args->data[0].of.i32 < 0) {
        wasm_name_t message;
        wasm_name_new_from_string_nt(&message, "indirect host trap");
        auto *trap = wasm_trap_new(env->store, &message);
        wasm_name_delete(&message);
        return trap;
    }
    results->data[0] = val_i32(args->data[0].of.i32 + 22);
    return nullptr;
}

TEST_F(CApiTestBase, indirect_imports_preserve_c_api_abi_and_environment)
{
    FixtureModule module;
    module.init(store, "indirect_import");
    ASSERT_NE(nullptr, module.get());
    CallbackEnv env = { store, 0 };
    wasm_functype_t *type = i32_functype(1, 1);
    wasm_func_t *plain = wasm_func_new(store, type, host_double);
    wasm_func_t *context =
        wasm_func_new_with_env(store, type, with_context, &env, nullptr);
    ASSERT_NE(nullptr, plain);
    ASSERT_NE(nullptr, context);
    // The C API's borrowed-vector macros require built-in arrays.
    // NOLINTBEGIN(modernize-avoid-c-arrays)
    wasm_extern_t *const items[] = { wasm_func_as_extern(plain),
                                     wasm_func_as_extern(context) };
    wasm_extern_vec_t imports;
    wasm_extern_vec_new(&imports, 2, items);
    {
        FixtureInstance instance;
        instance.init(store, module.get(), &imports);
        ASSERT_NE(nullptr, instance.get());
        const wasm_func_t *call =
            wasm_extern_as_func(instance.export_by_name("call"));
        ASSERT_NE(nullptr, call);
        for (unsigned i = 0; i < 3; ++i) {
            wasm_val_t values[] = { val_i32(i != 0 ? 1 : 0),
                                    val_i32(i == 2 ? -1 : 20) };
            wasm_val_t result[] = { val_i32(0) };
            const wasm_val_vec_t args = WASM_ARRAY_VEC(values);
            wasm_val_vec_t results = WASM_ARRAY_VEC(result);
            wasm_trap_t *trap = wasm_func_call(call, &args, &results);
            if (i == 2) {
                EXPECT_NE(nullptr, trap);
                wasm_trap_delete(trap);
            }
            else {
                EXPECT_EQ(nullptr, trap);
                EXPECT_EQ(i ? 42 : 40, result[0].of.i32);
            }
        }
    }
    // NOLINTEND(modernize-avoid-c-arrays)
    EXPECT_EQ(2u, env.calls);
    wasm_extern_vec_delete(&imports);
    wasm_functype_delete(type);
}
} // namespace

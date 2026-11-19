/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The vector types that own their elements, for the element types the other
   cases do not build vectors of: instance, module and store (whose copy
   functions are stubs that return NULL) and the empty valtype vector. */

#include "c_api_test_fixture.h"

/* wasm_c_api.c defines the vectors of instances, modules and stores, but
   wasm_c_api.h does not declare them.  Declare them here with the header's
   own macro, so the layout is the one the library uses. */
#define own
extern "C" {
WASM_DECLARE_VEC(instance, *)
WASM_DECLARE_VEC(module, *)
WASM_DECLARE_VEC(store, *)
}
#undef own

namespace {

class WasmCApiVecTest : public CApiTestBase
{};

TEST_F(WasmCApiVecTest, new_empty_gives_an_empty_vector)
{
    wasm_instance_vec_t instances = { 1, nullptr, 1, 1, nullptr };
    wasm_instance_vec_new_empty(&instances);
    EXPECT_EQ(0u, instances.size);
    EXPECT_EQ(0u, instances.num_elems);
    EXPECT_EQ(nullptr, instances.data);
    wasm_instance_vec_delete(&instances);

    wasm_module_vec_t modules = { 1, nullptr, 1, 1, nullptr };
    wasm_module_vec_new_empty(&modules);
    EXPECT_EQ(0u, modules.size);
    EXPECT_EQ(nullptr, modules.data);
    wasm_module_vec_delete(&modules);

    wasm_store_vec_t stores = { 1, nullptr, 1, 1, nullptr };
    wasm_store_vec_new_empty(&stores);
    EXPECT_EQ(0u, stores.size);
    EXPECT_EQ(nullptr, stores.data);
    wasm_store_vec_delete(&stores);

    wasm_valtype_vec_t valtypes = { 1, nullptr, 1, 1, nullptr };
    wasm_valtype_vec_new_empty(&valtypes);
    EXPECT_EQ(0u, valtypes.size);
    EXPECT_EQ(nullptr, valtypes.data);
    wasm_valtype_vec_delete(&valtypes);
}

TEST_F(WasmCApiVecTest, new_uninitialized_reserves_room)
{
    wasm_store_vec_t stores = { 0, nullptr, 0, 0, nullptr };

    wasm_store_vec_new_uninitialized(&stores, 3);
    EXPECT_EQ(3u, stores.size);
    EXPECT_NE(nullptr, stores.data);
    /* No element was filled in, so there is nothing to delete one by one. */
    stores.num_elems = 0;
    wasm_store_vec_delete(&stores);
}

TEST_F(WasmCApiVecTest, store_vec_new_takes_ownership_and_delete_frees)
{
    wasm_store_t *owned = wasm_store_new(engine);
    ASSERT_NE(nullptr, owned);

    wasm_store_t *list[1] = { owned };
    wasm_store_vec_t stores = { 0, nullptr, 0, 0, nullptr };

    wasm_store_vec_new(&stores, 1, list);
    ASSERT_EQ(1u, stores.size);
    ASSERT_EQ(1u, stores.num_elems);
    EXPECT_EQ(owned, stores.data[0]);

    /* The vector owns the store now; deleting it deletes the store. */
    wasm_store_vec_delete(&stores);
    EXPECT_EQ(nullptr, stores.data);
}

TEST_F(WasmCApiVecTest, copy_of_an_empty_vector_is_empty)
{
    wasm_instance_vec_t src = { 0, nullptr, 0, 0, nullptr };
    wasm_instance_vec_t out = { 5, nullptr, 5, 5, nullptr };
    wasm_instance_vec_copy(&out, &src);
    EXPECT_EQ(0u, out.size);
    EXPECT_EQ(nullptr, out.data);

    wasm_module_vec_t module_src = { 0, nullptr, 0, 0, nullptr };
    wasm_module_vec_t module_out = { 5, nullptr, 5, 5, nullptr };
    wasm_module_vec_copy(&module_out, &module_src);
    EXPECT_EQ(0u, module_out.size);

    wasm_store_vec_t store_src = { 0, nullptr, 0, 0, nullptr };
    wasm_store_vec_t store_out = { 5, nullptr, 5, 5, nullptr };
    wasm_store_vec_copy(&store_out, &store_src);
    EXPECT_EQ(0u, store_out.size);
}

TEST_F(WasmCApiVecTest, copy_fails_where_the_element_copy_is_a_stub)
{
    /* wasm_instance_copy(), wasm_module_copy() and wasm_store_copy() are stubs
       that return NULL, so copying a non-empty vector cannot succeed and
       leaves the destination empty. */
    wasm_instance_t *instance_list[2] = { nullptr, nullptr };
    wasm_instance_vec_t instance_src = { 0, nullptr, 0, 0, nullptr };
    wasm_instance_vec_new(&instance_src, 2, instance_list);
    ASSERT_EQ(2u, instance_src.num_elems);

    wasm_instance_vec_t instance_out = { 0, nullptr, 0, 0, nullptr };
    wasm_instance_vec_copy(&instance_out, &instance_src);
    EXPECT_EQ(0u, instance_out.size);
    EXPECT_EQ(nullptr, instance_out.data);
    wasm_instance_vec_delete(&instance_src);

    wasm_module_t *module_list[2] = { nullptr, nullptr };
    wasm_module_vec_t module_src = { 0, nullptr, 0, 0, nullptr };
    wasm_module_vec_new(&module_src, 2, module_list);
    ASSERT_EQ(2u, module_src.num_elems);

    wasm_module_vec_t module_out = { 0, nullptr, 0, 0, nullptr };
    wasm_module_vec_copy(&module_out, &module_src);
    EXPECT_EQ(0u, module_out.size);
    EXPECT_EQ(nullptr, module_out.data);
    wasm_module_vec_delete(&module_src);

    wasm_store_t *store_list[2] = { nullptr, nullptr };
    wasm_store_vec_t store_src = { 0, nullptr, 0, 0, nullptr };
    wasm_store_vec_new(&store_src, 2, store_list);
    ASSERT_EQ(2u, store_src.num_elems);

    wasm_store_vec_t store_out = { 0, nullptr, 0, 0, nullptr };
    wasm_store_vec_copy(&store_out, &store_src);
    EXPECT_EQ(0u, store_out.size);
    EXPECT_EQ(nullptr, store_out.data);
    wasm_store_vec_delete(&store_src);
}

} // namespace

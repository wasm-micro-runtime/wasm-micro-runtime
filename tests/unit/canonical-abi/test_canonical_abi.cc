/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "gtest/gtest.h"
#include <limits>
#include <cmath>
#include <cstring>
#include "wasm_export.h"

extern "C" {
#include "wasm_canonical_abi.h"
#include "wasm_runtime_common.h"
#include "wasm_runtime.h"
}

#define HEAP_SIZE (100 * 1024 * 1024) // 100 MB

class CanonicalAbiTest : public testing::Test
{
  public:
    CanonicalAbiTest(){}
    ~CanonicalAbiTest() {}
    RuntimeInitArgs init_args;

    uint32_t wasm_file_size = 0;
    uint32_t stack_size = 16 * 1024; // 16 KB
    uint32_t heap_size = HEAP_SIZE; // 100 MB

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB
    virtual void SetUp() {
        memset(&init_args, 0, sizeof(RuntimeInitArgs));

        init_args.mem_alloc_type = Alloc_With_Pool;
        init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
        init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

        wasm_runtime_full_init(&init_args);
    }

    virtual void TearDown() {
        wasm_runtime_destroy();
    }
};

TEST_F(CanonicalAbiTest, BoolCtor) {
    wit_value_t val_true = wit_bool_ctor(true);
    ASSERT_NE(val_true, nullptr);
    ASSERT_EQ(val_true->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val_true->prim_type, WASM_COMP_PRIMVAL_BOOL);
    ASSERT_TRUE(val_true->value.bool_value);
    ASSERT_TRUE(free_wit_value(val_true));

    wit_value_t val_false = wit_bool_ctor(false);
    ASSERT_NE(val_false, nullptr);
    ASSERT_EQ(val_false->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val_false->prim_type, WASM_COMP_PRIMVAL_BOOL);
    ASSERT_FALSE(val_false->value.bool_value);
    ASSERT_TRUE(free_wit_value(val_false));
}

TEST_F(CanonicalAbiTest, S8Ctor) {
    int8_t test_val = -42;
    wit_value_t val = wit_s8_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(val->value.s8_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s8_ctor(std::numeric_limits<int8_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(val->value.s8_value, std::numeric_limits<int8_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s8_ctor(std::numeric_limits<int8_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(val->value.s8_value, std::numeric_limits<int8_t>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s8_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S8);
    ASSERT_EQ(val->value.s8_value, 0);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, S16Ctor) {
    int16_t test_val = -2048;
    wit_value_t val = wit_s16_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S16);
    ASSERT_EQ(val->value.s16_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s16_ctor(std::numeric_limits<int16_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S16);
    ASSERT_EQ(val->value.s16_value, std::numeric_limits<int16_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s16_ctor(std::numeric_limits<int16_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S16);
    ASSERT_EQ(val->value.s16_value, std::numeric_limits<int16_t>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s16_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S16);
    ASSERT_EQ(val->value.s16_value, 0);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, S32Ctor) {
    int32_t test_val = -100000;
    wit_value_t val = wit_s32_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(val->value.s32_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s32_ctor(std::numeric_limits<int32_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(val->value.s32_value, std::numeric_limits<int32_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s32_ctor(std::numeric_limits<int32_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(val->value.s32_value, std::numeric_limits<int32_t>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s32_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(val->value.s32_value, 0);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, S64Ctor) {
    int64_t test_val = -5000000000;
    wit_value_t val = wit_s64_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S64);
    ASSERT_EQ(val->value.s64_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s64_ctor(std::numeric_limits<int64_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S64);
    ASSERT_EQ(val->value.s64_value, std::numeric_limits<int64_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s64_ctor(std::numeric_limits<int64_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S64);
    ASSERT_EQ(val->value.s64_value, std::numeric_limits<int64_t>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_s64_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_S64);
    ASSERT_EQ(val->value.s64_value, 0);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, U8Ctor) {
    uint8_t test_val = 200;
    wit_value_t val = wit_u8_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(val->value.u8_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u8_ctor(std::numeric_limits<uint8_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(val->value.u8_value, std::numeric_limits<uint8_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u8_ctor(std::numeric_limits<uint8_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U8);
    ASSERT_EQ(val->value.u8_value, std::numeric_limits<uint8_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, U16Ctor) {
    uint16_t test_val = 40000;
    wit_value_t val = wit_u16_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U16);
    ASSERT_EQ(val->value.u16_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u16_ctor(std::numeric_limits<uint16_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U16);
    ASSERT_EQ(val->value.u16_value, std::numeric_limits<uint16_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u16_ctor(std::numeric_limits<uint16_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U16);
    ASSERT_EQ(val->value.u16_value, std::numeric_limits<uint16_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, U32Ctor) {
    uint32_t test_val = 3000000000;
    wit_value_t val = wit_u32_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(val->value.u32_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u32_ctor(std::numeric_limits<uint32_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(val->value.u32_value, std::numeric_limits<uint32_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u32_ctor(std::numeric_limits<uint32_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U32);
    ASSERT_EQ(val->value.u32_value, std::numeric_limits<uint32_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, U64Ctor) {
    uint64_t test_val = 10000000000;
    wit_value_t val = wit_u64_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(val->value.u64_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u64_ctor(std::numeric_limits<uint64_t>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(val->value.u64_value, std::numeric_limits<uint64_t>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_u64_ctor(std::numeric_limits<uint64_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_U64);
    ASSERT_EQ(val->value.u64_value, std::numeric_limits<uint64_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, F32Ctor) {
    float test_val = 3.14f;
    wit_value_t val = wit_f32_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(val->value.f32_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(std::numeric_limits<float>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(val->value.f32_value, std::numeric_limits<float>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(std::numeric_limits<float>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(val->value.f32_value, std::numeric_limits<float>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(0.0f);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_FLOAT_EQ(val->value.f32_value, 0.0f);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(std::numeric_limits<float>::infinity());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_TRUE(std::isinf(val->value.f32_value));
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(-std::numeric_limits<float>::infinity());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_TRUE(std::isinf(val->value.f32_value));
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f32_ctor(std::numeric_limits<float>::quiet_NaN());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F32);
    ASSERT_TRUE(std::isnan(val->value.f32_value));
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, F64Ctor) {
    double test_val = 3.1415926535;
    wit_value_t val = wit_f64_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_DOUBLE_EQ(val->value.f64_value, test_val);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(std::numeric_limits<double>::min());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_DOUBLE_EQ(val->value.f64_value, std::numeric_limits<double>::min());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(std::numeric_limits<double>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_DOUBLE_EQ(val->value.f64_value, std::numeric_limits<double>::max());
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(0.0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_DOUBLE_EQ(val->value.f64_value, 0.0);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(std::numeric_limits<double>::infinity());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_TRUE(std::isinf(val->value.f64_value));
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(-std::numeric_limits<double>::infinity());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_TRUE(std::isinf(val->value.f64_value));
    ASSERT_TRUE(free_wit_value(val));

    val = wit_f64_ctor(std::numeric_limits<double>::quiet_NaN());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_TRUE(std::isnan(val->value.f64_value));
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, CharCtor) {
    char test_val = 'a';
    wit_value_t val = wit_char_ctor((uint32_t)test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_CHAR);
    ASSERT_EQ(val->value.char_value, test_val);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, FreeNullType) {
    wit_value_t val = NULL;
    ASSERT_FALSE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, ListCtorEmpty) {
    wit_value_t list = wit_list_ctor(NULL, 0);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(list->value.list_value.size, 0);
    ASSERT_EQ(list->value.list_value.elems, nullptr);
    ASSERT_TRUE(free_wit_value(list));
}

TEST_F(CanonicalAbiTest, ListCtorSingleElement) {
    wit_value_t* single_elem = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t));
    single_elem[0] = wit_s32_ctor(1);
    wit_value_t list = wit_list_ctor(single_elem, 1);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(list->value.list_value.size, 1);
    ASSERT_NE(list->value.list_value.elems, nullptr);
    ASSERT_EQ(list->value.list_value.elems[0], single_elem[0]);
    ASSERT_TRUE(free_wit_value(list));
}

TEST_F(CanonicalAbiTest, ListCtorMultipleElements) {
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    wit_value_t list = wit_list_ctor(elems, 2);
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(list->value.list_value.size, 2);
    ASSERT_NE(list->value.list_value.elems, nullptr);
    ASSERT_EQ(list->value.list_value.elems[0], elems[0]);
    ASSERT_EQ(list->value.list_value.elems[1], elems[1]);
    ASSERT_TRUE(free_wit_value(list));
}

TEST_F(CanonicalAbiTest, ListCtorMixedTypesFailure) {
    wit_value_t* mixed_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    mixed_elems[0] = wit_s32_ctor(1);
    mixed_elems[1] = wit_s8_ctor(2);
    wit_value_t list = wit_list_ctor(mixed_elems, 2);
    ASSERT_EQ(list, nullptr);
    free_wit_value(mixed_elems[0]);
    free_wit_value(mixed_elems[1]);
}

TEST_F(CanonicalAbiTest, ListCtorListOfLists) {
    wit_value_t* inner_elems1 = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    inner_elems1[0] = wit_s32_ctor(1);
    inner_elems1[1] = wit_s32_ctor(2);
    wit_value_t inner_list1 = wit_list_ctor(inner_elems1, 2);

    wit_value_t* inner_elems2 = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    inner_elems2[0] = wit_s32_ctor(3);
    inner_elems2[1] = wit_s32_ctor(4);
    wit_value_t inner_list2 = wit_list_ctor(inner_elems2, 2);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = inner_list1;
    outer_elems[1] = inner_list2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_NE(outer_list, nullptr);
    ASSERT_EQ(outer_list->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(outer_list->value.list_value.size, 2);
    ASSERT_NE(outer_list->value.list_value.elems, nullptr);
    ASSERT_EQ(outer_list->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(outer_list->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_LIST);

    wit_value_t retrieved_inner_list1 = outer_list->value.list_value.elems[0];
    ASSERT_EQ(retrieved_inner_list1->value.list_value.size, 2);
    ASSERT_EQ(retrieved_inner_list1->value.list_value.elems[0]->value.s32_value, 1);
    ASSERT_EQ(retrieved_inner_list1->value.list_value.elems[1]->value.s32_value, 2);

    wit_value_t retrieved_inner_list2 = outer_list->value.list_value.elems[1];
    ASSERT_EQ(retrieved_inner_list2->value.list_value.size, 2);
    ASSERT_EQ(retrieved_inner_list2->value.list_value.elems[0]->value.s32_value, 3);
    ASSERT_EQ(retrieved_inner_list2->value.list_value.elems[1]->value.s32_value, 4);

    ASSERT_TRUE(free_wit_value(outer_list));
}

TEST_F(CanonicalAbiTest, ListCtorListOfListsMixedTypesFailure) {
    wit_value_t* inner_elems1 = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    inner_elems1[0] = wit_s32_ctor(1);
    inner_elems1[1] = wit_s32_ctor(2);
    wit_value_t inner_list1 = wit_list_ctor(inner_elems1, 2);

    wit_value_t* inner_elems2 = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    inner_elems2[0] = wit_s8_ctor(3);
    inner_elems2[1] = wit_s8_ctor(4);
    wit_value_t inner_list2 = wit_list_ctor(inner_elems2, 2);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = inner_list1;
    outer_elems[1] = inner_list2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_EQ(outer_list, nullptr);
    free_wit_value(inner_list1);
    free_wit_value(inner_list2);
}

TEST_F(CanonicalAbiTest, ListCtorListOfRecords) {
    ComponentWITRecordField* fields1 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_s32";
    init_record_field(&fields1[0], key1, strlen(key1), wit_s32_ctor(1));
    wit_value_t record1 = wit_record_ctor(fields1, 1);

    ComponentWITRecordField* fields2 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key2[] = "my_s32";
    init_record_field(&fields2[0], key2, strlen(key2), wit_s32_ctor(2));
    wit_value_t record2 = wit_record_ctor(fields2, 1);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = record1;
    outer_elems[1] = record2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_NE(outer_list, nullptr);
    ASSERT_EQ(outer_list->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(outer_list->value.list_value.size, 2);
    ASSERT_NE(outer_list->value.list_value.elems, nullptr);
    ASSERT_EQ(outer_list->value.list_value.elems[0]->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(outer_list->value.list_value.elems[1]->type, COMPONENT_VAL_TYPE_RECORD);

    wit_value_t retrieved_record1 = outer_list->value.list_value.elems[0];
    ASSERT_EQ(retrieved_record1->value.record_value.size, 1);
    ASSERT_STREQ(retrieved_record1->value.record_value.fields[0].key, "my_s32");
    ASSERT_EQ(retrieved_record1->value.record_value.fields[0].value->value.s32_value, 1);

    wit_value_t retrieved_record2 = outer_list->value.list_value.elems[1];
    ASSERT_EQ(retrieved_record2->value.record_value.size, 1);
    ASSERT_STREQ(retrieved_record2->value.record_value.fields[0].key, "my_s32");
    ASSERT_EQ(retrieved_record2->value.record_value.fields[0].value->value.s32_value, 2);

    ASSERT_TRUE(free_wit_value(outer_list));
}

TEST_F(CanonicalAbiTest, ListCtorListOfRecordsMixedFieldNamesFailure) {
    ComponentWITRecordField* fields1 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_s32";
    init_record_field(&fields1[0], key1, strlen(key1), wit_s32_ctor(1));
    wit_value_t record1 = wit_record_ctor(fields1, 1);

    ComponentWITRecordField* fields2 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key2[] = "my_s32_different";
    init_record_field(&fields2[0], key2, strlen(key2), wit_s32_ctor(2));
    wit_value_t record2 = wit_record_ctor(fields2, 1);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = record1;
    outer_elems[1] = record2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_EQ(outer_list, nullptr);
    free_wit_value(record1);
    free_wit_value(record2);
}

TEST_F(CanonicalAbiTest, ListCtorListOfRecordsMixedFieldTypesFailure) {
    ComponentWITRecordField* fields1 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_field";
    init_record_field(&fields1[0], key1, strlen(key1), wit_s32_ctor(1));
    wit_value_t record1 = wit_record_ctor(fields1, 1);

    ComponentWITRecordField* fields2 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key2[] = "my_field";
    init_record_field(&fields2[0], key2, strlen(key2), wit_s8_ctor(2));
    wit_value_t record2 = wit_record_ctor(fields2, 1);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = record1;
    outer_elems[1] = record2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_EQ(outer_list, nullptr);
    free_wit_value(record1);
    free_wit_value(record2);
}

TEST_F(CanonicalAbiTest, ListCtorListOfRecordsMixedFieldCountsFailure) {
    ComponentWITRecordField* fields1 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_s32";
    init_record_field(&fields1[0], key1, strlen(key1), wit_s32_ctor(1));
    wit_value_t record1 = wit_record_ctor(fields1, 1);

    ComponentWITRecordField* fields2 = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField) * 2);
    char key2[] = "my_s32";
    char key3[] = "my_s8";
    init_record_field(&fields2[0], key2, strlen(key2), wit_s32_ctor(2));
    init_record_field(&fields2[1], key3, strlen(key3), wit_s8_ctor(3));
    wit_value_t record2 = wit_record_ctor(fields2, 2);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    outer_elems[0] = record1;
    outer_elems[1] = record2;
    wit_value_t outer_list = wit_list_ctor(outer_elems, 2);

    ASSERT_EQ(outer_list, nullptr);
    free_wit_value(record1);
    free_wit_value(record2);
}

TEST_F(CanonicalAbiTest, OptionCtorNone) {
    wit_value_t option = wit_option_ctor(NULL);
    ASSERT_NE(option, nullptr);
    ASSERT_EQ(option->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_EQ(option->value.option_value.optional_elem, nullptr);
    ASSERT_TRUE(free_wit_value(option));
}

TEST_F(CanonicalAbiTest, OptionCtorSomeS32) {
    wit_value_t s32_val = wit_s32_ctor(42);
    wit_value_t option = wit_option_ctor(s32_val);
    ASSERT_NE(option, nullptr);
    ASSERT_EQ(option->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(option->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(option->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(option->value.option_value.optional_elem->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(option->value.option_value.optional_elem->value.s32_value, 42);
    ASSERT_TRUE(free_wit_value(option));
}

TEST_F(CanonicalAbiTest, OptionCtorSomeF64) {
    wit_value_t f64_val = wit_f64_ctor(3.14159);
    wit_value_t option = wit_option_ctor(f64_val);
    ASSERT_NE(option, nullptr);
    ASSERT_EQ(option->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(option->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(option->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(option->value.option_value.optional_elem->prim_type, WASM_COMP_PRIMVAL_F64);
    ASSERT_DOUBLE_EQ(option->value.option_value.optional_elem->value.f64_value, 3.14159);
    ASSERT_TRUE(free_wit_value(option));
}

TEST_F(CanonicalAbiTest, OptionCtorSomeList) {
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    wit_value_t list = wit_list_ctor(elems, 2);
    wit_value_t option = wit_option_ctor(list);
    ASSERT_NE(option, nullptr);
    ASSERT_EQ(option->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(option->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(option->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(option->value.option_value.optional_elem->value.list_value.size, 2);
    ASSERT_TRUE(free_wit_value(option));
}

TEST_F(CanonicalAbiTest, OptionCtorSomeNestedOption) {
    wit_value_t s32_val = wit_s32_ctor(42);
    wit_value_t inner_option = wit_option_ctor(s32_val);
    wit_value_t outer_option = wit_option_ctor(inner_option);
    ASSERT_NE(outer_option, nullptr);
    ASSERT_EQ(outer_option->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(outer_option->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(outer_option->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_NE(outer_option->value.option_value.optional_elem->value.option_value.optional_elem, nullptr);
    ASSERT_EQ(outer_option->value.option_value.optional_elem->value.option_value.optional_elem->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(outer_option->value.option_value.optional_elem->value.option_value.optional_elem->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(outer_option->value.option_value.optional_elem->value.option_value.optional_elem->value.s32_value, 42);
    ASSERT_TRUE(free_wit_value(outer_option));
}

TEST_F(CanonicalAbiTest, ResultOk) {
    wit_value_t ok_val = wit_s32_ctor(42);
    bool is_err = false;
    wit_value_t result = wit_result_ctor(is_err, ok_val);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_FALSE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.ok, ok_val);
    ASSERT_EQ(result->value.result_value.result.ok->value.s32_value, 42);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, ResultErr) {
    wit_value_t err_val = wit_s32_ctor(-1);
    bool is_err = true;
    wit_value_t result = wit_result_ctor(is_err, err_val);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_TRUE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.err, err_val);
    ASSERT_EQ(result->value.result_value.result.err->value.s32_value, -1);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, ResultOkNull) {
    bool is_err = false;
    wit_value_t result = wit_result_ctor(is_err, NULL);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_FALSE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.ok, nullptr);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, ResultErrNull) {
    bool is_err = true;
    wit_value_t result = wit_result_ctor(is_err, NULL);
    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_TRUE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.err, nullptr);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, ResultOkWithList) {
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    wit_value_t list = wit_list_ctor(elems, 2);
    bool is_err = false;
    wit_value_t result = wit_result_ctor(is_err, list);

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_FALSE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.ok->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.result_value.result.ok->value.list_value.size, 2);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, ResultErrWithList) {
   wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    wit_value_t list = wit_list_ctor(elems, 2);
    bool is_err = true;
    wit_value_t result = wit_result_ctor(is_err, list);

    ASSERT_NE(result, nullptr);
    ASSERT_EQ(result->type, COMPONENT_VAL_TYPE_RESULT);
    ASSERT_TRUE(result->value.result_value.is_err);
    ASSERT_EQ(result->value.result_value.result.err->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(result->value.result_value.result.err->value.list_value.size, 2);
    ASSERT_TRUE(free_wit_value(result));
}

TEST_F(CanonicalAbiTest, RecordCtorEmpty) {
    wit_value_t record = wit_record_ctor(NULL, 0);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(record->value.record_value.size, 0);
    ASSERT_EQ(record->value.record_value.fields, nullptr);
    ASSERT_TRUE(free_wit_value(record));
}

TEST_F(CanonicalAbiTest, RecordCtorSingleElement) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_s32";
    init_record_field(&fields[0], key1, strlen(key1), wit_s32_ctor(1));
    wit_value_t record = wit_record_ctor(fields, 1);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(record->value.record_value.size, 1);
    ASSERT_NE(record->value.record_value.fields, nullptr);
    ASSERT_STREQ(record->value.record_value.fields[0].key, "my_s32");
    ASSERT_EQ(record->value.record_value.fields[0].value->value.s32_value, 1);
    ASSERT_TRUE(free_wit_value(record));
}

TEST_F(CanonicalAbiTest, RecordCtorMultipleElements) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField) * 2);
    char key1[] = "my_s32";
    init_record_field(&fields[0], key1, strlen(key1), wit_s32_ctor(1));
    char key2[] = "my_f64";
    init_record_field(&fields[1], key2, strlen(key2), wit_f64_ctor(3.14));
    wit_value_t record = wit_record_ctor(fields, 2);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(record->value.record_value.size, 2);
    ASSERT_NE(record->value.record_value.fields, nullptr);
    ASSERT_STREQ(record->value.record_value.fields[0].key, "my_s32");
    ASSERT_EQ(record->value.record_value.fields[0].value->value.s32_value, 1);
    ASSERT_STREQ(record->value.record_value.fields[1].key, "my_f64");
    ASSERT_DOUBLE_EQ(record->value.record_value.fields[1].value->value.f64_value, 3.14);
    ASSERT_TRUE(free_wit_value(record));
}

TEST_F(CanonicalAbiTest, RecordCtorNullFieldsFailure) {
    wit_value_t record = wit_record_ctor(NULL, 1);
    ASSERT_EQ(record, nullptr);
}

TEST_F(CanonicalAbiTest, RecordCtorWithOption) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_option";
    init_record_field(&fields[0], key1, strlen(key1), wit_option_ctor(wit_s32_ctor(1)));
    wit_value_t record = wit_record_ctor(fields, 1);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(record->value.record_value.size, 1);
    ASSERT_NE(record->value.record_value.fields, nullptr);
    ASSERT_STREQ(record->value.record_value.fields[0].key, "my_option");
    ASSERT_EQ(record->value.record_value.fields[0].value->type, COMPONENT_VAL_TYPE_OPTION);
    ASSERT_EQ(record->value.record_value.fields[0].value->value.option_value.optional_elem->value.s32_value, 1);
    ASSERT_TRUE(free_wit_value(record));
}

TEST_F(CanonicalAbiTest, RecordCtorWithList) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    char key1[] = "my_list";
    init_record_field(&fields[0], key1, strlen(key1), wit_list_ctor(elems, 2));
    wit_value_t record = wit_record_ctor(fields, 1);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->type, COMPONENT_VAL_TYPE_RECORD);
    ASSERT_EQ(record->value.record_value.size, 1);
    ASSERT_NE(record->value.record_value.fields, nullptr);
    ASSERT_STREQ(record->value.record_value.fields[0].key, "my_list");
    ASSERT_EQ(record->value.record_value.fields[0].value->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(record->value.record_value.fields[0].value->value.list_value.size, 2);
    ASSERT_TRUE(free_wit_value(record));
}

TEST_F(CanonicalAbiTest, TupleCtorEmpty) {
    wit_value_t tuple = wit_tuple_ctor(NULL, 0);
    ASSERT_NE(tuple, nullptr);
    ASSERT_EQ(tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    ASSERT_EQ(tuple->value.tuple_value.size, 0);
    ASSERT_EQ(tuple->value.tuple_value.elems, nullptr);
    ASSERT_TRUE(free_wit_value(tuple));
}

TEST_F(CanonicalAbiTest, TupleCtorSingleElement) {
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t));
    elems[0] = wit_s32_ctor(1);
    wit_value_t tuple = wit_tuple_ctor(elems, 1);
    ASSERT_NE(tuple, nullptr);
    ASSERT_EQ(tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    ASSERT_EQ(tuple->value.tuple_value.size, 1);
    ASSERT_NE(tuple->value.tuple_value.elems, nullptr);
    ASSERT_EQ(tuple->value.tuple_value.elems[0]->value.s32_value, 1);
    ASSERT_TRUE(free_wit_value(tuple));
}

TEST_F(CanonicalAbiTest, TupleCtorMultipleElements) {
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_f64_ctor(3.14);
    wit_value_t tuple = wit_tuple_ctor(elems, 2);
    ASSERT_NE(tuple, nullptr);
    ASSERT_EQ(tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    ASSERT_EQ(tuple->value.tuple_value.size, 2);
    ASSERT_NE(tuple->value.tuple_value.elems, nullptr);
    ASSERT_EQ(tuple->value.tuple_value.elems[0]->value.s32_value, 1);
    ASSERT_DOUBLE_EQ(tuple->value.tuple_value.elems[1]->value.f64_value, 3.14);
    ASSERT_TRUE(free_wit_value(tuple));
}

TEST_F(CanonicalAbiTest, TupleCtorNullElemsFailure) {
    wit_value_t tuple = wit_tuple_ctor(NULL, 1);
    ASSERT_EQ(tuple, nullptr);
}

TEST_F(CanonicalAbiTest, TupleCtorWithNestedTuple) {
    wit_value_t* inner_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t));
    inner_elems[0] = wit_s32_ctor(1);
    wit_value_t inner_tuple = wit_tuple_ctor(inner_elems, 1);

    wit_value_t* outer_elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t));
    outer_elems[0] = inner_tuple;
    wit_value_t outer_tuple = wit_tuple_ctor(outer_elems, 1);

    ASSERT_NE(outer_tuple, nullptr);
    ASSERT_EQ(outer_tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    ASSERT_EQ(outer_tuple->value.tuple_value.size, 1);
    ASSERT_NE(outer_tuple->value.tuple_value.elems, nullptr);
    
    wit_value_t retrieved_inner_tuple = outer_tuple->value.tuple_value.elems[0];
    ASSERT_EQ(retrieved_inner_tuple->type, COMPONENT_VAL_TYPE_TUPLE);
    ASSERT_EQ(retrieved_inner_tuple->value.tuple_value.size, 1);
    ASSERT_EQ(retrieved_inner_tuple->value.tuple_value.elems[0]->value.s32_value, 1);

    ASSERT_TRUE(free_wit_value(outer_tuple));
}

TEST_F(CanonicalAbiTest, EnumCtor)
{
    uint32_t test_val = 42;
    wit_value_t val = wit_enum_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(val->value.enum_value.value, test_val);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, EnumCtorEdgeCases)
{
    wit_value_t val = wit_enum_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(val->value.enum_value.value, 0);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_enum_ctor(std::numeric_limits<uint32_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_ENUM);
    ASSERT_EQ(val->value.enum_value.value, std::numeric_limits<uint32_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, VariantCtorCaseWithValue)
{
    char discriminator[] = "discriminator_1";
    wit_value_t s32_val = wit_s32_ctor(123);
    wit_value_t variant = wit_variant_ctor(discriminator, strlen(discriminator), s32_val);

    ASSERT_NE(variant, nullptr);
    ASSERT_EQ(variant->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(variant->value.variant_value.discriminator, discriminator);
    ASSERT_NE(variant->value.variant_value.value, nullptr);
    ASSERT_EQ(variant->value.variant_value.value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(variant->value.variant_value.value->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(variant->value.variant_value.value->value.s32_value, 123);

    ASSERT_TRUE(free_wit_value(variant));
}

TEST_F(CanonicalAbiTest, VariantCtorCaseWithoutValue)
{
    char discriminator[] = "discriminator_1";
    wit_value_t variant = wit_variant_ctor(discriminator, strlen(discriminator), NULL);

    ASSERT_NE(variant, nullptr);
    ASSERT_EQ(variant->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(variant->value.variant_value.discriminator, discriminator);
    ASSERT_EQ(variant->value.variant_value.value, nullptr);

    ASSERT_TRUE(free_wit_value(variant));
}

TEST_F(CanonicalAbiTest, VariantCtorWithComplexType)
{
    char discriminator[] = "discriminator_1";
    wit_value_t* elems = (wit_value_t*)wasm_runtime_malloc(sizeof(wit_value_t) * 2);
    elems[0] = wit_s32_ctor(1);
    elems[1] = wit_s32_ctor(2);
    wit_value_t list = wit_list_ctor(elems, 2);
    wit_value_t variant = wit_variant_ctor(discriminator, strlen(discriminator), list);

    ASSERT_NE(variant, nullptr);
    ASSERT_EQ(variant->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(variant->value.variant_value.discriminator, discriminator);
    ASSERT_NE(variant->value.variant_value.value, nullptr);
    ASSERT_EQ(variant->value.variant_value.value->type, COMPONENT_VAL_TYPE_LIST);
    ASSERT_EQ(variant->value.variant_value.value->value.list_value.size, 2);

    ASSERT_TRUE(free_wit_value(variant));
}

TEST_F(CanonicalAbiTest, VariantCtorWithNestedVariant)
{
    char inner_discriminator[] = "inner_discriminator";
    wit_value_t s32_val = wit_s32_ctor(42);
    wit_value_t inner_variant = wit_variant_ctor(inner_discriminator, strlen(inner_discriminator), s32_val);

    char outer_discriminator[] = "outer_discriminator";
    wit_value_t outer_variant = wit_variant_ctor(outer_discriminator, strlen(outer_discriminator), inner_variant);

    ASSERT_NE(outer_variant, nullptr);
    ASSERT_EQ(outer_variant->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(outer_variant->value.variant_value.discriminator, outer_discriminator);
    ASSERT_NE(outer_variant->value.variant_value.value, nullptr);

    wit_value_t retrieved_inner_variant = outer_variant->value.variant_value.value;
    ASSERT_EQ(retrieved_inner_variant->type, COMPONENT_VAL_TYPE_VARIANT);
    ASSERT_STREQ(retrieved_inner_variant->value.variant_value.discriminator, inner_discriminator);
    ASSERT_NE(retrieved_inner_variant->value.variant_value.value, nullptr);
    ASSERT_EQ(retrieved_inner_variant->value.variant_value.value->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(retrieved_inner_variant->value.variant_value.value->prim_type, WASM_COMP_PRIMVAL_S32);
    ASSERT_EQ(retrieved_inner_variant->value.variant_value.value->value.s32_value, 42);

    ASSERT_TRUE(free_wit_value(outer_variant));
}

TEST_F(CanonicalAbiTest, ResourceCtor)
{
    uint32_t test_val = 42;
    wit_value_t val = wit_resource_ctor(test_val);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_RESOURCE_SYNC);
    ASSERT_EQ(val->value.resource_value.value, test_val);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, ResourceCtorEdgeCases)
{
    wit_value_t val = wit_resource_ctor(0);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_RESOURCE_SYNC);
    ASSERT_EQ(val->value.resource_value.value, 0);
    ASSERT_TRUE(free_wit_value(val));

    val = wit_resource_ctor(std::numeric_limits<uint32_t>::max());
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_RESOURCE_SYNC);
    ASSERT_EQ(val->value.resource_value.value, std::numeric_limits<uint32_t>::max());
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, StringCtorSuccess) {
    char test_str[] = "hello world";
    uint32_t size = strlen(test_str);
    wit_value_t val = wit_string_ctor(test_str, size, size, ENCODING_UTF_8);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_EQ(val->value.string_value.size_bytes, size);
    ASSERT_EQ(val->value.string_value.hint_encoding, ENCODING_UTF_8);
    ASSERT_STREQ(val->value.string_value.chars, "hello world");
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, StringCtorEmpty) {
    char test_str[] = "";
    uint32_t size = strlen(test_str);
    wit_value_t val = wit_string_ctor(test_str, size, size, ENCODING_UTF_8);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_EQ(val->value.string_value.size_bytes, 0);
    ASSERT_EQ(val->value.string_value.hint_encoding, ENCODING_UTF_8);
    ASSERT_STREQ(val->value.string_value.chars, "");
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, StringCtorUTF8) {
    char test_str[] = "你好, world";
    uint32_t size = strlen(test_str);
    wit_value_t val = wit_string_ctor(test_str, size, size, ENCODING_UTF_8);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_EQ(val->value.string_value.size_bytes, size);
    ASSERT_EQ(val->value.string_value.hint_encoding, ENCODING_UTF_8);
    ASSERT_STREQ(val->value.string_value.chars, "你好, world");
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, StringCtorNullFailure) {
    wit_value_t val = wit_string_ctor(NULL, 1, 1, ENCODING_UTF_8);
    ASSERT_EQ(val, nullptr);
}

TEST_F(CanonicalAbiTest, StringCtorLarge) {
    const uint32_t size = 1024 * 1024; // 1MB
    char* test_str = (char*)malloc(size + 1);
    ASSERT_NE(test_str, nullptr);
    for (uint32_t i = 0; i < size; ++i) {
        test_str[i] = 'a' + (i % 26);
    }
    test_str[size] = '\0';

    wit_value_t val = wit_string_ctor(test_str, size, size, ENCODING_UTF_8);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->type, COMPONENT_VAL_TYPE_PRIMVAL);
    ASSERT_EQ(val->prim_type, WASM_COMP_PRIMVAL_STRING);
    ASSERT_EQ(val->value.string_value.size_bytes, size);
    ASSERT_EQ(val->value.string_value.hint_encoding, ENCODING_UTF_8);
    ASSERT_EQ(strncmp(val->value.string_value.chars, test_str, size), 0);
    ASSERT_TRUE(free_wit_value(val));
}

TEST_F(CanonicalAbiTest, FlagCtorEmpty) {
    wit_value_t flag = wit_flag_ctor(NULL, 0);
    ASSERT_NE(flag, nullptr);
    ASSERT_EQ(flag->type, COMPONENT_VAL_TYPE_FLAGS);
    ASSERT_EQ(flag->value.flag_value.size, 0);
    ASSERT_EQ(flag->value.flag_value.fields, nullptr);
    ASSERT_TRUE(free_wit_value(flag));
}

TEST_F(CanonicalAbiTest, FlagCtorSingleElement) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_bool";
    init_record_field(&fields[0], key1, strlen(key1), wit_bool_ctor(true));
    wit_value_t flag = wit_flag_ctor(fields, 1);
    ASSERT_NE(flag, nullptr);
    ASSERT_EQ(flag->type, COMPONENT_VAL_TYPE_FLAGS);
    ASSERT_EQ(flag->value.flag_value.size, 1);
    ASSERT_NE(flag->value.flag_value.fields, nullptr);
    ASSERT_STREQ(flag->value.flag_value.fields[0].key, "my_bool");
    ASSERT_TRUE(flag->value.flag_value.fields[0].value->value.bool_value);
    ASSERT_TRUE(free_wit_value(flag));
}

TEST_F(CanonicalAbiTest, FlagCtorMultipleElements) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField) * 2);
    char key1[] = "my_bool1";
    init_record_field(&fields[0], key1, strlen(key1), wit_bool_ctor(true));
    char key2[] = "my_bool2";
    init_record_field(&fields[1], key2, strlen(key2), wit_bool_ctor(false));
    wit_value_t flag = wit_flag_ctor(fields, 2);
    ASSERT_NE(flag, nullptr);
    ASSERT_EQ(flag->type, COMPONENT_VAL_TYPE_FLAGS);
    ASSERT_EQ(flag->value.flag_value.size, 2);
    ASSERT_NE(flag->value.flag_value.fields, nullptr);
    ASSERT_STREQ(flag->value.flag_value.fields[0].key, "my_bool1");
    ASSERT_TRUE(flag->value.flag_value.fields[0].value->value.bool_value);
    ASSERT_STREQ(flag->value.flag_value.fields[1].key, "my_bool2");
    ASSERT_FALSE(flag->value.flag_value.fields[1].value->value.bool_value);
    ASSERT_TRUE(free_wit_value(flag));
}

TEST_F(CanonicalAbiTest, FlagCtorNullFieldsFailure) {
    wit_value_t flag = wit_flag_ctor(NULL, 1);
    ASSERT_EQ(flag, nullptr);
}

TEST_F(CanonicalAbiTest, FlagCtorNonNullFieldsAndZeroSizeFailure) {
    ComponentWITRecordField* fields = (ComponentWITRecordField*)wasm_runtime_malloc(sizeof(ComponentWITRecordField));
    char key1[] = "my_bool";
    init_record_field(&fields[0], key1, strlen(key1), wit_bool_ctor(true));
    wit_value_t flag = wit_flag_ctor(fields, 0);
    ASSERT_EQ(flag, nullptr);

    wasm_runtime_free(fields[0].key);
    free_wit_value(fields[0].value);
    wasm_runtime_free(fields);
}
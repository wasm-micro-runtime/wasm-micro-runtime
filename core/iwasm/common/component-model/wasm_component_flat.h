/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_FLAT_H
#define WASM_COMPONENT_FLAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wasm_component_canonical.h"

#define MAX_FLAT_PARAMS 16
#define MAX_FLAT_ASYNC_PARAMS 4
#define MAX_FLAT_RESULTS 1
#define MAX_FLAT_TYPES 32

typedef enum {
    CORE_TYPE_I32,
    CORE_TYPE_I64,
    CORE_TYPE_F32,
    CORE_TYPE_F64
} CoreValType;

typedef enum { FLATTEN_CONTEXT_LIFT, FLATTEN_CONTEXT_LOWER } FlattenContext;

typedef struct {
    CoreValType type;
    union {
        uint32_t i32;
        uint64_t i64;
        float f32;
        double f64;
    } val;
} CoreValue;

typedef struct {
    CoreValType types[MAX_FLAT_TYPES];
    uint32_t count;
} FlatTypes;

typedef struct CoreValueIter {
    // Normal mode fields
    CoreValue *values;
    uint32_t count;
    uint32_t index;

    // Coercion mode fields (parent == NULL for normal mode)
    struct CoreValueIter *parent; // Original iterator to read from
    FlatTypes *coerce_types;
    uint32_t coerce_index;
} CoreValueIter;

typedef struct {
    CoreValue values[MAX_FLAT_TYPES];
    uint32_t count;
} CoreValueList;

void
cvl_init(CoreValueList *list);
bool
cvl_push_i32(CoreValueList *list, uint32_t v);
bool
cvl_push_i64(CoreValueList *list, uint64_t v);
bool
cvl_push_f32(CoreValueList *list, float v);
bool
cvl_push_f64(CoreValueList *list, double v);
bool
cvl_extend(CoreValueList *dst, const CoreValueList *src);

CoreValue
vi_next(CoreValueIter *vi, CoreValType want);

uint32_t
vi_next_i32(CoreValueIter *vi);
uint64_t
vi_next_i64(CoreValueIter *vi);
float
vi_next_f32(CoreValueIter *vi);
double
vi_next_f64(CoreValueIter *vi);

bool
vi_done(const CoreValueIter *vi);
void
vi_init(CoreValueIter *vi, CoreValue *values, uint32_t count);
void
vi_init_coerce(CoreValueIter *vi, CoreValueIter *parent,
               FlatTypes *coerce_types, uint32_t start_index);

bool
lift_flat(LiftLowerContext *cx, CoreValueIter *vi,
          WASMComponentTypeInstance *type, wit_value_t *out);
bool
lower_flat(LiftLowerContext *cx, WASMComponentTypeInstance *type,
           CoreValueList *out, wit_value_t value);

// Flattening
void
flat_types_init(FlatTypes *ft);
bool
flat_types_push(FlatTypes *ft, CoreValType t);
bool
flat_types_extend(FlatTypes *dst, const FlatTypes *src);

bool
flatten_type(LiftLowerContext *cx, WASMComponentTypeInstance *type,
             FlatTypes *out);
bool
flatten_param_types(LiftLowerContext *cx,
                    WASMComponentParamListInstance *params, FlatTypes *out);
bool
flatten_result_types(LiftLowerContext *cx,
                     WASMComponentResultListInstance *results, FlatTypes *out);
bool
flatten_functype(LiftLowerContext *cx, WASMComponentFuncTypeInstance *ft,
                 FlattenContext context, WASMComponentCoreFuncType *out);

// Lifting and Lowering Values
bool
lift_flat_values(LiftLowerContext *cx, uint32_t max_flat, CoreValueIter *vi,
                 WASMComponentParamListInstance *params,
                 WASMComponentResultListInstance *results, wit_value_t *out);
bool
lower_flat_values(LiftLowerContext *cx, uint32_t max_flat, wit_value_t values,
                  WASMComponentParamListInstance *params,
                  WASMComponentResultListInstance *results,
                  CoreValueIter *out_param, CoreValueList *out);

#ifdef __cplusplus
}
#endif

#endif

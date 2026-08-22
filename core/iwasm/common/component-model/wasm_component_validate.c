/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_validate.h"

#define CONSTRUCTOR_PREFIX "[constructor]"
#define METHOD_PREFIX "[method]"
#define STATIC_PREFIX "[static]"
#define CONSTRUCTOR_PREFIX_LEN 13
#define METHOD_PREFIX_LEN 8
#define STATIC_PREFIX_LEN 8

static bool
defvaltype_subtype(WASMComponentDefValType *a, WASMComponentDefValType *b,
                   WASMComponentValidationContext *ctx);
static bool
defvaltype_equal(WASMComponentDefValType *a, WASMComponentDefValType *b,
                 WASMComponentValidationContext *ctx);

// Grow types[] and type_is_local[] if needed, then record the entry at
// ctx->type_count.
static bool
ctx_push_type(WASMComponentValidationContext *ctx, WASMComponentTypes *type,
              bool is_local, char *error_buf, uint32_t error_buf_size)
{
    if (ctx->type_count >= ctx->types_capacity) {
        uint32_t new_cap =
            ctx->types_capacity == 0 ? 8 : ctx->types_capacity * 2;
        WASMComponentTypes **new_types =
            (WASMComponentTypes **)wasm_runtime_malloc(
                new_cap * sizeof(WASMComponentTypes *));
        bool *new_is_local = wasm_runtime_malloc(new_cap * sizeof(bool));
        if (!new_types || !new_is_local) {
            if (new_types)
                wasm_runtime_free((void *)new_types);

            if (new_is_local)
                wasm_runtime_free(new_is_local);

            set_error_buf_ex(error_buf, error_buf_size,
                             "out of memory allocating type lookup array");
            return false;
        }

        // Copy the existing entries into the new buffers
        if (ctx->types) {
            memcpy((void *)new_types, (const void *)ctx->types,
                   ctx->type_count * sizeof(WASMComponentTypes *));
            memcpy(new_is_local, ctx->type_is_local,
                   ctx->type_count * sizeof(bool));
            wasm_runtime_free((void *)ctx->types);
            wasm_runtime_free(ctx->type_is_local);
        }

        ctx->types = new_types;
        ctx->type_is_local = new_is_local;
        ctx->types_capacity = new_cap;
    }

    // Save type pointer at the current slot. NULL is a valid entry
    ctx->types[ctx->type_count] = type;
    ctx->type_is_local[ctx->type_count] = is_local;
    return true;
}

// Grow value_consumed[] if needed, then record false (not yet consumed) at
// ctx->value_count.
static bool
ctx_push_value(WASMComponentValidationContext *ctx, char *error_buf,
               uint32_t error_buf_size)
{
    if (ctx->value_count >= ctx->value_consumed_capacity) {
        uint32_t new_cap = ctx->value_consumed_capacity == 0
                               ? 8
                               : ctx->value_consumed_capacity * 2;
        bool *new_consumed = wasm_runtime_malloc(new_cap * sizeof(bool));
        if (!new_consumed) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "out of memory allocating value consumed array");
            return false;
        }
        if (ctx->value_consumed) {
            memcpy(new_consumed, ctx->value_consumed,
                   ctx->value_count * sizeof(bool));
            wasm_runtime_free(ctx->value_consumed);
        }
        ctx->value_consumed = new_consumed;
        ctx->value_consumed_capacity = new_cap;
    }
    ctx->value_consumed[ctx->value_count] = false;
    return true;
}

// Grow func_type_indexes[] if needed, then save the type index at
// ctx->func_count.
static bool
ctx_push_func_type(WASMComponentValidationContext *ctx, uint32_t type_idx,
                   char *error_buf, uint32_t error_buf_size)
{
    if (ctx->func_count >= ctx->func_type_indexes_capacity) {
        uint32_t new_cap = ctx->func_type_indexes_capacity == 0
                               ? 8
                               : ctx->func_type_indexes_capacity * 2;
        uint32_t *new_arr = wasm_runtime_malloc(new_cap * sizeof(uint32_t));
        if (!new_arr) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "out of memory allocating func type indexes array");
            return false;
        }
        if (ctx->func_type_indexes) {
            memcpy(new_arr, ctx->func_type_indexes,
                   ctx->func_count * sizeof(uint32_t));
            wasm_runtime_free(ctx->func_type_indexes);
        }
        ctx->func_type_indexes = new_arr;
        ctx->func_type_indexes_capacity = new_cap;
    }
    ctx->func_type_indexes[ctx->func_count] = type_idx;
    return true;
}

// Mark value at idx as consumed. Errors on out-of-bounds or double consumption.
static bool
ctx_consume_value(WASMComponentValidationContext *ctx, uint32_t idx,
                  char *error_buf, uint32_t error_buf_size)
{
    if (idx >= ctx->value_count) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "value idx %u out of bounds", idx);
        return false;
    }
    if (ctx->value_consumed[idx]) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "value %u consumed more than once", idx);
        return false;
    }
    ctx->value_consumed[idx] = true;
    return true;
}

// Compare two WASMComponentValueType* for structural equality.
static bool
valtype_equal(WASMComponentValueType *a, WASMComponentValueType *b,
              WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;

    // Both are primitive value types
    if (a->type == WASM_COMP_VAL_TYPE_PRIMVAL
        && b->type == WASM_COMP_VAL_TYPE_PRIMVAL)
        return a->type_specific.primval_type == b->type_specific.primval_type;

    // Both are type index references - resolve and compare structurally
    if (a->type == WASM_COMP_VAL_TYPE_IDX
        && b->type == WASM_COMP_VAL_TYPE_IDX) {
        if (a->type_specific.type_idx == b->type_specific.type_idx)
            return true;

        // Resolve both and compare structurally
        uint32_t idx_a = a->type_specific.type_idx;
        uint32_t idx_b = b->type_specific.type_idx;
        if (idx_a >= ctx->type_count || idx_b >= ctx->type_count)
            return false;
        if (!ctx->types[idx_a] || !ctx->types[idx_b])
            return false;

        // Both must be def types with the same structure
        WASMComponentTypes *ta = ctx->types[idx_a];
        WASMComponentTypes *tb = ctx->types[idx_b];
        if (ta->tag != WASM_COMP_DEF_TYPE || tb->tag != WASM_COMP_DEF_TYPE)
            return ta->tag == tb->tag;

        return defvaltype_equal(ta->type.def_val_type, tb->type.def_val_type,
                                ctx);
    }

    // Mixed (eg one primval, one idx) - not equal
    return false;
}

// Compare two functype for structural equality
static bool
functype_equal(WASMComponentFuncType *a, WASMComponentFuncType *b,
               WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;

    // Compare params
    uint32_t pa = a->params ? a->params->count : 0;
    uint32_t pb = b->params ? b->params->count : 0;
    if (pa != pb)
        return false;

    for (uint32_t i = 0; i < pa; i++) {
        if (strcmp(a->params->params[i].label->name,
                   b->params->params[i].label->name)
            != 0)
            return false;
        if (!valtype_equal(a->params->params[i].value_type,
                           b->params->params[i].value_type, ctx))
            return false;
    }

    // Compare results
    if (!a->results && !b->results)
        return true;
    if (!a->results || !b->results)
        return false;
    if (a->results->tag != b->results->tag)
        return false;
    if (a->results->tag == WASM_COMP_RESULT_LIST_EMPTY)
        return true;

    return valtype_equal(a->results->results, b->results->results, ctx);
}

// Compare two WASMComponentDefValType* for structural equality
static bool
defvaltype_equal(WASMComponentDefValType *a, WASMComponentDefValType *b,
                 WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->tag != b->tag)
        return false;

    switch (a->tag) {
        case WASM_COMP_DEF_VAL_PRIMVAL:
            return a->def_val.primval == b->def_val.primval;

        case WASM_COMP_DEF_VAL_RECORD:
        {
            WASMComponentRecordType *ra = a->def_val.record;
            WASMComponentRecordType *rb = b->def_val.record;
            if (ra->count != rb->count)
                return false;
            for (uint32_t i = 0; i < ra->count; i++) {
                if (strcmp(ra->fields[i].label->name, rb->fields[i].label->name)
                    != 0)
                    return false;
                if (!valtype_equal(ra->fields[i].value_type,
                                   rb->fields[i].value_type, ctx))
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_VARIANT:
        {
            WASMComponentVariantType *va = a->def_val.variant;
            WASMComponentVariantType *vb = b->def_val.variant;
            if (va->count != vb->count)
                return false;
            for (uint32_t i = 0; i < va->count; i++) {
                if (strcmp(va->cases[i].label->name, vb->cases[i].label->name)
                    != 0)
                    return false;
                if (!valtype_equal(va->cases[i].value_type,
                                   vb->cases[i].value_type, ctx))
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_LIST:
            return valtype_equal(a->def_val.list->element_type,
                                 b->def_val.list->element_type, ctx);

        case WASM_COMP_DEF_VAL_LIST_LEN:
            if (a->def_val.list_len->len != b->def_val.list_len->len)
                return false;
            return valtype_equal(a->def_val.list_len->element_type,
                                 b->def_val.list_len->element_type, ctx);

        case WASM_COMP_DEF_VAL_TUPLE:
        {
            WASMComponentTupleType *ta = a->def_val.tuple;
            WASMComponentTupleType *tb = b->def_val.tuple;
            if (ta->count != tb->count)
                return false;
            for (uint32_t i = 0; i < ta->count; i++) {
                if (!valtype_equal(&ta->element_types[i], &tb->element_types[i],
                                   ctx))
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_FLAGS:
        {
            const WASMComponentFlagType *fa = a->def_val.flag;
            const WASMComponentFlagType *fb = b->def_val.flag;
            if (fa->count != fb->count)
                return false;
            for (uint32_t i = 0; i < fa->count; i++) {
                if (strcmp(fa->flags[i].name, fb->flags[i].name) != 0)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_ENUM:
        {
            const WASMComponentEnumType *ea = a->def_val.enum_type;
            const WASMComponentEnumType *eb = b->def_val.enum_type;
            if (ea->count != eb->count)
                return false;
            for (uint32_t i = 0; i < ea->count; i++) {
                if (strcmp(ea->labels[i].name, eb->labels[i].name) != 0)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_OPTION:
            return valtype_equal(a->def_val.option->element_type,
                                 b->def_val.option->element_type, ctx);

        case WASM_COMP_DEF_VAL_RESULT:
            return valtype_equal(a->def_val.result->result_type,
                                 b->def_val.result->result_type, ctx)
                   && valtype_equal(a->def_val.result->error_type,
                                    b->def_val.result->error_type, ctx);

        case WASM_COMP_DEF_VAL_OWN:
            return a->def_val.owned->type_idx == b->def_val.owned->type_idx;

        case WASM_COMP_DEF_VAL_BORROW:
            return a->def_val.borrow->type_idx == b->def_val.borrow->type_idx;

        case WASM_COMP_DEF_VAL_STREAM:
            return valtype_equal(a->def_val.stream->element_type,
                                 b->def_val.stream->element_type, ctx);

        case WASM_COMP_DEF_VAL_FUTURE:
            return valtype_equal(a->def_val.future->element_type,
                                 b->def_val.future->element_type, ctx);

        default:
            return false;
    }
}

// Compare two top-level WASMComponentTypes* for structural equality.
static bool
component_type_equal(WASMComponentTypes *a, WASMComponentTypes *b,
                     WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->tag != b->tag)
        return false;

    switch (a->tag) {
        case WASM_COMP_DEF_TYPE:
            return defvaltype_equal(a->type.def_val_type, b->type.def_val_type,
                                    ctx);
        case WASM_COMP_FUNC_TYPE:
            return functype_equal(a->type.func_type, b->type.func_type, ctx);
        case WASM_COMP_RESOURCE_TYPE_SYNC:
        case WASM_COMP_RESOURCE_TYPE_ASYNC: // Resource types are generative -
                                            // each definition creates a unique
                                            // type
        default:
            // Component types and instance types - structural comparison would
            // require walking their declaration lists. For now, same-pointer
            // only.
            return false;
    }
}

// Check if valtype A is a subtype of valtype B.
static bool
valtype_subtype(WASMComponentValueType *a, WASMComponentValueType *b,
                WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;

    if (a->type == WASM_COMP_VAL_TYPE_PRIMVAL
        && b->type == WASM_COMP_VAL_TYPE_PRIMVAL)
        return a->type_specific.primval_type == b->type_specific.primval_type;

    if (a->type == WASM_COMP_VAL_TYPE_IDX
        && b->type == WASM_COMP_VAL_TYPE_IDX) {
        if (a->type_specific.type_idx == b->type_specific.type_idx)
            return true;

        uint32_t idx_a = a->type_specific.type_idx;
        uint32_t idx_b = b->type_specific.type_idx;
        if (idx_a >= ctx->type_count || idx_b >= ctx->type_count)
            return false;
        // If either type is opaque (NULL from import/alias), it cannot
        // structurally compare and assume valid
        if (!ctx->types[idx_a] || !ctx->types[idx_b])
            return true;

        WASMComponentTypes *ta = ctx->types[idx_a];
        WASMComponentTypes *tb = ctx->types[idx_b];
        if (ta->tag != WASM_COMP_DEF_TYPE || tb->tag != WASM_COMP_DEF_TYPE)
            return ta->tag == tb->tag;

        return defvaltype_subtype(ta->type.def_val_type, tb->type.def_val_type,
                                  ctx);
    }

    return false;
}

// Check if functype A is a subtype of functype B.
// Params are CONTRAVARIANT (B's param types subtype of A's),
// results are COVARIANT (A's result types subtype of B's).
static bool
functype_subtype(WASMComponentFuncType *a, WASMComponentFuncType *b,
                 WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;

    uint32_t pa = a->params ? a->params->count : 0;
    uint32_t pb = b->params ? b->params->count : 0;
    if (pa != pb)
        return false;

    for (uint32_t i = 0; i < pa; i++) {
        if (strcmp(a->params->params[i].label->name,
                   b->params->params[i].label->name)
            != 0)
            return false;
        // CONTRAVARIANT: B's param must be subtype of A's param
        if (!valtype_subtype(b->params->params[i].value_type,
                             a->params->params[i].value_type, ctx))
            return false;
    }

    if (!a->results && !b->results)
        return true;
    if (!a->results || !b->results)
        return false;
    if (a->results->tag != b->results->tag)
        return false;
    if (a->results->tag == WASM_COMP_RESULT_LIST_EMPTY)
        return true;

    // COVARIANT: A's result must be subtype of B's result
    return valtype_subtype(a->results->results, b->results->results, ctx);
}

// Check if defvaltype A is a subtype of defvaltype B.
static bool
defvaltype_subtype(WASMComponentDefValType *a, WASMComponentDefValType *b,
                   WASMComponentValidationContext *ctx)
{
    uint32_t i = 0, j = 0;

    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->tag != b->tag)
        return false;

    switch (a->tag) {
        case WASM_COMP_DEF_VAL_PRIMVAL:
            return a->def_val.primval == b->def_val.primval;

        case WASM_COMP_DEF_VAL_RECORD:
        {
            // A must have ALL of B's fields, with subtype values.
            // A may have extra fields.
            const WASMComponentRecordType *ra = a->def_val.record;
            const WASMComponentRecordType *rb = b->def_val.record;
            for (i = 0; i < rb->count; i++) {
                bool found = false;
                for (j = 0; j < ra->count; j++) {
                    if (strcmp(ra->fields[j].label->name,
                               rb->fields[i].label->name)
                        == 0) {
                        if (!valtype_subtype(ra->fields[j].value_type,
                                             rb->fields[i].value_type, ctx))
                            return false;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_VARIANT:
        {
            // Every case in A must exist in B, with subtype values.
            const WASMComponentVariantType *va = a->def_val.variant;
            const WASMComponentVariantType *vb = b->def_val.variant;
            for (i = 0; i < va->count; i++) {
                bool found = false;
                for (j = 0; j < vb->count; j++) {
                    if (strcmp(va->cases[i].label->name,
                               vb->cases[j].label->name)
                        == 0) {
                        if (!valtype_subtype(va->cases[i].value_type,
                                             vb->cases[j].value_type, ctx))
                            return false;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_LIST:
            return valtype_subtype(a->def_val.list->element_type,
                                   b->def_val.list->element_type, ctx);

        case WASM_COMP_DEF_VAL_LIST_LEN:
            if (a->def_val.list_len->len != b->def_val.list_len->len)
                return false;
            return valtype_subtype(a->def_val.list_len->element_type,
                                   b->def_val.list_len->element_type, ctx);

        case WASM_COMP_DEF_VAL_TUPLE:
        {
            const WASMComponentTupleType *ta = a->def_val.tuple;
            const WASMComponentTupleType *tb = b->def_val.tuple;
            if (ta->count != tb->count)
                return false;
            for (i = 0; i < ta->count; i++) {
                if (!valtype_subtype(&ta->element_types[i],
                                     &tb->element_types[i], ctx))
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_FLAGS:
        {
            // A's flags must be a SUBSET of B's flags
            const WASMComponentFlagType *fa = a->def_val.flag;
            const WASMComponentFlagType *fb = b->def_val.flag;
            for (i = 0; i < fa->count; i++) {
                bool found = false;
                for (j = 0; j < fb->count; j++) {
                    if (strcmp(fa->flags[i].name, fb->flags[j].name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_ENUM:
        {
            // A's labels must be a SUBSET of B's labels
            const WASMComponentEnumType *ea = a->def_val.enum_type;
            const WASMComponentEnumType *eb = b->def_val.enum_type;
            for (i = 0; i < ea->count; i++) {
                bool found = false;
                for (j = 0; j < eb->count; j++) {
                    if (strcmp(ea->labels[i].name, eb->labels[j].name) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return false;
            }
            return true;
        }

        case WASM_COMP_DEF_VAL_OPTION:
            return valtype_subtype(a->def_val.option->element_type,
                                   b->def_val.option->element_type, ctx);

        case WASM_COMP_DEF_VAL_RESULT:
            return valtype_subtype(a->def_val.result->result_type,
                                   b->def_val.result->result_type, ctx)
                   && valtype_subtype(a->def_val.result->error_type,
                                      b->def_val.result->error_type, ctx);

        case WASM_COMP_DEF_VAL_OWN:
        {
            uint32_t ia = a->def_val.owned->type_idx;
            uint32_t ib = b->def_val.owned->type_idx;
            if (ia == ib)
                return true;
            // Resolve through types array - same pointer means same resource
            if (ia >= ctx->type_count || ib >= ctx->type_count)
                return false;
            // If either is opaque (NULL), assume valid
            if (!ctx->types[ia] || !ctx->types[ib])
                return true;
            return ctx->types[ia] == ctx->types[ib];
        }

        case WASM_COMP_DEF_VAL_BORROW:
        {
            uint32_t ia = a->def_val.borrow->type_idx;
            uint32_t ib = b->def_val.borrow->type_idx;
            if (ia == ib)
                return true;
            if (ia >= ctx->type_count || ib >= ctx->type_count)
                return false;
            if (!ctx->types[ia] || !ctx->types[ib])
                return true;
            return ctx->types[ia] == ctx->types[ib];
        }

        case WASM_COMP_DEF_VAL_STREAM:
            return valtype_subtype(a->def_val.stream->element_type,
                                   b->def_val.stream->element_type, ctx);

        case WASM_COMP_DEF_VAL_FUTURE:
            return valtype_subtype(a->def_val.future->element_type,
                                   b->def_val.future->element_type, ctx);

        default:
            return false;
    }
}

// Check if top-level component type A is a subtype of B.
static bool
component_type_subtype(WASMComponentTypes *a, WASMComponentTypes *b,
                       WASMComponentValidationContext *ctx)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->tag != b->tag)
        return false;

    switch (a->tag) {
        case WASM_COMP_DEF_TYPE:
            return defvaltype_subtype(a->type.def_val_type,
                                      b->type.def_val_type, ctx);
        case WASM_COMP_FUNC_TYPE:
            return functype_subtype(a->type.func_type, b->type.func_type, ctx);
        case WASM_COMP_RESOURCE_TYPE_SYNC:
        case WASM_COMP_RESOURCE_TYPE_ASYNC:
        default:
            return false;
    }
}

// Check if a value type resolves to own<R>, returning the resource type_idx or
// UINT32_MAX
static uint32_t
valtype_get_own_resource(WASMComponentValueType *vt,
                         WASMComponentValidationContext *ctx)
{
    if (!vt || vt->type != WASM_COMP_VAL_TYPE_IDX)
        return UINT32_MAX;

    uint32_t idx = vt->type_specific.type_idx;
    if (idx >= ctx->type_count || !ctx->types[idx])
        return UINT32_MAX;

    WASMComponentTypes *t = ctx->types[idx];
    if (t->tag != WASM_COMP_DEF_TYPE)
        return UINT32_MAX;

    if (t->type.def_val_type->tag == WASM_COMP_DEF_VAL_OWN)
        return t->type.def_val_type->def_val.owned->type_idx;

    return UINT32_MAX;
}

// Check if a value type resolves to borrow<R>, returning the resource type_idx
// or UINT32_MAX
static uint32_t
valtype_get_borrow_resource(WASMComponentValueType *vt,
                            WASMComponentValidationContext *ctx)
{
    if (!vt || vt->type != WASM_COMP_VAL_TYPE_IDX)
        return UINT32_MAX;

    uint32_t idx = vt->type_specific.type_idx;
    if (idx >= ctx->type_count || !ctx->types[idx])
        return UINT32_MAX;

    WASMComponentTypes *t = ctx->types[idx];
    if (t->tag != WASM_COMP_DEF_TYPE)
        return UINT32_MAX;

    if (t->type.def_val_type->tag == WASM_COMP_DEF_VAL_BORROW)
        return t->type.def_val_type->def_val.borrow->type_idx;

    return UINT32_MAX;
}

// Check if result type is (own $R) or (result (own $R) E?)
static bool
result_is_own_or_result_own(WASMComponentFuncType *ft,
                            WASMComponentValidationContext *ctx)
{
    if (!ft || !ft->results)
        return false;

    if (ft->results->tag != WASM_COMP_RESULT_LIST_WITH_TYPE
        || !ft->results->results)
        return false;

    WASMComponentValueType *rv = ft->results->results;

    // Direct own<R>
    if (valtype_get_own_resource(rv, ctx) != UINT32_MAX)
        return true;

    // result<own<R>, E?> — the result_type of a result type must be own<R>
    if (rv->type == WASM_COMP_VAL_TYPE_IDX) {
        uint32_t idx = rv->type_specific.type_idx;
        if (idx < ctx->type_count && ctx->types[idx]
            && ctx->types[idx]->tag == WASM_COMP_DEF_TYPE
            && ctx->types[idx]->type.def_val_type->tag
                   == WASM_COMP_DEF_VAL_RESULT) {
            WASMComponentResultType *res =
                ctx->types[idx]->type.def_val_type->def_val.result;
            if (valtype_get_own_resource(res->result_type, ctx) != UINT32_MAX)
                return true;
        }
    }

    return false;
}

// Validate [constructor]/[method]/[static] annotations on a func import or
// export.
static bool
validate_func_name_annotation(WASMComponentValidationContext *ctx,
                              const char *name, uint32_t ft_idx,
                              char *error_buf, uint32_t error_buf_size)
{
    if (!name)
        return true;

    WASMComponentFuncType *ft = NULL;
    if (ft_idx != UINT32_MAX && ft_idx < ctx->type_count && ctx->types[ft_idx]
        && ctx->types[ft_idx]->tag == WASM_COMP_FUNC_TYPE) {
        ft = ctx->types[ft_idx]->type.func_type;
    }

    if (strncmp(name, CONSTRUCTOR_PREFIX, CONSTRUCTOR_PREFIX_LEN) == 0) {
        // [constructor]R — result must be (own $R) or (result (own $R) E?)
        if (!ft) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "[constructor] annotation requires a known functype");
            return false;
        }
        if (!result_is_own_or_result_own(ft, ctx)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[constructor] result must be (own $R) or (result "
                             "(own $R) E?)");
            return false;
        }
    }
    else if (strncmp(name, METHOD_PREFIX, METHOD_PREFIX_LEN) == 0) {
        // [method]R.name — first param must be (param "self" (borrow $R))
        if (!ft) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[method] annotation requires a known functype");
            return false;
        }
        if (!ft->params || ft->params->count == 0) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "[method] functype must have at least one parameter");
            return false;
        }
        WASMComponentLabelValType *first_param = &ft->params->params[0];
        if (!first_param->label
            || strcmp(first_param->label->name, "self") != 0) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[method] first parameter label must be \"self\"");
            return false;
        }
        if (valtype_get_borrow_resource(first_param->value_type, ctx)
            == UINT32_MAX) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[method] first parameter type must be borrow<R>");
            return false;
        }
    }
    else if (strncmp(name, STATIC_PREFIX, STATIC_PREFIX_LEN) == 0) {
        // [static]R.name — R must match a preceding resource import/export name
        const char *rest = name + STATIC_PREFIX_LEN;
        // Extract R (everything before the first '.')
        const char *dot = strchr(rest, '.');
        if (!dot || dot == rest) {
            set_error_buf_ex(
                error_buf, error_buf_size,
                "[static] annotation must have form [static]R.name");
            return false;
        }
        size_t r_len = dot - rest;
        char r_name[256];
        if (r_len >= sizeof(r_name)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[static] resource name too long");
            return false;
        }
        memcpy(r_name, rest, r_len);
        r_name[r_len] = '\0';

        if (!bh_hash_map_find(ctx->resource_type_names, (void *)r_name)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "[static] resource name \"%s\" not found in scope",
                             r_name);
            return false;
        }
    }

    return true;
}

// Return true if vt (or any type it transitively references) contains
// borrow<T>. If a referenced type index is out of range or was imported/aliased
// (NULL entry) return false
static bool
valtype_has_borrow(WASMComponentValueType *vt,
                   WASMComponentValidationContext *ctx)
{
    // A null type or a primitive value type (like u32, bool, char) can never be
    // borrow<T>
    if (!vt)
        return false;
    if (vt->type == WASM_COMP_VAL_TYPE_PRIMVAL)
        return false;

    uint32_t idx = vt->type_specific.type_idx;
    if (idx >= ctx->type_count || !ctx->types[idx])
        return false;

    WASMComponentTypes *t = ctx->types[idx];
    if (t->tag != WASM_COMP_DEF_TYPE)
        return false;

    // Walk the structure recursively
    WASMComponentDefValType *dvt = t->type.def_val_type;
    switch (dvt->tag) {
        case WASM_COMP_DEF_VAL_BORROW:
            return true;
        case WASM_COMP_DEF_VAL_OWN:
            return false;
        case WASM_COMP_DEF_VAL_RECORD:
        {
            // Check every field of the record
            WASMComponentRecordType *rec = dvt->def_val.record;
            for (uint32_t i = 0; i < rec->count; i++)
                if (valtype_has_borrow(rec->fields[i].value_type, ctx))
                    return true;
            return false;
        }
        case WASM_COMP_DEF_VAL_VARIANT:
        {
            // A borrow in any variant case is enough to reject it in result
            // position
            WASMComponentVariantType *var = dvt->def_val.variant;
            for (uint32_t i = 0; i < var->count; i++)
                if (valtype_has_borrow(var->cases[i].value_type, ctx))
                    return true;
            return false;
        }
        case WASM_COMP_DEF_VAL_TUPLE:
        {
            WASMComponentTupleType *tup = dvt->def_val.tuple;
            for (uint32_t i = 0; i < tup->count; i++)
                if (valtype_has_borrow(&tup->element_types[i], ctx))
                    return true;
            return false;
        }
        // For single-element containers, just recurse into the element type
        case WASM_COMP_DEF_VAL_OPTION:
            return valtype_has_borrow(dvt->def_val.option->element_type, ctx);
        case WASM_COMP_DEF_VAL_RESULT:
            return valtype_has_borrow(dvt->def_val.result->result_type, ctx)
                   || valtype_has_borrow(dvt->def_val.result->error_type, ctx);
        case WASM_COMP_DEF_VAL_LIST:
            return valtype_has_borrow(dvt->def_val.list->element_type, ctx);
        case WASM_COMP_DEF_VAL_LIST_LEN:
            return valtype_has_borrow(dvt->def_val.list_len->element_type, ctx);
        case WASM_COMP_DEF_VAL_FUTURE:
            return valtype_has_borrow(dvt->def_val.future->element_type, ctx);
        default:
            // flags, enum, stream, etc - none of these can contain borrows
            return false;
    }
}

static bool
valtype_has_list_or_string(WASMComponentValueType *vt,
                           WASMComponentValidationContext *ctx)
{
    if (!vt)
        return false;

    if (vt->type == WASM_COMP_VAL_TYPE_PRIMVAL)
        return vt->type_specific.primval_type == WASM_COMP_PRIMVAL_STRING;

    uint32_t idx = vt->type_specific.type_idx;
    if (idx >= ctx->type_count || !ctx->types[idx])
        return false;

    WASMComponentTypes *t = ctx->types[idx];
    if (t->tag != WASM_COMP_DEF_TYPE)
        return false;

    WASMComponentDefValType *dvt = t->type.def_val_type;
    switch (dvt->tag) {
        case WASM_COMP_DEF_VAL_LIST:
        case WASM_COMP_DEF_VAL_LIST_LEN:
            return true;
        case WASM_COMP_DEF_VAL_RECORD:
        {
            WASMComponentRecordType *rec = dvt->def_val.record;
            for (uint32_t i = 0; i < rec->count; i++)
                if (valtype_has_list_or_string(rec->fields[i].value_type, ctx))
                    return true;
            return false;
        }
        case WASM_COMP_DEF_VAL_VARIANT:
        {
            WASMComponentVariantType *var = dvt->def_val.variant;
            for (uint32_t i = 0; i < var->count; i++)
                if (valtype_has_list_or_string(var->cases[i].value_type, ctx))
                    return true;
            return false;
        }
        case WASM_COMP_DEF_VAL_TUPLE:
        {
            WASMComponentTupleType *tup = dvt->def_val.tuple;
            for (uint32_t i = 0; i < tup->count; i++)
                if (valtype_has_list_or_string(&tup->element_types[i], ctx))
                    return true;
            return false;
        }
        case WASM_COMP_DEF_VAL_OPTION:
            return valtype_has_list_or_string(dvt->def_val.option->element_type,
                                              ctx);
        case WASM_COMP_DEF_VAL_RESULT:
            return valtype_has_list_or_string(dvt->def_val.result->result_type,
                                              ctx)
                   || valtype_has_list_or_string(
                       dvt->def_val.result->error_type, ctx);
        default:
            return false;
    }
}

static bool
functype_params_has_list_or_string(WASMComponentFuncType *ft,
                                   WASMComponentValidationContext *ctx)
{
    if (!ft || !ft->params)
        return false;

    for (uint32_t i = 0; i < ft->params->count; i++)
        if (valtype_has_list_or_string(ft->params->params[i].value_type, ctx))
            return true;

    return false;
}

static bool
functype_results_has_list_or_string(WASMComponentFuncType *ft,
                                    WASMComponentValidationContext *ctx)
{
    if (!ft)
        return false;

    if (ft->results && ft->results->tag == WASM_COMP_RESULT_LIST_WITH_TYPE
        && ft->results->results) {
        if (valtype_has_list_or_string(ft->results->results, ctx))
            return true;
    }

    return false;
}

static bool
functype_has_list_or_string(WASMComponentFuncType *ft,
                            WASMComponentValidationContext *ctx)
{
    return functype_params_has_list_or_string(ft, ctx)
           || functype_results_has_list_or_string(ft, ctx);
}

// Core instances are created either by instantiating a core module with named
// import args, or by gathering existing core exports into an inline synthetic
// instance.
static bool
validate_core_instance_section(WASMComponentValidationContext *ctx,
                               WASMComponentSection *section, char *error_buf,
                               uint32_t error_buf_size)
{
    WASMComponentCoreInst *core_inst = NULL;
    for (uint32_t i = 0; i < section->parsed.core_instance_section->count;
         i++) {
        core_inst = &section->parsed.core_instance_section->instances[i];
        switch (core_inst->instance_expression_tag) {
            case WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS:
            {
                // The module being instantiated must already be in the index
                // space
                if (core_inst->expression.with_args.idx
                    >= ctx->core_module_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "module idx is greater than core_module_count");
                    return false;
                }

                // Each arg supplies a core instance as a named import; check
                // bounds and uniqueness
                for (uint32_t j = 0;
                     j < core_inst->expression.with_args.arg_len; j++) {
                    if (core_inst->expression.with_args.args[j].idx.instance_idx
                        >= ctx->core_instance_count) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "instance idx is greater than core_instance_count");
                        return false;
                    }

                    // Import module names must be unique
                    for (uint32_t k = j + 1;
                         k < core_inst->expression.with_args.arg_len; k++) {
                        if (strcmp(core_inst->expression.with_args.args[j]
                                       .name->name,
                                   core_inst->expression.with_args.args[k]
                                       .name->name)
                            == 0) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "names are not unique");
                            return false;
                        }
                    }
                }

                break;
            }

            case WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS:
            {
                // Each export must refer to a valid index.
                for (uint32_t j = 0;
                     j < core_inst->expression.without_args.inline_expr_len;
                     j++) {
                    WASMComponentInlineExport *exp =
                        &core_inst->expression.without_args.inline_expr[j];
                    uint32_t idx = exp->sort_idx->idx;
                    uint8_t core_sort = exp->sort_idx->sort->core_sort;
                    bool in_bounds = false;
                    // Check the idx against the relevant counter for that sort
                    switch (core_sort) {
                        case WASM_COMP_CORE_SORT_FUNC:
                            in_bounds = idx < ctx->core_func_count;
                            break;
                        case WASM_COMP_CORE_SORT_TABLE:
                            in_bounds = idx < ctx->core_table_count;
                            break;
                        case WASM_COMP_CORE_SORT_MEMORY:
                            in_bounds = idx < ctx->core_memory_count;
                            break;
                        case WASM_COMP_CORE_SORT_GLOBAL:
                            in_bounds = idx < ctx->core_global_count;
                            break;
                        case WASM_COMP_CORE_SORT_TYPE:
                            in_bounds = idx < ctx->core_type_count;
                            break;
                        case WASM_COMP_CORE_SORT_MODULE:
                            in_bounds = idx < ctx->core_module_count;
                            break;
                        case WASM_COMP_CORE_SORT_INSTANCE:
                            in_bounds = idx < ctx->core_instance_count;
                            break;
                        default:
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "invalid core sort in inline export: 0x%02x",
                                core_sort);
                            return false;
                    }
                    if (!in_bounds) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "inline export idx %u out of bounds "
                                         "for core sort 0x%02x",
                                         idx, core_sort);
                        return false;
                    }

                    // Export names within a single instance must be unique
                    for (uint32_t k = j + 1;
                         k < core_inst->expression.without_args.inline_expr_len;
                         k++) {
                        if (strcmp(core_inst->expression.without_args
                                       .inline_expr[j]
                                       .name->name,
                                   core_inst->expression.without_args
                                       .inline_expr[k]
                                       .name->name)
                            == 0) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "duplicate export name in core instance");
                            return false;
                        }
                    }
                }
                break;
            }

            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "failed to validate core instance section");
                return false;
            }
        }

        // Each successfully validated core instance occupies the next slot in
        // the core instance index space
        ctx->core_instance_count++;
    }

    return true;
}

// Support only moduletypes. rectype/subtype are not not supported, reject to
// avoid silently accepting malformed binaries
static bool
validate_core_type_section(WASMComponentValidationContext *ctx,
                           WASMComponentSection *section, char *error_buf,
                           uint32_t error_buf_size)
{
    WASMComponentCoreType *core_type_section = NULL;
    for (uint32_t i = 0; i < section->parsed.core_type_section->count; i++) {
        core_type_section = &section->parsed.core_type_section->types[i];

        switch (core_type_section->deftype->tag) {
            case WASM_CORE_DEFTYPE_RECTYPE:
            case WASM_CORE_DEFTYPE_SUBTYPE:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "unsupported core deftype");
                return false;
            }

            case WASM_CORE_DEFTYPE_MODULETYPE:
            {
                WASMComponentCoreModuleType *moduletype =
                    core_type_section->deftype->type.moduletype;
                for (uint32_t j = 0; j < moduletype->decl_count; j++) {
                    switch (moduletype->declarations[j].tag) {
                        case WASM_CORE_MODULEDECL_IMPORT:
                        {
                            const WASMComponentCoreImport *imp =
                                moduletype->declarations[j]
                                    .decl.import_decl.import;
                            // Each pair (module, name) must be unique within
                            // the moduletype
                            for (uint32_t k = 0; k < j; k++) {
                                if (moduletype->declarations[k].tag
                                    != WASM_CORE_MODULEDECL_IMPORT)
                                    continue;

                                const WASMComponentCoreImport *prev =
                                    moduletype->declarations[k]
                                        .decl.import_decl.import;
                                if (strcmp(imp->mod_name->name,
                                           prev->mod_name->name)
                                        == 0
                                    && strcmp(imp->nm->name, prev->nm->name)
                                           == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate import (module, name) in "
                                        "moduledecl");
                                    return false;
                                }
                            }
                            break;
                        }

                        case WASM_CORE_MODULEDECL_TYPE:
                        {
                            // A module cannot declare another module as an
                            // inline type
                            if (moduletype->declarations[j]
                                    .decl.type_decl.type->deftype->tag
                                == WASM_CORE_DEFTYPE_MODULETYPE) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "type declarator inside moduledecl must "
                                    "not be a moduletype");
                                return false;
                            }
                            break;
                        }

                        case WASM_CORE_MODULEDECL_ALIAS:
                        {
                            // Alias declarations inside a moduletype can only
                            // alias types
                            if (moduletype->declarations[j]
                                    .decl.alias_decl.alias->sort.core_sort
                                != WASM_COMP_CORE_SORT_TYPE) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "invalid core sort");
                                return false;
                            }
                            break;
                        }

                        case WASM_CORE_MODULEDECL_EXPORT:
                        {
                            const WASMComponentCoreExportDecl *exp =
                                moduletype->declarations[j]
                                    .decl.export_decl.export_decl;
                            // Export names must be unique within the moduletype
                            for (uint32_t k = 0; k < j; k++) {
                                if (moduletype->declarations[k].tag
                                    != WASM_CORE_MODULEDECL_EXPORT)
                                    continue;

                                const WASMComponentCoreExportDecl *prev =
                                    moduletype->declarations[k]
                                        .decl.export_decl.export_decl;
                                if (strcmp(exp->name->name, prev->name->name)
                                    == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate export name in moduledecl");
                                    return false;
                                }
                            }
                            break;
                        }

                        default:
                        {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "invalid moduletype tag");
                            return false;
                        }
                    }
                }

                break;
            }

            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "invalid deftype tag");
                return false;
            }
        }

        ctx->core_type_count++;
    }

    return true;
}

// Nested component is validated recursively
static bool
validate_component_section(WASMComponentValidationContext *ctx,
                           WASMComponentSection *section, char *error_buf,
                           uint32_t error_buf_size)
{
    bool ok = wasm_component_validate(section->parsed.component, ctx, error_buf,
                                      error_buf_size);
    if (ok)
        ctx->component_count++;
    return ok;
}

// Each one either instantiates a component with named args, or bundles existing
// component-level definitions into a synthetic instance via inline exports
static bool
validate_instances_section(WASMComponentValidationContext *ctx,
                           WASMComponentSection *section, char *error_buf,
                           uint32_t error_buf_size)
{
    WASMComponentInst *inst = NULL;
    for (uint32_t i = 0; i < section->parsed.instance_section->count; i++) {
        inst = &section->parsed.instance_section->instances[i];

        switch (inst->instance_expression_tag) {
            case WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS:
            {
                if (inst->expression.with_args.idx >= ctx->component_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "component idx is greater than component_count");
                    return false;
                }

                // Each argument is a pair (name, sort+idx) that satisfies one
                // of the import
                for (uint32_t j = 0; j < inst->expression.with_args.arg_len;
                     j++) {
                    WASMComponentSortIdx *sidx =
                        inst->expression.with_args.args[j].idx.sort_idx;
                    bool in_bounds = false;
                    switch (sidx->sort->sort) {
                        case WASM_COMP_SORT_CORE_SORT:
                            if (sidx->sort->core_sort != 0x11) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "only core module sort allowed in "
                                    "component instantiate arg");
                                return false;
                            }
                            in_bounds = sidx->idx < ctx->core_module_count;
                            break;
                        case WASM_COMP_SORT_FUNC:
                            in_bounds = sidx->idx < ctx->func_count;
                            break;
                        case WASM_COMP_SORT_VALUE:
                            in_bounds = sidx->idx < ctx->value_count;
                            break;
                        case WASM_COMP_SORT_TYPE:
                            in_bounds = sidx->idx < ctx->type_count;
                            break;
                        case WASM_COMP_SORT_COMPONENT:
                            in_bounds = sidx->idx < ctx->component_count;
                            break;
                        case WASM_COMP_SORT_INSTANCE:
                            in_bounds = sidx->idx < ctx->instance_count;
                            break;
                        default:
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "invalid sort in component "
                                             "instantiate arg: 0x%02x",
                                             sidx->sort->sort);
                            return false;
                    }
                    if (!in_bounds) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "arg idx %u out of bounds for sort 0x%02x",
                            sidx->idx, sidx->sort->sort);
                        return false;
                    }

                    if (sidx->sort->sort == WASM_COMP_SORT_VALUE) {
                        if (!ctx_consume_value(ctx, sidx->idx, error_buf,
                                               error_buf_size))
                            return false;
                    }

                    // Argument names must be unique
                    for (uint32_t k = j + 1;
                         k < inst->expression.with_args.arg_len; k++) {
                        if (strcmp(
                                inst->expression.with_args.args[j].name->name,
                                inst->expression.with_args.args[k].name->name)
                            == 0) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "duplicate arg name in component instance");
                            return false;
                        }
                    }
                }

                break;
            }

            case WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS:
            {
                for (uint32_t j = 0;
                     j < inst->expression.without_args.inline_expr_len; j++) {
                    WASMComponentInlineExport *exp =
                        &inst->expression.without_args.inline_expr[j];
                    uint32_t idx = exp->sort_idx->idx;
                    uint8_t comp_sort = exp->sort_idx->sort->sort;
                    bool in_bounds = false;

                    switch (comp_sort) {
                        case WASM_COMP_SORT_CORE_SORT:
                            if (exp->sort_idx->sort->core_sort != 0x11) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "only core module sort allowed in "
                                    "component inline export");
                                return false;
                            }
                            in_bounds = idx < ctx->core_module_count;
                            break;
                        case WASM_COMP_SORT_FUNC:
                            in_bounds = idx < ctx->func_count;
                            break;
                        case WASM_COMP_SORT_VALUE:
                            in_bounds = idx < ctx->value_count;
                            break;
                        case WASM_COMP_SORT_TYPE:
                            in_bounds = idx < ctx->type_count;
                            break;
                        case WASM_COMP_SORT_COMPONENT:
                            in_bounds = idx < ctx->component_count;
                            break;
                        case WASM_COMP_SORT_INSTANCE:
                            in_bounds = idx < ctx->instance_count;
                            break;
                        default:
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "invalid sort in component inline "
                                             "export: 0x%02x",
                                             comp_sort);
                            return false;
                    }

                    if (!in_bounds) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "inline export idx %u out of bounds "
                                         "for sort 0x%02x",
                                         idx, comp_sort);
                        return false;
                    }

                    if (comp_sort == WASM_COMP_SORT_VALUE) {
                        if (!ctx_consume_value(ctx, idx, error_buf,
                                               error_buf_size))
                            return false;
                    }

                    // Export names within the synthetic instance must be unique
                    for (uint32_t k = j + 1;
                         k < inst->expression.without_args.inline_expr_len;
                         k++) {
                        if (strcmp(inst->expression.without_args.inline_expr[j]
                                       .name->name,
                                   inst->expression.without_args.inline_expr[k]
                                       .name->name)
                            == 0) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "duplicate export name in component instance");
                            return false;
                        }
                    }
                }
                break;
            }

            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "failed to validate instance section");
                return false;
            }
        }

        ctx->instance_count++;
    }

    return true;
}

static bool
validate_aliases_section(WASMComponentValidationContext *ctx,
                         WASMComponentSection *section, char *error_buf,
                         uint32_t error_buf_size)
{
    WASMComponentAliasSection *alias_section = section->parsed.alias_section;
    for (uint32_t i = 0; i < alias_section->count; i++) {
        WASMComponentAliasDefinition *alias_def = &alias_section->aliases[i];
        WASMComponentTypes *outer_type_propagate = NULL;

        switch (alias_def->alias_target_type) {
            case WASM_COMP_ALIAS_TARGET_EXPORT:
            {
                // The instance must already exist in the component instance
                // index space
                if (alias_def->target.exported.instance_idx
                    >= ctx->instance_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "invalid instance index for alias export");
                    return false;
                }
                break;
            }

            case WASM_COMP_ALIAS_TARGET_CORE_EXPORT:
            {
                // The instance must already exist in the core component
                // instance index space
                if (alias_def->target.core_exported.instance_idx
                    >= ctx->core_instance_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "invalid instance index for core alias export");
                    return false;
                }
                break;
            }

            case WASM_COMP_ALIAS_TARGET_OUTER:
            {
                // Outer aliases reach into an enclosing component's scope. ct
                // (component traversal) is the number of component boundaries
                // to cross
                uint32_t ct = alias_def->target.outer.ct;
                uint32_t idx = alias_def->target.outer.idx;

                // outer alias sort restricted to type, module (core), or
                // component
                if (alias_def->sort->sort == WASM_COMP_SORT_CORE_SORT) {
                    uint8_t cs = alias_def->sort->core_sort;
                    if (cs != WASM_COMP_CORE_SORT_TYPE
                        && cs != WASM_COMP_CORE_SORT_MODULE) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "outer alias core sort restricted to "
                                         "type or module");
                        return false;
                    }
                }
                else {
                    uint8_t s = alias_def->sort->sort;
                    if (s != WASM_COMP_SORT_TYPE
                        && s != WASM_COMP_SORT_COMPONENT) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "outer alias sort restricted to type or component");
                        return false;
                    }
                }

                // Walk up the parent chain ct levels to reach the target
                // ancestor
                WASMComponentValidationContext *ancestor = ctx;
                for (uint32_t level = 0; level < ct; level++) {
                    ancestor = ancestor->parent;
                    if (!ancestor) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "outer alias ct exceeds component nesting depth");
                        return false;
                    }
                }

                bool in_bounds = false;
                if (alias_def->sort->sort == WASM_COMP_SORT_CORE_SORT) {
                    switch (alias_def->sort->core_sort) {
                        case WASM_COMP_CORE_SORT_TYPE:
                            in_bounds = idx < ancestor->core_type_count;
                            break;
                        case WASM_COMP_CORE_SORT_MODULE:
                            in_bounds = idx < ancestor->core_module_count;
                            break;
                        default:
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "invalid core sort in outer alias");
                            return false;
                    }
                }
                else {
                    switch (alias_def->sort->sort) {
                        case WASM_COMP_SORT_TYPE:
                            in_bounds = idx < ancestor->type_count;
                            // If the ancestor has a types[] entry for this
                            // index, carry it into the current scope so
                            // own<T>/borrow<T> checks can still resolve it
                            if (in_bounds && ancestor->types) {
                                outer_type_propagate = ancestor->types[idx];
                                if (outer_type_propagate
                                    && (outer_type_propagate->tag
                                            == WASM_COMP_RESOURCE_TYPE_SYNC
                                        || outer_type_propagate->tag
                                               == WASM_COMP_RESOURCE_TYPE_ASYNC)) {
                                    set_error_buf_ex(error_buf, error_buf_size,
                                                     "outer alias of resource "
                                                     "type is not allowed");
                                    return false;
                                }
                            }
                            break;
                        case WASM_COMP_SORT_COMPONENT:
                            in_bounds = idx < ancestor->component_count;
                            break;
                        default:
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "invalid sort in outer alias");
                            return false;
                    }
                }
                if (!in_bounds) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "outer alias idx %u out of bounds", idx);
                    return false;
                }
                break;
            }

            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "unknown alias target type");
                return false;
            }
        }

        // Every alias introduces a new entry into the matching index space
        if (alias_def->sort->sort == WASM_COMP_SORT_CORE_SORT) {
            switch (alias_def->sort->core_sort) {
                case WASM_COMP_CORE_SORT_FUNC:
                    ctx->core_func_count++;
                    break;
                case WASM_COMP_CORE_SORT_TABLE:
                    ctx->core_table_count++;
                    break;
                case WASM_COMP_CORE_SORT_MEMORY:
                    ctx->core_memory_count++;
                    break;
                case WASM_COMP_CORE_SORT_GLOBAL:
                    ctx->core_global_count++;
                    break;
                case WASM_COMP_CORE_SORT_TYPE:
                    ctx->core_type_count++;
                    break;
                case WASM_COMP_CORE_SORT_MODULE:
                    ctx->core_module_count++;
                    break;
                case WASM_COMP_CORE_SORT_INSTANCE:
                    ctx->core_instance_count++;
                    break;
                default:
                {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "invalid core sort in alias");
                    return false;
                }
            }
        }
        else {
            switch (alias_def->sort->sort) {
                case WASM_COMP_SORT_FUNC:
                    if (!ctx_push_func_type(ctx, UINT32_MAX, error_buf,
                                            error_buf_size))
                        return false;
                    ctx->func_count++;
                    break;
                case WASM_COMP_SORT_VALUE:
                    if (!ctx_push_value(ctx, error_buf, error_buf_size))
                        return false;
                    ctx->value_count++;
                    break;
                case WASM_COMP_SORT_TYPE:
                    // For type aliases we also record an entry in types[]. If
                    // this was an outer alias we have a real pointer; otherwise
                    // we push NULL
                    if (!ctx_push_type(ctx, outer_type_propagate, false,
                                       error_buf, error_buf_size))
                        return false;
                    ctx->type_count++;
                    break;
                case WASM_COMP_SORT_COMPONENT:
                    ctx->component_count++;
                    break;
                case WASM_COMP_SORT_INSTANCE:
                    ctx->instance_count++;
                    break;
                default:
                {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "invalid sort in alias: 0x%02x",
                                     alias_def->sort->sort);
                    return false;
                }
            }
        }
    }

    return true;
}

// Each new type appended here gets a slot in the flat types[] array so later
// sections can do O(1) tag checks via type index
static bool
validate_type_section(WASMComponentValidationContext *ctx,
                      WASMComponentSection *section, char *error_buf,
                      uint32_t error_buf_size)
{
    WASMComponentTypes *type_section = NULL;
    for (uint32_t i = 0; i < section->parsed.type_section->count; i++) {
        type_section = &section->parsed.type_section->types[i];
        switch (type_section->tag) {
            case WASM_COMP_DEF_TYPE:
            {
                // defvaltype covers all the value-carrying types
                WASMComponentDefValType *dvt = type_section->type.def_val_type;
                switch (dvt->tag) {
                    case WASM_COMP_DEF_VAL_OWN:
                    {
                        // own<T> must wrap a resource type
                        uint32_t idx = dvt->def_val.owned->type_idx;
                        if (idx >= ctx->type_count) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "own type idx %u out of bounds",
                                             idx);
                            return false;
                        }
                        if (ctx->types[idx]
                            && ctx->types[idx]->tag
                                   != WASM_COMP_RESOURCE_TYPE_SYNC
                            && ctx->types[idx]->tag
                                   != WASM_COMP_RESOURCE_TYPE_ASYNC) {
                            set_error_buf_ex(
                                error_buf, error_buf_size,
                                "own type idx must refer to a resource type");
                            return false;
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_BORROW:
                    {
                        // borrow<T> must be a resource type
                        uint32_t idx = dvt->def_val.borrow->type_idx;
                        if (idx >= ctx->type_count) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "borrow type idx %u out of bounds",
                                             idx);
                            return false;
                        }
                        if (ctx->types[idx]
                            && ctx->types[idx]->tag
                                   != WASM_COMP_RESOURCE_TYPE_SYNC
                            && ctx->types[idx]->tag
                                   != WASM_COMP_RESOURCE_TYPE_ASYNC) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "borrow type idx must refer to a "
                                             "resource type");
                            return false;
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_STREAM:
                    {
                        // The spec explicitly bans (stream char) because it
                        // would conflict with the string type, which already
                        // has its own dedicated encoding options
                        WASMComponentStreamType *st = dvt->def_val.stream;
                        if (st->element_type
                            && st->element_type->type
                                   == WASM_COMP_VAL_TYPE_PRIMVAL
                            && st->element_type->type_specific.primval_type
                                   == WASM_COMP_PRIMVAL_CHAR) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "(stream char) is not allowed");
                            return false;
                        }
                        if (st->element_type
                            && valtype_has_borrow(st->element_type, ctx)) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "borrow type must not appear in "
                                             "stream element type");
                            return false;
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_FUTURE:
                    {
                        WASMComponentFutureType *ft = dvt->def_val.future;
                        if (ft->element_type
                            && ft->element_type->type
                                   == WASM_COMP_VAL_TYPE_PRIMVAL
                            && ft->element_type->type_specific.primval_type
                                   == WASM_COMP_PRIMVAL_CHAR) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "(future char) is not allowed");
                            return false;
                        }
                        if (ft->element_type
                            && valtype_has_borrow(ft->element_type, ctx)) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "borrow type must not appear in "
                                             "future element type");
                            return false;
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_RECORD:
                    {
                        // Record field names must all be unique
                        const WASMComponentRecordType *rec =
                            dvt->def_val.record;
                        for (uint32_t j = 0; j < rec->count; j++) {
                            for (uint32_t k = j + 1; k < rec->count; k++) {
                                if (strcmp(rec->fields[j].label->name,
                                           rec->fields[k].label->name)
                                    == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate field label in record type");
                                    return false;
                                }
                            }
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_VARIANT:
                    {
                        // Variant case names must be unique
                        const WASMComponentVariantType *var =
                            dvt->def_val.variant;
                        for (uint32_t j = 0; j < var->count; j++) {
                            for (uint32_t k = j + 1; k < var->count; k++) {
                                if (strcmp(var->cases[j].label->name,
                                           var->cases[k].label->name)
                                    == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate case label in variant type");
                                    return false;
                                }
                            }
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_FLAGS:
                    {
                        // Flag names duplicates would make the bitfield
                        // ambiguous so reject them
                        const WASMComponentFlagType *fl = dvt->def_val.flag;
                        for (uint32_t j = 0; j < fl->count; j++) {
                            for (uint32_t k = j + 1; k < fl->count; k++) {
                                if (strcmp(fl->flags[j].name, fl->flags[k].name)
                                    == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate label in flags type");
                                    return false;
                                }
                            }
                        }
                        break;
                    }
                    case WASM_COMP_DEF_VAL_ENUM:
                    {
                        // Enum labels map to consecutive integer values,
                        // duplicates are invalid
                        const WASMComponentEnumType *en =
                            dvt->def_val.enum_type;
                        for (uint32_t j = 0; j < en->count; j++) {
                            for (uint32_t k = j + 1; k < en->count; k++) {
                                if (strcmp(en->labels[j].name,
                                           en->labels[k].name)
                                    == 0) {
                                    set_error_buf_ex(
                                        error_buf, error_buf_size,
                                        "duplicate label in enum type");
                                    return false;
                                }
                            }
                        }
                        break;
                    }
                    default:
                        break;
                }
                break;
            }

            case WASM_COMP_FUNC_TYPE:
            {
                const WASMComponentFuncType *ft = type_section->type.func_type;
                // Parameter labels needs to be unique
                if (ft->params) {
                    for (uint32_t j = 0; j < ft->params->count; j++) {
                        for (uint32_t k = j + 1; k < ft->params->count; k++) {
                            if (strcmp(ft->params->params[j].label->name,
                                       ft->params->params[k].label->name)
                                == 0) {
                                set_error_buf_ex(
                                    error_buf, error_buf_size,
                                    "duplicate parameter label in functype");
                                return false;
                            }
                        }
                    }
                }
                // borrow<T> is a caller-scoped lend and returning it across a
                // function boundary would extend its lifetime past the call
                if (ft->results
                    && ft->results->tag == WASM_COMP_RESULT_LIST_WITH_TYPE
                    && valtype_has_borrow(ft->results->results, ctx)) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "borrow type must not appear in functype "
                                     "result position");
                    return false;
                }
                break;
            }

            case WASM_COMP_COMPONENT_TYPE:
            {
                // Resource types embed one inside a componenttype would violate
                // that uniqueness guarantee
                const WASMComponentComponentType *ct =
                    type_section->type.component_type;
                for (uint32_t j = 0; j < ct->count; j++) {
                    WASMComponentComponentDecl *cdecl = &ct->component_decls[j];
                    if (cdecl->tag == WASM_COMP_COMPONENT_DECL_TYPE) {
                        WASMComponentInstDecl *id = cdecl->decl.instance_decl;
                        if (id->tag == WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE) {
                            const WASMComponentTypes *inner = id->decl.type;
                            if (inner->tag == WASM_COMP_RESOURCE_TYPE_SYNC
                                || inner->tag
                                       == WASM_COMP_RESOURCE_TYPE_ASYNC) {
                                set_error_buf_ex(error_buf, error_buf_size,
                                                 "resource type not allowed "
                                                 "inside componenttype");
                                return false;
                            }
                        }
                    }
                }
                break;
            }

            case WASM_COMP_INSTANCE_TYPE:
            {
                // Resource types in instancetype bodies would be shared across
                // all instances that match the type
                const WASMComponentInstType *it =
                    type_section->type.instance_type;
                for (uint32_t j = 0; j < it->count; j++) {
                    WASMComponentInstDecl *decl = &it->instance_decls[j];
                    if (decl->tag == WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE) {
                        const WASMComponentTypes *inner = decl->decl.type;
                        if (inner->tag == WASM_COMP_RESOURCE_TYPE_SYNC
                            || inner->tag == WASM_COMP_RESOURCE_TYPE_ASYNC) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "resource type not allowed inside "
                                             "instancetype");
                            return false;
                        }
                    }
                    // sort restricted to type (0x03) or instance (0x05)
                    if (decl->tag == WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS) {
                        uint8_t sort = decl->decl.alias->sort->sort;
                        if (sort != WASM_COMP_SORT_TYPE
                            && sort != WASM_COMP_SORT_INSTANCE) {
                            set_error_buf_ex(error_buf, error_buf_size,
                                             "alias inside instancedecl must "
                                             "be type or instance sort");
                            return false;
                        }
                    }
                }
                break;
            }

            case WASM_COMP_RESOURCE_TYPE_SYNC:
            {
                // A sync resource may optionally declare a destructor. If it
                // does, the destructor must be a core function that's already
                // in scope
                const WASMComponentResourceTypeSync *rs =
                    type_section->type.resource_type->resource.sync;
                if (rs->has_dtor && rs->dtor_func_idx >= ctx->core_func_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "resource sync dtor func idx out of bounds");
                    return false;
                }
                break;
            }

            case WASM_COMP_RESOURCE_TYPE_ASYNC:
            {
                // Async resources always have a destructor and a callback; both
                // must be valid
                const WASMComponentResourceTypeAsync *ra =
                    type_section->type.resource_type->resource.async;
                if (ra->dtor_func_idx >= ctx->core_func_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "resource async dtor func idx out of bounds");
                    return false;
                }
                if (ra->callback_func_idx >= ctx->core_func_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "resource async callback func idx out of bounds");
                    return false;
                }
                break;
            }

            default:
            {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "invalid type section tag");
                return false;
            }
        }

        // Save the type in the flat lookup array so subsequent definitions can
        // reference it by index. is_local=true distinguishes types defined here
        // from imported/aliased ones
        if (!ctx_push_type(ctx, type_section, true, error_buf, error_buf_size))
            return false;
        ctx->type_count++;
    }

    return true;
}

// Options can appear in any order but each may appear at most once, and some
// combinations are illegal (e.g., realloc without memory)
static bool
validate_canon_opts(WASMComponentValidationContext *ctx,
                    WASMComponentCanonOpts *opts, bool is_lift,
                    WASMComponentFuncType *ft, char *error_buf,
                    uint32_t error_buf_size)
{
    if (!opts)
        return true;

    // Track which options have been seen so we can detect duplicates and
    // illegal combinations
    bool seen_string_enc = false;
    bool has_memory = false;
    bool has_realloc = false;
    bool has_post_return = false;
    for (uint32_t i = 0; i < opts->canon_opts_count; i++) {
        WASMComponentCanonOpt *opt = &opts->canon_opts[i];
        switch (opt->tag) {
            case WASM_COMP_CANON_OPT_STRING_UTF8:
            case WASM_COMP_CANON_OPT_STRING_UTF16:
            case WASM_COMP_CANON_OPT_STRING_LATIN1_UTF16:
                // Only one string encoding may be specified
                if (seen_string_enc) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "string-encoding specified more than once in canonopt");
                    return false;
                }
                seen_string_enc = true;
                break;
            case WASM_COMP_CANON_OPT_MEMORY:
                if (has_memory) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "duplicate memory in canonopt");
                    return false;
                }
                if (opt->payload.memory.mem_idx >= ctx->core_memory_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canonopt memory idx out of bounds");
                    return false;
                }
                has_memory = true;
                break;
            case WASM_COMP_CANON_OPT_REALLOC:
                if (has_realloc) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "duplicate realloc in canonopt");
                    return false;
                }
                if (opt->payload.realloc_opt.func_idx >= ctx->core_func_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canonopt realloc func idx out of bounds");
                    return false;
                }
                has_realloc = true;
                break;
            case WASM_COMP_CANON_OPT_POST_RETURN:
                // post-return is called after the caller is done with returned
                // memory and it only makes sense on lift
                if (has_post_return) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "duplicate post-return in canonopt");
                    return false;
                }
                if (!is_lift) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "post-return canonopt only allowed on canon lift");
                    return false;
                }
                if (opt->payload.post_return.func_idx >= ctx->core_func_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canonopt post-return func idx out of bounds");
                    return false;
                }
                has_post_return = true;
                break;
            case WASM_COMP_CANON_OPT_ASYNC:
            case WASM_COMP_CANON_OPT_CALLBACK:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "async canonopt is not supported");
                return false;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "unknown canonopt tag: 0x%02x", opt->tag);
                return false;
        }
    }

    if (has_realloc && !has_memory) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "realloc canonopt requires memory to also be present");
        return false;
    }

    if (ft && functype_has_list_or_string(ft, ctx)) {
        if (!has_memory) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "canon %s requires memory when functype contains "
                             "list or string",
                             is_lift ? "lift" : "lower");
            return false;
        }
        // realloc is needed for canon lift when params have list/string (to
        // write incoming data), and for canon lower when results have
        // list/string (to write outgoing data)
        bool needs_realloc = is_lift
                                 ? functype_params_has_list_or_string(ft, ctx)
                                 : functype_results_has_list_or_string(ft, ctx);
        if (needs_realloc && !has_realloc) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "canon %s requires realloc when functype %s "
                             "contain list or string",
                             is_lift ? "lift" : "lower",
                             is_lift ? "params" : "results");
            return false;
        }
    }

    return true;
}

// The resource operations manage the lifetime of opaque handles passed across
// component boundaries
static bool
validate_canons_section(WASMComponentValidationContext *ctx,
                        WASMComponentSection *section, char *error_buf,
                        uint32_t error_buf_size)
{
    WASMComponentCanon *canon_section = NULL;
    for (uint32_t i = 0; i < section->parsed.canon_section->count; i++) {
        canon_section = &section->parsed.canon_section->canons[i];

        switch (canon_section->tag) {
            case WASM_COMP_CANON_LIFT:
            {
                // The core func must already exist, and the type it's lifted to
                // must be a functype
                if (canon_section->canon_data.lift.core_func_idx
                    >= ctx->core_func_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canon lift core func idx out of bounds");
                    return false;
                }
                uint32_t lift_type_idx =
                    canon_section->canon_data.lift.type_idx;
                if (lift_type_idx >= ctx->type_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canon lift type idx out of bounds");
                    return false;
                }
                // The type idx must refer to a functype
                if (ctx->types[lift_type_idx]
                    && ctx->types[lift_type_idx]->tag != WASM_COMP_FUNC_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canon lift type must be a functype");
                    return false;
                }
                WASMComponentFuncType *lift_ft =
                    (ctx->types[lift_type_idx]
                     && ctx->types[lift_type_idx]->tag == WASM_COMP_FUNC_TYPE)
                        ? ctx->types[lift_type_idx]->type.func_type
                        : NULL;
                if (!validate_canon_opts(
                        ctx, canon_section->canon_data.lift.canon_opts, true,
                        lift_ft, error_buf, error_buf_size))
                    return false;
                // Lifting a core function introduces a new entry in the
                // component function index space
                if (!ctx_push_func_type(ctx, lift_type_idx, error_buf,
                                        error_buf_size))
                    return false;
                ctx->func_count++;
                break;
            }

            case WASM_COMP_CANON_LOWER:
            {
                uint32_t lower_func_idx =
                    canon_section->canon_data.lower.func_idx;
                if (lower_func_idx >= ctx->func_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "canon lower func idx out of bounds");
                    return false;
                }
                // Look up the functype via func-to-type tracking
                WASMComponentFuncType *lower_ft = NULL;
                uint32_t lower_ft_idx = ctx->func_type_indexes[lower_func_idx];
                if (lower_ft_idx != UINT32_MAX && lower_ft_idx < ctx->type_count
                    && ctx->types[lower_ft_idx]
                    && ctx->types[lower_ft_idx]->tag == WASM_COMP_FUNC_TYPE) {
                    lower_ft = ctx->types[lower_ft_idx]->type.func_type;
                }
                if (!validate_canon_opts(
                        ctx, canon_section->canon_data.lower.canon_opts, false,
                        lower_ft, error_buf, error_buf_size))
                    return false;
                ctx->core_func_count++;
                break;
            }

            case WASM_COMP_CANON_RESOURCE_NEW:
            {
                uint32_t rt_idx =
                    canon_section->canon_data.resource_new.resource_type_idx;
                if (rt_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.new type idx out of bounds");
                    return false;
                }
                if (!ctx->type_is_local[rt_idx]) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.new type must be locally defined");
                    return false;
                }
                if (ctx->types[rt_idx]
                    && ctx->types[rt_idx]->tag != WASM_COMP_RESOURCE_TYPE_SYNC
                    && ctx->types[rt_idx]->tag
                           != WASM_COMP_RESOURCE_TYPE_ASYNC) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.new type must be a resource type");
                    return false;
                }
                ctx->core_func_count++;
                break;
            }

            case WASM_COMP_CANON_RESOURCE_DROP:
            {
                // resource.drop accepts both local and imported resource types
                uint32_t rt_idx =
                    canon_section->canon_data.resource_drop.resource_type_idx;
                if (rt_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.drop type idx out of bounds");
                    return false;
                }
                if (ctx->types[rt_idx]
                    && ctx->types[rt_idx]->tag != WASM_COMP_RESOURCE_TYPE_SYNC
                    && ctx->types[rt_idx]->tag
                           != WASM_COMP_RESOURCE_TYPE_ASYNC) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.drop type must be a resource type");
                    return false;
                }
                ctx->core_func_count++;
                break;
            }

            case WASM_COMP_CANON_RESOURCE_REP:
            {
                uint32_t rt_idx =
                    canon_section->canon_data.resource_rep.resource_type_idx;
                if (rt_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.rep type idx out of bounds");
                    return false;
                }
                if (!ctx->type_is_local[rt_idx]) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.rep type must be locally defined");
                    return false;
                }
                if (ctx->types[rt_idx]
                    && ctx->types[rt_idx]->tag != WASM_COMP_RESOURCE_TYPE_SYNC
                    && ctx->types[rt_idx]->tag
                           != WASM_COMP_RESOURCE_TYPE_ASYNC) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "canon resource.rep type must be a resource type");
                    return false;
                }
                ctx->core_func_count++;
                break;
            }

            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "unsupported canon opcode: 0x%02x",
                                 (unsigned)canon_section->tag);
                return false;
        }
    }

    return true;
}

static bool
validate_start_section(WASMComponentValidationContext *ctx,
                       WASMComponentSection *section, char *error_buf,
                       uint32_t error_buf_size)
{
    WASMComponentStartSection *start_section = section->parsed.start_section;
    if (start_section->func_idx >= ctx->func_count) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "start function index is greater than func count");
        return false;
    }

    // Verify that the start function's param/result arity matches the
    // arg/result counts
    uint32_t start_ft_idx = ctx->func_type_indexes[start_section->func_idx];
    if (start_ft_idx != UINT32_MAX && start_ft_idx < ctx->type_count
        && ctx->types[start_ft_idx]
        && ctx->types[start_ft_idx]->tag == WASM_COMP_FUNC_TYPE) {
        WASMComponentFuncType *start_ft =
            ctx->types[start_ft_idx]->type.func_type;
        uint32_t expected_params =
            start_ft->params ? start_ft->params->count : 0;
        uint32_t expected_results = 0;
        if (start_ft->results
            && start_ft->results->tag == WASM_COMP_RESULT_LIST_WITH_TYPE)
            expected_results = 1;

        if (start_section->value_args_count != expected_params) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "start section arg count %u does not match "
                             "functype param count %u",
                             start_section->value_args_count, expected_params);
            return false;
        }
        if (start_section->result != expected_results) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "start section result count %u does not match "
                             "functype result count %u",
                             start_section->result, expected_results);
            return false;
        }
    }

    // Each value arg must refer to an already-defined value, and consumes it
    for (uint32_t i = 0; i < start_section->value_args_count; i++) {
        if (start_section->value_args[i] >= ctx->value_count) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "start value arg idx %u out of bounds",
                             start_section->value_args[i]);
            return false;
        }
        if (!ctx_consume_value(ctx, start_section->value_args[i], error_buf,
                               error_buf_size))
            return false;
    }
    // The r result values produced by start become new (unconsumed) entries in
    // the value index space
    for (uint32_t i = 0; i < start_section->result; i++) {
        if (!ctx_push_value(ctx, error_buf, error_buf_size))
            return false;
        ctx->value_count++;
    }

    return true;
}

// The name must be unique
static bool
validate_imports_section(WASMComponentValidationContext *ctx,
                         WASMComponentSection *section, char *error_buf,
                         uint32_t error_buf_size)
{
    WASMComponentImport *imp = NULL;
    for (uint32_t i = 0; i < section->parsed.import_section->count; i++) {
        imp = &section->parsed.import_section->imports[i];

        // Extract the import name - either a plain string or a name with a
        // semver suffix
        const char *name = NULL;
        if (imp->import_name->tag == WASM_COMP_IMPORTNAME_SIMPLE) {
            name = imp->import_name->imported.simple.name->name;
        }
        else {
            name = imp->import_name->imported.versioned.name->name;
        }

        if (bh_hash_map_find(ctx->import_names, (void *)name)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "duplicate import name");
            return false;
        }
        bh_hash_map_insert(
            ctx->import_names, (void *)name,
            (void *)(uintptr_t)1); // NOLINT(performance-no-int-to-ptr);

        WASMComponentExternDesc *desc = imp->extern_desc;
        switch (desc->type) {
            case WASM_COMP_EXTERN_CORE_MODULE:
                if (desc->extern_desc.core_module.type_idx
                    >= ctx->core_type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "import core module type idx out of bounds");
                    return false;
                }
                ctx->core_module_count++;
                break;
            case WASM_COMP_EXTERN_FUNC:
            {
                uint32_t func_type_idx = desc->extern_desc.func.type_idx;
                if (func_type_idx >= ctx->type_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import func type idx out of bounds");
                    return false;
                }
                if (ctx->types[func_type_idx]
                    && ctx->types[func_type_idx]->tag != WASM_COMP_FUNC_TYPE) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "import func type idx must refer to a functype");
                    return false;
                }
                if (!validate_func_name_annotation(ctx, name, func_type_idx,
                                                   error_buf, error_buf_size))
                    return false;
                if (!ctx_push_func_type(ctx, func_type_idx, error_buf,
                                        error_buf_size))
                    return false;
                ctx->func_count++;
                break;
            }
            case WASM_COMP_EXTERN_VALUE:
            {
                WASMComponentValueBound *vb =
                    desc->extern_desc.value.value_bound;
                if (vb->tag == WASM_COMP_VALUEBOUND_EQ) {
                    if (vb->bound.value_idx >= ctx->value_count) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "import value eq idx out of bounds");
                        return false;
                    }
                }
                else {
                    const WASMComponentValueType *vt = vb->bound.value_type;
                    if (vt->type == WASM_COMP_VAL_TYPE_IDX
                        && vt->type_specific.type_idx >= ctx->type_count) {
                        set_error_buf_ex(error_buf, error_buf_size,
                                         "import value type idx out of bounds");
                        return false;
                    }
                }
                if (!ctx_push_value(ctx, error_buf, error_buf_size))
                    return false;
                ctx->value_count++;
                break;
            }
            case WASM_COMP_EXTERN_TYPE:
            {
                // Importing a type introduces a new opaque entry in the type
                // index space
                const WASMComponentTypeBound *tb =
                    desc->extern_desc.type.type_bound;
                if (tb->tag == WASM_COMP_TYPEBOUND_EQ
                    && tb->type_idx >= ctx->type_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import type eq idx out of bounds");
                    return false;
                }
                // Sub-resource import: save the name for [static] annotation
                // validation
                if (tb->tag == WASM_COMP_TYPEBOUND_TYPE) {
                    bh_hash_map_insert(
                        ctx->resource_type_names, (void *)name,
                        (void
                             *)(uintptr_t)1); // NOLINT(performance-no-int-to-ptr);
                }
                if (!ctx_push_type(ctx, NULL, false, error_buf, error_buf_size))
                    return false;
                ctx->type_count++;
                break;
            }
            case WASM_COMP_EXTERN_COMPONENT:
            {
                uint32_t comp_type_idx = desc->extern_desc.component.type_idx;
                if (comp_type_idx >= ctx->type_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import component type idx out of bounds");
                    return false;
                }
                if (ctx->types[comp_type_idx]
                    && ctx->types[comp_type_idx]->tag
                           != WASM_COMP_COMPONENT_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import component type idx must refer to "
                                     "a componenttype");
                    return false;
                }
                ctx->component_count++;
                break;
            }
            case WASM_COMP_EXTERN_INSTANCE:
            {
                uint32_t inst_type_idx = desc->extern_desc.instance.type_idx;
                if (inst_type_idx >= ctx->type_count) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import instance type idx out of bounds");
                    return false;
                }
                if (ctx->types[inst_type_idx]
                    && ctx->types[inst_type_idx]->tag
                           != WASM_COMP_INSTANCE_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "import instance type idx must refer to "
                                     "an instancetype");
                    return false;
                }
                ctx->instance_count++;
                break;
            }
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "invalid extern desc type in import");
                return false;
        }
    }
    return true;
}

// Each export name must be unique within this component, and the thing being
// exported must already be defined
static bool
validate_exports_section(WASMComponentValidationContext *ctx,
                         WASMComponentSection *section, char *error_buf,
                         uint32_t error_buf_size)
{
    WASMComponentExport *exp = NULL;
    for (uint32_t i = 0; i < section->parsed.export_section->count; i++) {
        exp = &section->parsed.export_section->exports[i];
        const char *name = NULL;
        if (exp->export_name->tag == WASM_COMP_IMPORTNAME_SIMPLE) {
            name = exp->export_name->exported.simple.name->name;
        }
        else {
            name = exp->export_name->exported.versioned.name->name;
        }

        // Each export name must be unique
        if (bh_hash_map_find(ctx->export_names, (void *)name)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "duplicate export name");
            return false;
        }
        bh_hash_map_insert(
            ctx->export_names, (void *)name,
            (void *)(uintptr_t)1); // NOLINT(performance-no-int-to-ptr);

        // Check that the referenced item is within bounds for its sort
        uint32_t idx = exp->sort_idx->idx;
        bool in_bounds = false;
        if (exp->sort_idx->sort->sort == WASM_COMP_SORT_CORE_SORT) {
            if (exp->sort_idx->sort->core_sort != WASM_COMP_CORE_SORT_MODULE) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "only core module sort allowed in component export");
                return false;
            }
            in_bounds = idx < ctx->core_module_count;
        }
        else {
            switch (exp->sort_idx->sort->sort) {
                case WASM_COMP_SORT_FUNC:
                    in_bounds = idx < ctx->func_count;
                    break;
                case WASM_COMP_SORT_VALUE:
                    in_bounds = idx < ctx->value_count;
                    break;
                case WASM_COMP_SORT_TYPE:
                    in_bounds = idx < ctx->type_count;
                    break;
                case WASM_COMP_SORT_COMPONENT:
                    in_bounds = idx < ctx->component_count;
                    break;
                case WASM_COMP_SORT_INSTANCE:
                    in_bounds = idx < ctx->instance_count;
                    break;
                default:
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "invalid sort in component export: 0x%02x",
                                     exp->sort_idx->sort->sort);
                    return false;
            }
        }
        if (!in_bounds) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "export sort idx %u out of bounds", idx);
            return false;
        }

        // Validate [constructor]/[method]/[static] annotations on func exports
        if (exp->sort_idx->sort->sort == WASM_COMP_SORT_FUNC) {
            uint32_t exp_ft_idx =
                (idx < ctx->func_count && ctx->func_type_indexes)
                    ? ctx->func_type_indexes[idx]
                    : UINT32_MAX;
            if (!validate_func_name_annotation(ctx, name, exp_ft_idx, error_buf,
                                               error_buf_size))
                return false;
        }

        // If an optional externdesc is present on the export, verify the
        // exported item's type matches the declared externdesc type.
        if (exp->extern_desc) {
            uint8_t sort = exp->sort_idx->sort->sort;

            // externdesc sort must match the exported sort
            if (sort == WASM_COMP_SORT_FUNC
                && exp->extern_desc->type != WASM_COMP_EXTERN_FUNC) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "export externdesc sort mismatch for func");
                return false;
            }
            if (sort == WASM_COMP_SORT_TYPE
                && exp->extern_desc->type != WASM_COMP_EXTERN_TYPE) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "export externdesc sort mismatch for type");
                return false;
            }
            if (sort == WASM_COMP_SORT_COMPONENT
                && exp->extern_desc->type != WASM_COMP_EXTERN_COMPONENT) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "export externdesc sort mismatch for component");
                return false;
            }
            if (sort == WASM_COMP_SORT_INSTANCE
                && exp->extern_desc->type != WASM_COMP_EXTERN_INSTANCE) {
                set_error_buf_ex(
                    error_buf, error_buf_size,
                    "export externdesc sort mismatch for instance");
                return false;
            }

            // For func exports: bounds + kind check, then subtype check
            if (sort == WASM_COMP_SORT_FUNC) {
                uint32_t decl_ft_idx =
                    exp->extern_desc->extern_desc.func.type_idx;
                if (decl_ft_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "export externdesc func type idx out of bounds");
                    return false;
                }
                if (ctx->types[decl_ft_idx]
                    && ctx->types[decl_ft_idx]->tag != WASM_COMP_FUNC_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "export externdesc func type idx must "
                                     "refer to a functype");
                    return false;
                }

                // Subtype check: exported func type <: externdesc func type
                uint32_t exp_ft_idx =
                    (idx < ctx->func_count && ctx->func_type_indexes)
                        ? ctx->func_type_indexes[idx]
                        : UINT32_MAX;
                if (exp_ft_idx != UINT32_MAX && exp_ft_idx != decl_ft_idx
                    && exp_ft_idx < ctx->type_count && ctx->types[exp_ft_idx]
                    && ctx->types[decl_ft_idx]) {
                    if (!component_type_subtype(ctx->types[exp_ft_idx],
                                                ctx->types[decl_ft_idx], ctx)) {
                        set_error_buf_ex(
                            error_buf, error_buf_size,
                            "export func type is not a subtype of externdesc"
                            " (type idx %u vs %u)",
                            exp_ft_idx, decl_ft_idx);
                        return false;
                    }
                }
            }

            // For type exports with eq bound: verify the type_idx is in bounds
            if (sort == WASM_COMP_SORT_TYPE
                && exp->extern_desc->extern_desc.type.type_bound) {
                const WASMComponentTypeBound *bound =
                    exp->extern_desc->extern_desc.type.type_bound;
                if (bound->type_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "export externdesc type bound idx out of bounds");
                    return false;
                }
            }

            // For component exports: verify the type_idx refers to a
            // componenttype
            if (sort == WASM_COMP_SORT_COMPONENT) {
                uint32_t comp_type_idx =
                    exp->extern_desc->extern_desc.component.type_idx;
                if (comp_type_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "export externdesc component type idx out of bounds");
                    return false;
                }
                if (ctx->types[comp_type_idx]
                    && ctx->types[comp_type_idx]->tag
                           != WASM_COMP_COMPONENT_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "export externdesc component type idx "
                                     "must refer to a componenttype");
                    return false;
                }
            }

            // For instance exports: verify the type_idx refers to an
            // instancetype
            if (sort == WASM_COMP_SORT_INSTANCE) {
                uint32_t inst_type_idx =
                    exp->extern_desc->extern_desc.instance.type_idx;
                if (inst_type_idx >= ctx->type_count) {
                    set_error_buf_ex(
                        error_buf, error_buf_size,
                        "export externdesc instance type idx out of bounds");
                    return false;
                }
                if (ctx->types[inst_type_idx]
                    && ctx->types[inst_type_idx]->tag
                           != WASM_COMP_INSTANCE_TYPE) {
                    set_error_buf_ex(error_buf, error_buf_size,
                                     "export externdesc instance type idx must "
                                     "refer to an instancetype");
                    return false;
                }
            }
        }

        // Track resource type names for [static] annotation validation
        if (exp->sort_idx->sort->sort == WASM_COMP_SORT_TYPE
            && idx < ctx->type_count && ctx->types[idx]
            && (ctx->types[idx]->tag == WASM_COMP_RESOURCE_TYPE_SYNC
                || ctx->types[idx]->tag == WASM_COMP_RESOURCE_TYPE_ASYNC)) {
            bh_hash_map_insert(
                ctx->resource_type_names, (void *)name,
                (void *)(uintptr_t)1); // NOLINT(performance-no-int-to-ptr);
        }

        // Exporting a value consumes the original entry in the value index
        // space
        if (exp->sort_idx->sort->sort == WASM_COMP_SORT_VALUE) {
            if (!ctx_consume_value(ctx, idx, error_buf, error_buf_size))
                return false;
        }

        // Each export that validates successfully also claims the next slot in
        // its index space
        if (exp->sort_idx->sort->sort == WASM_COMP_SORT_CORE_SORT) {
            ctx->core_module_count++;
        }
        else {
            switch (exp->sort_idx->sort->sort) {
                case WASM_COMP_SORT_FUNC:
                {
                    // Copy the type index from the original function being
                    // exported
                    uint32_t orig_ft =
                        (idx < ctx->func_count && ctx->func_type_indexes)
                            ? ctx->func_type_indexes[idx]
                            : UINT32_MAX;
                    if (!ctx_push_func_type(ctx, orig_ft, error_buf,
                                            error_buf_size))
                        return false;
                    ctx->func_count++;
                    break;
                }
                case WASM_COMP_SORT_VALUE:
                    // The re-exported value slot is immediately consumed by
                    // this export
                    if (!ctx_push_value(ctx, error_buf, error_buf_size))
                        return false;
                    ctx->value_consumed[ctx->value_count] = true;
                    ctx->value_count++;
                    break;
                case WASM_COMP_SORT_TYPE:
                    if (!ctx_push_type(ctx, NULL, false, error_buf,
                                       error_buf_size))
                        return false;
                    ctx->type_count++;
                    break;
                case WASM_COMP_SORT_COMPONENT:
                    ctx->component_count++;
                    break;
                case WASM_COMP_SORT_INSTANCE:
                    ctx->instance_count++;
                    break;
                default:
                    break;
            }
        }
    }

    return true;
}

static bool
validate_values_section(WASMComponentValidationContext *ctx,
                        WASMComponentSection *section, char *error_buf,
                        uint32_t error_buf_size)
{
    for (uint32_t i = 0; i < section->parsed.value_section->count; i++) {
        WASMComponentValue *val = &section->parsed.value_section->values[i];
        if (val->val_type->type == WASM_COMP_VAL_TYPE_IDX) {
            uint32_t type_idx = val->val_type->type_specific.type_idx;
            if (type_idx >= ctx->type_count) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "value type idx out of bounds");
                return false;
            }
            // A value's type must be a defvaltype
            if (ctx->types[type_idx]
                && ctx->types[type_idx]->tag != WASM_COMP_DEF_TYPE) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "value type idx must refer to a defvaltype");
                return false;
            }
        }
        if (valtype_has_borrow(val->val_type, ctx)) {
            set_error_buf_ex(error_buf, error_buf_size,
                             "borrow type must not appear in value type");
            return false;
        }
        if (!ctx_push_value(ctx, error_buf, error_buf_size))
            return false;
        ctx->value_count++;
    }

    return true;
}

bool
wasm_component_validate(WASMComponent *comp,
                        WASMComponentValidationContext *parent, char *error_buf,
                        uint32_t error_buf_size)
{
    if (!comp) {
        set_error_buf_ex(error_buf, error_buf_size, "invalid component");
        return false;
    }

    WASMComponentValidationContext ctx;
    memset(&ctx, 0, sizeof(WASMComponentValidationContext));
    ctx.parent = parent;

    ctx.import_names =
        bh_hash_map_create(32, false, (HashFunc)wasm_string_hash,
                           (KeyEqualFunc)wasm_string_equal, NULL, NULL);
    if (!ctx.import_names) {
        set_error_buf_ex(error_buf, error_buf_size,
                         "could not allocate hashmap for import names");
        return false;
    }

    ctx.export_names =
        bh_hash_map_create(32, false, (HashFunc)wasm_string_hash,
                           (KeyEqualFunc)wasm_string_equal, NULL, NULL);
    if (!ctx.export_names) {
        bh_hash_map_destroy(ctx.import_names);
        set_error_buf_ex(error_buf, error_buf_size,
                         "could not allocate hashmap for export names");
        return false;
    }

    ctx.resource_type_names =
        bh_hash_map_create(16, false, (HashFunc)wasm_string_hash,
                           (KeyEqualFunc)wasm_string_equal, NULL, NULL);
    if (!ctx.resource_type_names) {
        bh_hash_map_destroy(ctx.import_names);
        bh_hash_map_destroy(ctx.export_names);
        set_error_buf_ex(error_buf, error_buf_size,
                         "could not allocate hashmap for resource type names");
        return false;
    }

    bool ok = true;
    for (uint32_t i = 0; i < comp->section_count && ok; i++) {
        WASMComponentSection *section = &comp->sections[i];

        switch (section->id) {
            case WASM_COMP_SECTION_CORE_CUSTOM:
                break;
            case WASM_COMP_SECTION_CORE_MODULE:
                // Core modules are validated by the regular wasm loader when
                // the binary is loaded
                ctx.core_module_count++;
                break;
            case WASM_COMP_SECTION_CORE_INSTANCE:
                ok = validate_core_instance_section(&ctx, section, error_buf,
                                                    error_buf_size);
                break;
            case WASM_COMP_SECTION_CORE_TYPE:
                ok = validate_core_type_section(&ctx, section, error_buf,
                                                error_buf_size);
                break;
            case WASM_COMP_SECTION_COMPONENT:
                ok = validate_component_section(&ctx, section, error_buf,
                                                error_buf_size);
                break;
            case WASM_COMP_SECTION_INSTANCES:
                ok = validate_instances_section(&ctx, section, error_buf,
                                                error_buf_size);
                break;
            case WASM_COMP_SECTION_ALIASES:
                ok = validate_aliases_section(&ctx, section, error_buf,
                                              error_buf_size);
                break;
            case WASM_COMP_SECTION_TYPE:
                ok = validate_type_section(&ctx, section, error_buf,
                                           error_buf_size);
                break;
            case WASM_COMP_SECTION_CANONS:
                ok = validate_canons_section(&ctx, section, error_buf,
                                             error_buf_size);
                break;
            case WASM_COMP_SECTION_START:
                ok = validate_start_section(&ctx, section, error_buf,
                                            error_buf_size);
                break;
            case WASM_COMP_SECTION_IMPORTS:
                ok = validate_imports_section(&ctx, section, error_buf,
                                              error_buf_size);
                break;
            case WASM_COMP_SECTION_EXPORTS:
                ok = validate_exports_section(&ctx, section, error_buf,
                                              error_buf_size);
                break;
            case WASM_COMP_SECTION_VALUES:
                ok = validate_values_section(&ctx, section, error_buf,
                                             error_buf_size);
                break;
            default:
                set_error_buf_ex(error_buf, error_buf_size,
                                 "invalid section id: %d", section->id);
                ok = false;
                break;
        }
    }

    // Every value must have been consumed exactly once
    if (ok) {
        for (uint32_t i = 0; i < ctx.value_count; i++) {
            if (!ctx.value_consumed[i]) {
                set_error_buf_ex(error_buf, error_buf_size,
                                 "value %u is never consumed", i);
                ok = false;
                break;
            }
        }
    }

    bh_hash_map_destroy(ctx.import_names);
    bh_hash_map_destroy(ctx.export_names);
    bh_hash_map_destroy(ctx.resource_type_names);

    if (ctx.types)
        wasm_runtime_free((void *)ctx.types);

    if (ctx.type_is_local)
        wasm_runtime_free(ctx.type_is_local);

    if (ctx.value_consumed)
        wasm_runtime_free(ctx.value_consumed);

    if (ctx.func_type_indexes)
        wasm_runtime_free(ctx.func_type_indexes);

    return ok;
}

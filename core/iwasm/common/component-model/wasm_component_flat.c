/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_flat.h"
#include "bh_assert.h"

void
flat_types_init(FlatTypes *ft)
{
    ft->count = 0;
}

bool
flat_types_push(FlatTypes *ft, CoreValType t)
{
    if (ft->count >= MAX_FLAT_TYPES)
        return false;
    ft->types[ft->count++] = t;
    return true;
}

bool
flat_types_extend(FlatTypes *dst, const FlatTypes *src)
{
    for (uint32_t i = 0; i < src->count; i++) {
        if (!flat_types_push(dst, src->types[i]))
            return false;
    }

    return true;
}

static bool
flatten_type_primitive(LiftLowerContext *cx, WASMComponentPrimValType primval,
                       FlatTypes *out)
{
    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
        case WASM_COMP_PRIMVAL_U8:
        case WASM_COMP_PRIMVAL_U16:
        case WASM_COMP_PRIMVAL_U32:
        case WASM_COMP_PRIMVAL_S8:
        case WASM_COMP_PRIMVAL_S16:
        case WASM_COMP_PRIMVAL_S32:
        {
            return flat_types_push(out, CORE_TYPE_I32);
        }

        case WASM_COMP_PRIMVAL_S64:
        case WASM_COMP_PRIMVAL_U64:
        {
            return flat_types_push(out, CORE_TYPE_I64);
        }

        case WASM_COMP_PRIMVAL_F32:
        {
            return flat_types_push(out, CORE_TYPE_F32);
        }

        case WASM_COMP_PRIMVAL_F64:
        {
            return flat_types_push(out, CORE_TYPE_F64);
        }

        case WASM_COMP_PRIMVAL_CHAR:
        {
            return flat_types_push(out, CORE_TYPE_I32);
        }

        case WASM_COMP_PRIMVAL_STRING:
        {
            if (!flat_types_push(out, CORE_TYPE_I32))
                return false;
            return flat_types_push(out, CORE_TYPE_I32);
        }

        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
        {
            return flat_types_push(out, CORE_TYPE_I32);
        }

        default:
        {
            set_component_exception(cx, "invalid type for flatten primitive");
            return false;
        }
    }
}

static bool
flatten_type_list(LiftLowerContext *cx, WASMComponentTypeInstance *type,
                  uint32_t maybe_length, bool is_fixed, FlatTypes *out)
{
    if (is_fixed) {
        FlatTypes elem_flat;
        if (!flatten_type(cx, type, &elem_flat))
            return false;

        for (uint32_t i = 0; i < maybe_length; i++) {
            if (!flat_types_extend(out, &elem_flat))
                return false;
        }

        return true;
    }

    if (!flat_types_push(out, CORE_TYPE_I32))
        return false;
    return flat_types_push(out, CORE_TYPE_I32);
}

static bool
flatten_type_record(LiftLowerContext *cx, WASMComponentRecordInstance *type,
                    FlatTypes *out)
{
    for (uint32_t i = 0; i < type->count; i++) {
        FlatTypes field_flat;
        if (!flatten_type(cx, type->fields[i].type, &field_flat))
            return false;
        if (!flat_types_extend(out, &field_flat))
            return false;
    }

    return true;
}

static CoreValType
join(CoreValType a, CoreValType b)
{
    if (a == b)
        return a;
    if ((a == CORE_TYPE_I32 && b == CORE_TYPE_F32)
        || (a == CORE_TYPE_F32 && b == CORE_TYPE_I32))
        return CORE_TYPE_I32;
    return CORE_TYPE_I64;
}

static bool
flatten_type_variant(LiftLowerContext *cx, WASMComponentVariantInstance *type,
                     FlatTypes *out)
{
    FlatTypes joined;
    flat_types_init(&joined);

    for (uint32_t c = 0; c < type->count; c++) {
        if (type->cases[c].value_type == NULL) {
            continue;
        }

        FlatTypes case_flat;
        if (!flatten_type(cx, type->cases[c].value_type, &case_flat))
            return false;

        for (uint32_t i = 0; i < case_flat.count; i++) {
            if (i < joined.count) {
                joined.types[i] = join(joined.types[i], case_flat.types[i]);
            }
            else {
                if (!flat_types_push(&joined, case_flat.types[i]))
                    return false;
            }
        }
    }

    // Discriminant is always i32 in flat representation
    if (!flat_types_push(out, CORE_TYPE_I32))
        return false;

    return flat_types_extend(out, &joined);
}

static bool
flatten_type_tuple(LiftLowerContext *cx, WASMComponentTupleInstance *type,
                   FlatTypes *out)
{
    for (uint32_t i = 0; i < type->count; i++) {
        FlatTypes field_flat;
        if (!flatten_type(cx, type->element_types[i], &field_flat))
            return false;
        if (!flat_types_extend(out, &field_flat))
            return false;
    }
    return true;
}

static bool
flatten_type_enum(LiftLowerContext *cx, WASMComponentEnumType *type,
                  FlatTypes *out)
{
    // Enum has only discriminant, no payloads
    // Discriminant is always i32 in flat representation
    return flat_types_push(out, CORE_TYPE_I32);
}

static bool
flatten_type_option(LiftLowerContext *cx, WASMComponentOptionInstance *type,
                    FlatTypes *out)
{
    // Option = variant { none, some(T) }
    FlatTypes joined;
    flat_types_init(&joined);

    // Case 0 "none": no payload
    // Case 1 "some": has payload of type->element_type
    if (type->element_type != NULL) {
        FlatTypes case_flat;
        if (!flatten_type(cx, type->element_type, &case_flat))
            return false;

        // Only one case has payload, so no joining needed
        if (!flat_types_extend(&joined, &case_flat))
            return false;
    }

    // Discriminant is always i32
    if (!flat_types_push(out, CORE_TYPE_I32))
        return false;

    return flat_types_extend(out, &joined);
}

static bool
flatten_type_result(LiftLowerContext *cx, WASMComponentResultInstance *type,
                    FlatTypes *out)
{
    // Result = variant { ok(T), error(E) }
    FlatTypes joined;
    flat_types_init(&joined);

    // Case 0 "ok": payload type->result_type
    if (type->result_type != NULL) {
        FlatTypes case_flat;
        if (!flatten_type(cx, type->result_type, &case_flat))
            return false;

        for (uint32_t i = 0; i < case_flat.count; i++) {
            if (!flat_types_push(&joined, case_flat.types[i]))
                return false;
        }
    }

    // Case 1 "error": payload type->error_type
    if (type->error_type != NULL) {
        FlatTypes case_flat;
        if (!flatten_type(cx, type->error_type, &case_flat))
            return false;

        // Join with "ok" case
        for (uint32_t i = 0; i < case_flat.count; i++) {
            if (i < joined.count) {
                joined.types[i] = join(joined.types[i], case_flat.types[i]);
            }
            else {
                if (!flat_types_push(&joined, case_flat.types[i]))
                    return false;
            }
        }
    }

    // Discriminant is always i32
    if (!flat_types_push(out, CORE_TYPE_I32))
        return false;

    return flat_types_extend(out, &joined);
}

bool
flatten_type(LiftLowerContext *cx, WASMComponentTypeInstance *type,
             FlatTypes *out)
{
    flat_types_init(out);
    switch (type->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
        {
            return flatten_type_primitive(cx, type->type_specific.primval, out);
        }

        case COMPONENT_VAL_TYPE_LIST:
        {
            return flatten_type_list(cx, type->type_specific.list->element_type,
                                     0, false, out);
        }

        case COMPONENT_VAL_TYPE_FIXED_SIZE_LIST:
        {
            return flatten_type_list(
                cx, type->type_specific.list_len->element_type,
                type->type_specific.list_len->len, true, out);
        }

        case COMPONENT_VAL_TYPE_RECORD:
        {
            return flatten_type_record(cx, type->type_specific.record, out);
        }

        case COMPONENT_VAL_TYPE_VARIANT:
        {
            return flatten_type_variant(cx, type->type_specific.variant, out);
        }

        case COMPONENT_VAL_TYPE_TUPLE:
        {
            return flatten_type_tuple(cx, type->type_specific.tuple, out);
        }

        case COMPONENT_VAL_TYPE_ENUM:
        {
            return flatten_type_enum(cx, type->type_specific.enum_type, out);
        }

        case COMPONENT_VAL_TYPE_OPTION:
        {
            return flatten_type_option(cx, type->type_specific.option, out);
        }

        case COMPONENT_VAL_TYPE_RESULT:
        {
            return flatten_type_result(cx, type->type_specific.result, out);
        }

        case COMPONENT_VAL_TYPE_FLAGS:
        case COMPONENT_VAL_TYPE_OWN:
        case COMPONENT_VAL_TYPE_BORROW:
        case COMPONENT_VAL_TYPE_STREAM:
        case COMPONENT_VAL_TYPE_FUTURE:
        {
            return flat_types_push(out, CORE_TYPE_I32);
        }

        default:
        {
            break;
        }
    }

    return false;
}

bool
flatten_param_types(LiftLowerContext *cx,
                    WASMComponentParamListInstance *params, FlatTypes *out)
{
    for (uint32_t i = 0; i < params->count; i++) {
        FlatTypes param_flat;
        if (!flatten_type(cx, params->params[i].type, &param_flat)) {
            return false;
        }
        if (!flat_types_extend(out, &param_flat)) {
            return false;
        }
    }

    return true;
}

bool
flatten_result_types(LiftLowerContext *cx,
                     WASMComponentResultListInstance *results, FlatTypes *out)
{
    if (results->tag == WASM_COMP_RESULT_LIST_EMPTY || results->count == 0) {
        return true;
    }

    for (uint32_t i = 0; i < results->count; i++) {
        FlatTypes result_flat;
        if (!flatten_type(cx, &results->result[i], &result_flat)) {
            return false;
        }
        if (!flat_types_extend(out, &result_flat)) {
            return false;
        }
    }

    return true;
}

static WASMComponentCoreValType
to_wasm_core_valtype(CoreValType t)
{
    WASMComponentCoreValType result;
    result.tag = WASM_CORE_VALTYPE_NUM;
    switch (t) {
        case CORE_TYPE_I32:
            result.type.num_type = WASM_CORE_NUM_TYPE_I32;
            break;
        case CORE_TYPE_I64:
            result.type.num_type = WASM_CORE_NUM_TYPE_I64;
            break;
        case CORE_TYPE_F32:
            result.type.num_type = WASM_CORE_NUM_TYPE_F32;
            break;
        case CORE_TYPE_F64:
            result.type.num_type = WASM_CORE_NUM_TYPE_F64;
            break;
    }
    return result;
}

static bool
flat_types_to_core_resulttype(FlatTypes *flat, WASMComponentCoreResultType *out)
{
    out->count = flat->count;

    if (flat->count == 0) {
        out->val_types = NULL;
        return true;
    }

    out->val_types =
        wasm_runtime_malloc(flat->count * sizeof(WASMComponentCoreValType));
    if (!out->val_types) {
        return false;
    }

    for (uint32_t i = 0; i < flat->count; i++) {
        out->val_types[i] = to_wasm_core_valtype(flat->types[i]);
    }
    return true;
}

bool
flatten_functype(LiftLowerContext *cx, WASMComponentFuncTypeInstance *ft,
                 FlattenContext context, WASMComponentCoreFuncType *out)
{
    FlatTypes flat_params;
    FlatTypes flat_results;

    flat_types_init(&flat_params);
    flat_types_init(&flat_results);

    // Flatten param
    if (!flatten_param_types(cx, ft->params, &flat_params)) {
        set_component_exception(cx, "cannot flat types for params");
        return false;
    }

    // Flatten result
    if (!flatten_result_types(cx, ft->results, &flat_results)) {
        set_component_exception(cx, "cannot flat types for results");
        return false;
    }

    if (!cx->canonical_opts->async) { // Sync
        if (flat_params.count > MAX_FLAT_PARAMS) {
            flat_types_init(&flat_params);
            flat_types_push(&flat_params, CORE_TYPE_I32);
        }

        if (flat_results.count > MAX_FLAT_RESULTS) {
            if (context == FLATTEN_CONTEXT_LIFT) {
                flat_types_init(&flat_results);
                flat_types_push(&flat_results, CORE_TYPE_I32);
            }
            else if (context == FLATTEN_CONTEXT_LOWER) {
                flat_types_push(&flat_params, CORE_TYPE_I32);
                flat_types_init(&flat_results);
            }
        }

        // Build CoreFuncType
        if (!flat_types_to_core_resulttype(&flat_params, &out->params)) {
            set_component_exception(cx, "failed to build core params");
            return false;
        }

        if (!flat_types_to_core_resulttype(&flat_results, &out->results)) {
            // Clean up params on failure
            if (out->params.val_types) {
                wasm_runtime_free(out->params.val_types);
            }
            set_component_exception(cx, "failed to build core results");
            return false;
        }

        return true;
    }
    else {
        if (context == FLATTEN_CONTEXT_LIFT) {
            if (flat_params.count > MAX_FLAT_PARAMS) {
                flat_types_init(&flat_params);
                flat_types_push(&flat_params, CORE_TYPE_I32);
            }

            if (cx->canonical_opts->callback_func != NULL) {
                flat_types_init(&flat_results);
                flat_types_push(&flat_results, CORE_TYPE_I32);
            }
            else {
                flat_types_init(&flat_results);
            }
        }
        else if (context == FLATTEN_CONTEXT_LOWER) {
            if (flat_params.count > MAX_FLAT_ASYNC_PARAMS) {
                flat_types_init(&flat_params);
                flat_types_push(&flat_params, CORE_TYPE_I32);
            }

            if (flat_results.count > 0) {
                flat_types_push(&flat_params, CORE_TYPE_I32);
            }

            flat_types_init(&flat_results);
            flat_types_push(&flat_results, CORE_TYPE_I32);
        }

        // Build CoreFuncType
        if (!flat_types_to_core_resulttype(&flat_params, &out->params)) {
            set_component_exception(cx, "failed to build core params");
            return false;
        }

        if (!flat_types_to_core_resulttype(&flat_results, &out->results)) {
            // Clean up params on failure
            if (out->params.val_types) {
                wasm_runtime_free(out->params.val_types);
            }
            set_component_exception(cx, "failed to build core results");
            return false;
        }

        return true;
    }

    return false;
}

static uint32_t
wrap_i64_to_i32(uint64_t i)
{
    return (uint32_t)(i & 0xFFFFFFFFULL);
}

static CoreValue
coerce_value(CoreValue v, CoreValType have, CoreValType want)
{
    CoreValue result;
    result.type = want;

    if (have == want) {
        return v;
    }

    if (have == CORE_TYPE_I32 && want == CORE_TYPE_F32) {
        result.val.f32 = core_f32_reinterpret_i32(v.val.i32);
    }
    else if (have == CORE_TYPE_I64 && want == CORE_TYPE_I32) {
        result.val.i32 = wrap_i64_to_i32(v.val.i64);
    }
    else if (have == CORE_TYPE_I64 && want == CORE_TYPE_F32) {
        result.val.f32 = core_f32_reinterpret_i32(wrap_i64_to_i32(v.val.i64));
    }
    else if (have == CORE_TYPE_I64 && want == CORE_TYPE_F64) {
        result.val.f64 = core_f64_reinterpret_i64(v.val.i64);
    }
    else {
        // Should not happen
        bh_assert(have == want);
        return v;
    }

    return result;
}

static bool
cvl_push(CoreValueList *list, CoreValue val)
{
    if (list->count >= MAX_FLAT_TYPES)
        return false;

    switch (val.type) {
        case CORE_TYPE_I32:
        {
            return cvl_push_i32(list, val.val.i32);
        }

        case CORE_TYPE_I64:
        {
            return cvl_push_i64(list, val.val.i64);
        }

        case CORE_TYPE_F32:
        {
            return cvl_push_f32(list, val.val.f32);
        }

        case CORE_TYPE_F64:
        {
            return cvl_push_f64(list, val.val.f64);
        }

        default:
        {
            return false;
        }
    }

    return true;
}

void
cvl_init(CoreValueList *list)
{
    list->count = 0;
}

bool
cvl_push_i32(CoreValueList *list, uint32_t v)
{
    if (list->count >= MAX_FLAT_TYPES)
        return false;
    CoreValue val;
    val.val.i32 = v;
    val.type = CORE_TYPE_I32;
    list->values[list->count++] = val;
    return true;
}

bool
cvl_push_i64(CoreValueList *list, uint64_t v)
{
    if (list->count >= MAX_FLAT_TYPES)
        return false;
    CoreValue val;
    val.val.i64 = v;
    val.type = CORE_TYPE_I64;
    list->values[list->count++] = val;
    return true;
}

bool
cvl_push_f32(CoreValueList *list, float v)
{
    if (list->count >= MAX_FLAT_TYPES)
        return false;
    CoreValue val;
    val.val.f32 = v;
    val.type = CORE_TYPE_F32;
    list->values[list->count++] = val;
    return true;
}

bool
cvl_push_f64(CoreValueList *list, double v)
{
    if (list->count >= MAX_FLAT_TYPES)
        return false;
    CoreValue val;
    val.val.f64 = v;
    val.type = CORE_TYPE_F64;
    list->values[list->count++] = val;
    return true;
}

bool
cvl_extend(CoreValueList *dst, const CoreValueList *src)
{
    for (uint32_t i = 0; i < src->count; i++) {
        if (!cvl_push(dst, src->values[i]))
            return false;
    }

    return true;
}

CoreValue
vi_next(CoreValueIter *vi, CoreValType want)
{
    if (vi->parent != NULL) {
        // Coercion mode: read from parent with "have" type, coerce to "want"
        bh_assert(vi->coerce_index < vi->coerce_types->count);
        CoreValType have = vi->coerce_types->types[vi->coerce_index++];
        CoreValue x = vi_next(vi->parent, have); // Recursive call to parent
        vi->index++; // Track how many values consumed
        return coerce_value(x, have, want);
    }

    // Normal mode
    bh_assert(vi->index < vi->count);
    const CoreValue *v = &vi->values[vi->index++];
    bh_assert(v->type == want);
    return *v;
}

uint32_t
vi_next_i32(CoreValueIter *vi)
{
    return vi_next(vi, CORE_TYPE_I32).val.i32;
}

uint64_t
vi_next_i64(CoreValueIter *vi)
{
    return vi_next(vi, CORE_TYPE_I64).val.i64;
}

float
vi_next_f32(CoreValueIter *vi)
{
    return vi_next(vi, CORE_TYPE_F32).val.f32;
}

double
vi_next_f64(CoreValueIter *vi)
{
    return vi_next(vi, CORE_TYPE_F64).val.f64;
}

bool
vi_done(const CoreValueIter *vi)
{
    return vi->index >= vi->count;
}

void
vi_init(CoreValueIter *vi, CoreValue *values, uint32_t count)
{
    vi->values = values;
    vi->count = count;
    vi->index = 0;
    // Normal mode - no coercion
    vi->parent = NULL;
    vi->coerce_types = NULL;
    vi->coerce_index = 0;
}

void
vi_init_coerce(CoreValueIter *vi, CoreValueIter *parent,
               FlatTypes *coerce_types, uint32_t start_index)
{
    // Coercion mode - read from parent iterator with type coercion
    vi->values = NULL;
    vi->count = 0;
    vi->index = 0; // Track how many values were used
    vi->parent = parent;
    vi->coerce_types = coerce_types;
    vi->coerce_index = start_index;
}

static uint64_t
lift_flat_unsigned(CoreValueIter *vi, uint32_t core_width, uint32_t t_width)
{
    uint64_t i = (core_width == 32) ? vi_next_i32(vi) : vi_next_i64(vi);
    if (t_width >= 64)
        return i;
    return i & ((1ULL << t_width) - 1);
}

static int64_t
lift_flat_signed(CoreValueIter *vi, uint32_t core_width, uint32_t t_width)
{
    uint64_t i = (core_width == 32) ? vi_next_i32(vi) : vi_next_i64(vi);
    if (t_width >= 64)
        return (int64_t)i;

    i &= ((1ULL << t_width) - 1);
    if (i >= (1ULL << (t_width - 1))) {
        return (int64_t)(i - (1ULL << t_width));
    }
    return (int64_t)i;
}

static bool
lift_flat_primitive(LiftLowerContext *cx, CoreValueIter *vi,
                    WASMComponentPrimValType primval, wit_value_t *out)
{
    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
        {
            *out = wit_bool_ctor(vi_next_i32(vi) != 0);
            return true;
        }

        case WASM_COMP_PRIMVAL_U8:
        {
            *out = wit_u8_ctor((uint8_t)lift_flat_unsigned(vi, 32, 8));
            return true;
        }

        case WASM_COMP_PRIMVAL_U16:
        {
            *out = wit_u16_ctor((uint16_t)lift_flat_unsigned(vi, 32, 16));
            return true;
        }

        case WASM_COMP_PRIMVAL_U32:
        {
            *out = wit_u32_ctor((uint32_t)lift_flat_unsigned(vi, 32, 32));
            return true;
        }

        case WASM_COMP_PRIMVAL_U64:
        {
            *out = wit_u64_ctor(lift_flat_unsigned(vi, 64, 64));
            return true;
        }

        case WASM_COMP_PRIMVAL_S8:
        {
            *out = wit_s8_ctor((int8_t)lift_flat_signed(vi, 32, 8));
            return true;
        }

        case WASM_COMP_PRIMVAL_S16:
        {
            *out = wit_s16_ctor((int16_t)lift_flat_signed(vi, 32, 16));
            return true;
        }

        case WASM_COMP_PRIMVAL_S32:
        {
            *out = wit_s32_ctor((int32_t)lift_flat_signed(vi, 32, 32));
            return true;
        }

        case WASM_COMP_PRIMVAL_S64:
        {
            *out = wit_s64_ctor(lift_flat_signed(vi, 64, 64));
            return true;
        }

        case WASM_COMP_PRIMVAL_F32:
        {
            *out = wit_f32_ctor(canonicalize_nan32(vi_next_f32(vi)));
            return true;
        }

        case WASM_COMP_PRIMVAL_F64:
        {
            *out = wit_f64_ctor(canonicalize_nan64(vi_next_f64(vi)));
            return true;
        }

        case WASM_COMP_PRIMVAL_CHAR:
        {
            uint32_t c = 0;
            if (!convert_i32_to_char(cx, vi_next_i32(vi), &c)) {
                return false;
            }

            *out = wit_char_ctor(c);
            return true;
        }

        case WASM_COMP_PRIMVAL_STRING:
        {
            uint32_t ptr = vi_next_i32(vi);
            uint32_t tagged_code_units = vi_next_i32(vi);
            return load_string_from_range(cx, ptr, tagged_code_units, out);
        }

        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
        {
            uint32_t index = vi_next_i32(vi);
            return lift_error_context(cx, index, out);
        }

        default:
        {
            return false;
        }
    }
}

static bool
lift_flat_list(LiftLowerContext *cx, CoreValueIter *vi,
               WASMComponentTypeInstance *type, uint32_t maybe_length,
               bool is_fixed_size, wit_value_t *out)
{
    if (is_fixed_size) {
        wit_value_t *elements = (wit_value_t *)wasm_runtime_malloc(
            maybe_length * sizeof(wit_value_t));
        if (!elements) {
            set_component_exception(cx, "allocation failed");
            return false;
        }

        for (uint32_t i = 0; i < maybe_length; i++) {
            if (!lift_flat(cx, vi, type, &elements[i])) {
                return false;
            }
        }

        *out = wit_list_ctor(elements, maybe_length);

        return true;
    }

    uint32_t ptr = vi_next_i32(vi);
    uint32_t length = vi_next_i32(vi);

    return load_list_from_range(cx, ptr, length, type, out);
}

static bool
lift_flat_record(LiftLowerContext *cx, CoreValueIter *vi,
                 WASMComponentRecordInstance *type, wit_value_t *out)
{
    ComponentWITRecordField *elements =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            type->count * sizeof(ComponentWITRecordField));
    if (!elements) {
        set_component_exception(cx, "out of memory for record elements");
        return false;
    }

    wit_value_t temp = NULL;

    for (uint32_t field = 0; field < type->count; field++) {
        temp = NULL;

        if (!lift_flat(cx, vi, type->fields[field].type, &temp)) {
            free_wit_value(temp);
            for (uint32_t index = 0; index < field; index++) {
                free_wit_value(elements[index].value);
                wasm_runtime_free(elements[index].key);
            }
            wasm_runtime_free(elements);
            return false;
        }

        init_record_field(&elements[field], type->fields[field].label->name,
                          type->fields[field].label->name_len, temp);
    }

    *out = wit_record_ctor(elements, type->count);

    return true;
}

static bool
lift_flat_variant(LiftLowerContext *cx, CoreValueIter *vi,
                  WASMComponentVariantInstance *type, wit_value_t *out)
{
    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_type_variant(cx, type, &flat_types)) {
        return false;
    }

    bh_assert(flat_types.types[0] == CORE_TYPE_I32);

    uint32_t case_index = vi_next_i32(vi);
    trap_if(case_index >= type->count);

    WASMComponentCaseValInstance c = type->cases[case_index];

    wit_value_t v = NULL;
    uint32_t skip_start = 1; // Start after discriminant

    if (c.value_type != NULL) {
        // Create coercing iterator wrapping the original vi
        CoreValueIter coerce_vi;
        vi_init_coerce(&coerce_vi, vi, &flat_types,
                       1); // start_index=1 skips discriminant

        if (!lift_flat(cx, &coerce_vi, c.value_type, &v)) {
            return false;
        }

        skip_start = coerce_vi.coerce_index;
    }

    // Skip remaining payload slots
    for (uint32_t i = skip_start; i < flat_types.count; i++) {
        vi_next(vi, flat_types.types[i]);
    }

    // Build variant: { c.label: v }
    *out = wit_variant_ctor(c.label->name, c.label->name_len, v);

    return true;
}

static bool
lift_flat_flags(LiftLowerContext *cx, CoreValueIter *vi,
                WASMComponentFlagType *type, wit_value_t *out)
{
    bh_assert(0 < type->count && type->count <= 32);
    uint32_t i = vi_next_i32(vi);
    return unpack_flags_from_int(cx, i, type, out);
}

static bool
lift_flat_tuple(LiftLowerContext *cx, CoreValueIter *vi,
                WASMComponentTupleInstance *type, wit_value_t *out)
{
    wit_value_t *elems =
        (wit_value_t *)wasm_runtime_malloc(type->count * sizeof(wit_value_t));
    if (!elems) {
        set_component_exception(cx, "out of memory for tuple elements");
        return false;
    }

    for (uint32_t field = 0; field < type->count; field++) {
        elems[field] = NULL;

        if (!lift_flat(cx, vi, type->element_types[field], &elems[field])) {
            free_wit_value(elems[field]);
            for (uint32_t index = 0; index < field; index++) {
                free_wit_value(elems[index]);
            }
            wasm_runtime_free((void *)elems);
            return false;
        }
    }

    *out = wit_tuple_ctor(elems, type->count);
    return true;
}

static bool
lift_flat_enum(LiftLowerContext *cx, CoreValueIter *vi,
               const WASMComponentEnumType *type, wit_value_t *out)
{
    // Read discriminant only - it has no payload
    uint32_t case_index = vi_next_i32(vi);
    trap_if(case_index >= type->count);

    *out = wit_enum_ctor(case_index);
    return true;
}

static bool
lift_flat_option(LiftLowerContext *cx, CoreValueIter *vi,
                 WASMComponentOptionInstance *type, wit_value_t *out)
{
    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_type_option(cx, type, &flat_types)) {
        return false;
    }

    bh_assert(flat_types.types[0] == CORE_TYPE_I32);

    uint32_t case_index = vi_next_i32(vi);
    trap_if(case_index >= 2);

    wit_value_t v = NULL;
    uint32_t skip_start = 1;

    if (case_index == 1 && type->element_type != NULL) {
        // Case "some" with payload
        CoreValueIter coerce_vi;
        vi_init_coerce(&coerce_vi, vi, &flat_types, 1);

        if (!lift_flat(cx, &coerce_vi, type->element_type, &v)) {
            return false;
        }

        skip_start = coerce_vi.coerce_index;
    }

    // Skip remaining payload slots
    for (uint32_t i = skip_start; i < flat_types.count; i++) {
        vi_next(vi, flat_types.types[i]);
    }

    if (case_index == 0) {
        *out = wit_option_ctor(NULL);
    }
    else {
        *out = wit_option_ctor(v);
    }

    return true;
}

static bool
lift_flat_result(LiftLowerContext *cx, CoreValueIter *vi,
                 WASMComponentResultInstance *type, wit_value_t *out)
{
    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_type_result(cx, type, &flat_types)) {
        return false;
    }

    bh_assert(flat_types.types[0] == CORE_TYPE_I32);

    uint32_t case_index = vi_next_i32(vi);
    trap_if(case_index >= 2);

    wit_value_t v = NULL;
    uint32_t skip_start = 1;

    WASMComponentTypeInstance *payload_type =
        (case_index == 0) ? type->result_type : type->error_type;

    if (payload_type != NULL) {
        CoreValueIter coerce_vi;
        vi_init_coerce(&coerce_vi, vi, &flat_types, 1);

        if (!lift_flat(cx, &coerce_vi, payload_type, &v)) {
            return false;
        }

        skip_start = coerce_vi.coerce_index;
    }

    // Skip remaining payload slots
    for (uint32_t i = skip_start; i < flat_types.count; i++) {
        vi_next(vi, flat_types.types[i]);
    }

    if (case_index == 0) {
        *out = wit_result_ctor(false, v);
    }
    else {
        *out = wit_result_ctor(true, v);
    }

    return true;
}

bool
lift_flat(LiftLowerContext *cx, CoreValueIter *vi,
          WASMComponentTypeInstance *type, wit_value_t *out)
{
    switch (type->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
        {
            return lift_flat_primitive(cx, vi, type->type_specific.primval,
                                       out);
        }

        case COMPONENT_VAL_TYPE_LIST:
        {
            return lift_flat_list(
                cx, vi, type->type_specific.list->element_type, 0, false, out);
        }

        case COMPONENT_VAL_TYPE_FIXED_SIZE_LIST:
        {
            return lift_flat_list(cx, vi,
                                  type->type_specific.list_len->element_type,
                                  type->type_specific.list_len->len, true, out);
        }

        case COMPONENT_VAL_TYPE_RECORD:
        {
            return lift_flat_record(cx, vi, type->type_specific.record, out);
        }

        case COMPONENT_VAL_TYPE_VARIANT:
        {
            return lift_flat_variant(cx, vi, type->type_specific.variant, out);
        }

        case COMPONENT_VAL_TYPE_FLAGS:
        {
            return lift_flat_flags(cx, vi, type->type_specific.flag, out);
        }

        case COMPONENT_VAL_TYPE_TUPLE:
        {
            return lift_flat_tuple(cx, vi, type->type_specific.tuple, out);
        }

        case COMPONENT_VAL_TYPE_ENUM:
        {
            return lift_flat_enum(cx, vi, type->type_specific.enum_type, out);
        }

        case COMPONENT_VAL_TYPE_OPTION:
        {
            return lift_flat_option(cx, vi, type->type_specific.option, out);
        }

        case COMPONENT_VAL_TYPE_RESULT:
        {
            return lift_flat_result(cx, vi, type->type_specific.result, out);
        }

        case COMPONENT_VAL_TYPE_OWN:
        {
            uint32_t i = vi_next_i32(vi);
            return lift_own(cx, i, type->type_specific.resource_handle, out);
        }

        case COMPONENT_VAL_TYPE_BORROW:
        {
            uint32_t i = vi_next_i32(vi);
            return lift_borrow(cx, i, type->type_specific.resource_handle, out);
        }

        default:
        {
            break;
        }
    }

    set_component_exception(cx, "invalid type to lift_flat");
    return false;
}

// Lower Flat

static uint64_t
lower_flat_signed(int64_t i, uint32_t core_bits)
{
    if (core_bits >= 64)
        return (uint64_t)i;

    if (i < 0) {
        i = (int64_t)((uint64_t)i + (1ULL << core_bits));
    }

    return i;
}

static bool
lower_flat_primitive(LiftLowerContext *cx, WASMComponentPrimValType primval,
                     CoreValueList *out, wit_value_t value)
{
    switch (primval) {
        case WASM_COMP_PRIMVAL_BOOL:
        {
            return cvl_push_i32(out, (uint32_t)value->value.bool_value);
        }

        case WASM_COMP_PRIMVAL_U8:
        {
            return cvl_push_i32(out, (uint32_t)value->value.u8_value);
        }

        case WASM_COMP_PRIMVAL_U16:
        {
            return cvl_push_i32(out, (uint32_t)value->value.u16_value);
        }

        case WASM_COMP_PRIMVAL_U32:
        {
            return cvl_push_i32(out, value->value.u32_value);
        }

        case WASM_COMP_PRIMVAL_U64:
        {
            return cvl_push_i64(out, value->value.u64_value);
        }

        case WASM_COMP_PRIMVAL_S8:
        {
            return cvl_push_i32(
                out, lower_flat_signed((int64_t)value->value.s8_value, 32));
        }

        case WASM_COMP_PRIMVAL_S16:
        {
            return cvl_push_i32(
                out, lower_flat_signed((int64_t)value->value.s16_value, 32));
        }

        case WASM_COMP_PRIMVAL_S32:
        {
            return cvl_push_i32(
                out, lower_flat_signed((int64_t)value->value.s32_value, 32));
        }

        case WASM_COMP_PRIMVAL_S64:
        {
            return cvl_push_i64(out,
                                lower_flat_signed(value->value.s64_value, 64));
        }

        case WASM_COMP_PRIMVAL_F32:
        {
            return cvl_push_f32(out,
                                maybe_scramble_nan32(value->value.f32_value));
        }

        case WASM_COMP_PRIMVAL_F64:
        {
            return cvl_push_f64(out,
                                maybe_scramble_nan64(value->value.f64_value));
        }

        case WASM_COMP_PRIMVAL_CHAR:
        {
            uint32_t validated = 0;
            if (!char_to_i32(cx, value->value.char_value, &validated))
                return false;
            return cvl_push_i32(out, validated);
        }

        case WASM_COMP_PRIMVAL_STRING:
        {
            uint32_t ptr = 0, tagged_code_units = 0;
            if (!store_string_into_range(cx, value, &ptr, &tagged_code_units))
                return false;
            return cvl_push_i32(out, ptr)
                   && cvl_push_i32(out, tagged_code_units);
        }

        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
        {
            uint32_t index = 0;

            if (!lower_error_context(cx, value, &index)) {
                return false;
            }

            return cvl_push_i32(out, index);
        }

        default:
        {
            set_component_exception(
                cx, "invalid primitive for lower_flat_primitive");
            return false;
        }
    }
}

bool
lower_flat_list(LiftLowerContext *cx, WASMComponentTypeInstance *type,
                uint32_t maybe_length, bool is_fixed, wit_value_t value,
                CoreValueList *out)
{
    if (is_fixed) {
        bh_assert(maybe_length == value->value.list_value.size);
        CoreValueList flat;
        for (uint32_t i = 0; i < maybe_length; i++) {
            cvl_init(&flat);
            if (!lower_flat(cx, type, &flat,
                            value->value.list_value.elems[i])) {
                return false;
            }

            cvl_extend(out, &flat);
        }

        return true;
    }

    uint32_t ptr = 0, length = 0;
    if (!store_list_into_range(cx, value->value.list_value.size, type, &ptr,
                               &length, value))
        return false;
    return cvl_push_i32(out, ptr) && cvl_push_i32(out, length);
}

bool
lower_flat_record(LiftLowerContext *cx, WASMComponentRecordInstance *type,
                  CoreValueList *out, wit_value_t value)
{
    CoreValueList flat;
    for (uint32_t i = 0; i < type->count; i++) {
        cvl_init(&flat);
        if (!lower_flat(cx, type->fields[i].type, &flat,
                        value->value.record_value.fields[i].value)) {
            return false;
        }

        cvl_extend(out, &flat);
    }

    return true;
}

bool
lower_flat_variant(LiftLowerContext *cx, WASMComponentVariantInstance *type,
                   CoreValueList *out, wit_value_t value)
{
    uint32_t case_index = 0;
    wit_value_t case_value = NULL;
    if (!match_case(cx, type, value, &case_index, &case_value)) {
        return false;
    }

    FlatTypes variant_flat;
    flat_types_init(&variant_flat);
    if (!flatten_type_variant(cx, type, &variant_flat)) {
        return false;
    }

    bh_assert(variant_flat.types[0] == CORE_TYPE_I32);
    uint32_t variant_flat_index = 1;

    WASMComponentCaseValInstance c = type->cases[case_index];

    CoreValueList payload;
    cvl_init(&payload);

    if (c.value_type != NULL) {
        if (!lower_flat(cx, c.value_type, &payload, case_value)) {
            return false;
        }

        FlatTypes case_flat;
        flat_types_init(&case_flat);
        if (!flatten_type(cx, c.value_type, &case_flat)) {
            return false;
        }

        // enumerate(zip(payload, case_flat))
        for (uint32_t i = 0; i < payload.count; i++) {
            CoreValType have = case_flat.types[i];
            CoreValType want = variant_flat.types[variant_flat_index++];

            if (have == want) {
                // No coercion needed
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I32) {
                payload.values[i].val.i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].type = CORE_TYPE_I32;
            }
            else if (have == CORE_TYPE_I32 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 = (uint64_t)payload.values[i].val.i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I64) {
                uint32_t as_i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].val.i64 = (uint64_t)as_i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F64 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 =
                    encode_float_as_i64(payload.values[i].val.f64);
                payload.values[i].type = CORE_TYPE_I64;
            }
            else {
                // Should not happen
                bh_assert(have == want);
            }
        }
    }

    while (variant_flat_index < variant_flat.count) {
        CoreValType want = variant_flat.types[variant_flat_index++];
        if (want == CORE_TYPE_I32 || want == CORE_TYPE_F32) {
            cvl_push_i32(&payload, 0);
        }
        else {
            cvl_push_i64(&payload, 0);
        }
    }

    cvl_init(out);
    if (!cvl_push_i32(out, case_index)) {
        return false;
    }

    if (!cvl_extend(out, &payload)) {
        return false;
    }

    return true;
}

bool
lower_flat_flags(LiftLowerContext *cx, const WASMComponentFlagType *type,
                 CoreValueList *out, wit_value_t value)
{
    bh_assert(0 < type->count && type->count <= 32);
    uint64_t i = 0;
    if (!pack_flags_into_int(cx, type, &i, value)) {
        return false;
    }

    return cvl_push_i32(out, (uint32_t)i);
}

bool
lower_flat_tuple(LiftLowerContext *cx, WASMComponentTupleInstance *type,
                 CoreValueList *out, wit_value_t value)
{
    // Lower each element
    CoreValueList flat;
    for (uint32_t i = 0; i < type->count; i++) {
        cvl_init(&flat);
        // Access tuple elements from tuple_value
        if (!lower_flat(cx, type->element_types[i], &flat,
                        value->value.tuple_value.elems[i])) {
            return false;
        }
        cvl_extend(out, &flat);
    }
    return true;
}

bool
lower_flat_enum(LiftLowerContext *cx, const WASMComponentEnumType *type,
                CoreValueList *out, wit_value_t value)
{
    // Write discriminant only
    uint32_t case_index = value->value.enum_value.value;
    trap_if(case_index >= type->count);

    return cvl_push_i32(out, case_index);
}

bool
lower_flat_option(LiftLowerContext *cx, WASMComponentOptionInstance *type,
                  CoreValueList *out, wit_value_t value)
{
    bool is_some = (value->value.option_value.optional_elem != NULL);
    uint32_t case_index = is_some ? 1 : 0;

    FlatTypes option_flat;
    flat_types_init(&option_flat);
    if (!flatten_type_option(cx, type, &option_flat)) {
        return false;
    }

    bh_assert(option_flat.types[0] == CORE_TYPE_I32);
    uint32_t option_flat_index = 1;

    CoreValueList payload;
    cvl_init(&payload);

    if (is_some && type->element_type != NULL) {
        if (!lower_flat(cx, type->element_type, &payload,
                        value->value.option_value.optional_elem)) {
            return false;
        }

        FlatTypes case_flat;
        flat_types_init(&case_flat);
        if (!flatten_type(cx, type->element_type, &case_flat)) {
            return false;
        }

        // Coerce payload values
        for (uint32_t i = 0; i < payload.count; i++) {
            CoreValType have = case_flat.types[i];
            CoreValType want = option_flat.types[option_flat_index++];

            if (have == want) {
                // No coercion needed
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I32) {
                payload.values[i].val.i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].type = CORE_TYPE_I32;
            }
            else if (have == CORE_TYPE_I32 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 = (uint64_t)payload.values[i].val.i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I64) {
                uint32_t as_i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].val.i64 = (uint64_t)as_i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F64 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 =
                    encode_float_as_i64(payload.values[i].val.f64);
                payload.values[i].type = CORE_TYPE_I64;
            }
        }
    }

    // Pad remaining slots with zeros
    while (option_flat_index < option_flat.count) {
        CoreValType want = option_flat.types[option_flat_index++];
        if (want == CORE_TYPE_I32 || want == CORE_TYPE_F32) {
            cvl_push_i32(&payload, 0);
        }
        else {
            cvl_push_i64(&payload, 0);
        }
    }

    cvl_init(out);
    if (!cvl_push_i32(out, case_index)) {
        return false;
    }

    return cvl_extend(out, &payload);
}

bool
lower_flat_result(LiftLowerContext *cx, WASMComponentResultInstance *type,
                  CoreValueList *out, wit_value_t value)
{
    bool is_err = value->value.result_value.is_err;
    uint32_t case_index = is_err ? 1 : 0;
    WASMComponentTypeInstance *payload_type =
        is_err ? type->error_type : type->result_type;
    wit_value_t case_value = is_err ? value->value.result_value.result.err
                                    : value->value.result_value.result.ok;

    FlatTypes result_flat;
    flat_types_init(&result_flat);
    if (!flatten_type_result(cx, type, &result_flat)) {
        return false;
    }

    bh_assert(result_flat.types[0] == CORE_TYPE_I32);
    uint32_t result_flat_index = 1;

    CoreValueList payload;
    cvl_init(&payload);

    if (payload_type != NULL && case_value != NULL) {
        if (!lower_flat(cx, payload_type, &payload, case_value)) {
            return false;
        }

        FlatTypes case_flat;
        flat_types_init(&case_flat);
        if (!flatten_type(cx, payload_type, &case_flat)) {
            return false;
        }

        // Coerce payload values
        for (uint32_t i = 0; i < payload.count; i++) {
            CoreValType have = case_flat.types[i];
            CoreValType want = result_flat.types[result_flat_index++];

            if (have == want) {
                // No coercion
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I32) {
                payload.values[i].val.i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].type = CORE_TYPE_I32;
            }
            else if (have == CORE_TYPE_I32 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 = (uint64_t)payload.values[i].val.i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F32 && want == CORE_TYPE_I64) {
                uint32_t as_i32 =
                    encode_float_as_i32(payload.values[i].val.f32);
                payload.values[i].val.i64 = (uint64_t)as_i32;
                payload.values[i].type = CORE_TYPE_I64;
            }
            else if (have == CORE_TYPE_F64 && want == CORE_TYPE_I64) {
                payload.values[i].val.i64 =
                    encode_float_as_i64(payload.values[i].val.f64);
                payload.values[i].type = CORE_TYPE_I64;
            }
        }
    }

    // Pad remaining slots with zeros
    while (result_flat_index < result_flat.count) {
        CoreValType want = result_flat.types[result_flat_index++];
        if (want == CORE_TYPE_I32 || want == CORE_TYPE_F32) {
            cvl_push_i32(&payload, 0);
        }
        else {
            cvl_push_i64(&payload, 0);
        }
    }

    cvl_init(out);
    if (!cvl_push_i32(out, case_index)) {
        return false;
    }

    return cvl_extend(out, &payload);
}

bool
lower_flat(LiftLowerContext *cx, WASMComponentTypeInstance *type,
           CoreValueList *out, wit_value_t value)
{
    switch (type->type) {
        case COMPONENT_VAL_TYPE_PRIMVAL:
        {
            return lower_flat_primitive(cx, type->type_specific.primval, out,
                                        value);
        }

        case COMPONENT_VAL_TYPE_LIST:
        {
            return lower_flat_list(cx, type->type_specific.list->element_type,
                                   0, false, value, out);
        }

        case COMPONENT_VAL_TYPE_FIXED_SIZE_LIST:
        {
            return lower_flat_list(
                cx, type->type_specific.list_len->element_type,
                type->type_specific.list_len->len, true, value, out);
        }

        case COMPONENT_VAL_TYPE_RECORD:
        {
            return lower_flat_record(cx, type->type_specific.record, out,
                                     value);
        }

        case COMPONENT_VAL_TYPE_VARIANT:
        {
            return lower_flat_variant(cx, type->type_specific.variant, out,
                                      value);
        }

        case COMPONENT_VAL_TYPE_FLAGS:
        {
            return lower_flat_flags(cx, type->type_specific.flag, out, value);
        }

        case COMPONENT_VAL_TYPE_TUPLE:
        {
            return lower_flat_tuple(cx, type->type_specific.tuple, out, value);
        }

        case COMPONENT_VAL_TYPE_ENUM:
        {
            return lower_flat_enum(cx, type->type_specific.enum_type, out,
                                   value);
        }

        case COMPONENT_VAL_TYPE_OPTION:
        {
            return lower_flat_option(cx, type->type_specific.option, out,
                                     value);
        }

        case COMPONENT_VAL_TYPE_RESULT:
        {
            return lower_flat_result(cx, type->type_specific.result, out,
                                     value);
        }

        case COMPONENT_VAL_TYPE_OWN:
        {
            uint32_t index = 0;
            if (!lower_own(cx, type->type_specific.resource_handle, value,
                           &index)) {
                return false;
            }
            return cvl_push_i32(out, index);
        }

        case COMPONENT_VAL_TYPE_BORROW:
        {
            uint32_t index = 0;
            if (!lower_borrow(cx, type->type_specific.resource_handle, value,
                              &index)) {
                return false;
            }
            return cvl_push_i32(out, index);
        }

        default:
        {
            break;
        }
    }

    set_component_exception(cx, "invalid type for lower_flat");
    return false;
}

// Lifting and Lowering Values

static WASMComponentTypeInstance *
params_to_tuple_type(WASMComponentParamListInstance *params)
{
    WASMComponentTupleInstance *tuple = wasm_runtime_malloc(
        sizeof(WASMComponentTupleInstance)
        + (params->count * sizeof(WASMComponentTypeInstance *)));
    if (!tuple)
        return NULL;

    tuple->count = params->count;
    tuple->element_types = (WASMComponentTypeInstance **)(tuple + 1);

    for (uint32_t i = 0; i < params->count; i++) {
        tuple->element_types[i] = params->params[i].type;
    }

    WASMComponentTypeInstance *type_inst =
        wasm_runtime_malloc(sizeof(WASMComponentTypeInstance));
    if (!type_inst) {
        wasm_runtime_free(tuple);
        return NULL;
    }

    type_inst->type = COMPONENT_VAL_TYPE_TUPLE;
    type_inst->type_specific.tuple = tuple;
    type_inst->alignment = compute_alignment(type_inst);
    type_inst->elem_size = compute_elem_size(type_inst);
    return type_inst;
}

static WASMComponentTypeInstance *
results_to_tuple_type(WASMComponentResultListInstance *results)
{
    if (results->tag == WASM_COMP_RESULT_LIST_EMPTY || results->count == 0) {
        return NULL;
    }

    WASMComponentTupleInstance *tuple = wasm_runtime_malloc(
        sizeof(WASMComponentTupleInstance)
        + (results->count * sizeof(WASMComponentTypeInstance *)));
    if (!tuple)
        return NULL;

    tuple->count = results->count;
    tuple->element_types = (WASMComponentTypeInstance **)(tuple + 1);

    for (uint32_t i = 0; i < results->count; i++) {
        tuple->element_types[i] = &results->result[i];
    }

    WASMComponentTypeInstance *type_inst =
        wasm_runtime_malloc(sizeof(WASMComponentTypeInstance));
    if (!type_inst) {
        wasm_runtime_free(tuple);
        return NULL;
    }

    type_inst->type = COMPONENT_VAL_TYPE_TUPLE;
    type_inst->type_specific.tuple = tuple;
    return type_inst;
}

static void
free_temp_tuple_type(WASMComponentTypeInstance *type_inst)
{
    if (type_inst) {
        wasm_runtime_free(type_inst->type_specific.tuple);
        wasm_runtime_free(type_inst);
    }
}

static bool
lift_flat_values_params(LiftLowerContext *cx, uint32_t max_flat,
                        CoreValueIter *vi,
                        WASMComponentParamListInstance *params,
                        wit_value_t *out)
{
    if (params->count == 0) {
        *out = NULL;
        return true;
    }

    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_param_types(cx, params, &flat_types)) {
        set_component_exception(
            cx, "Could not flatten param types in lift_flat_values");
        return false;
    }

    wit_value_t *values =
        (wit_value_t *)wasm_runtime_malloc(params->count * sizeof(wit_value_t));
    if (!values) {
        set_component_exception(cx, "allocation failed");
        return false;
    }

    if (flat_types.count > max_flat) {
        uint32_t ptr = vi_next_i32(vi);
        const WASMMemoryInstance *mem = get_mem_from_cx(cx);

        WASMComponentTypeInstance *tuple_type = params_to_tuple_type(params);
        if (!tuple_type) {
            wasm_runtime_free((void *)values);
            return false;
        }

        if ((ptr != align_to(ptr, tuple_type->alignment))
            || ((uint64_t)ptr + (uint64_t)tuple_type->elem_size
                > mem->memory_data_size)) {
            set_component_exception(
                cx, "lift params pointer out of bounds or misaligned");
            free_temp_tuple_type(tuple_type);
            wasm_runtime_free((void *)values);
            return false;
        }

        wit_value_t loaded = NULL;
        if (!load(cx, ptr, tuple_type, &loaded)) {
            free_temp_tuple_type(tuple_type);
            wasm_runtime_free((void *)values);
            return false;
        }

        for (uint32_t i = 0; i < params->count; i++) {
            values[i] = loaded->value.tuple_value.elems[i];
        }
        wasm_runtime_free((void *)loaded->value.tuple_value.elems);
        wasm_runtime_free(loaded);
        free_temp_tuple_type(tuple_type);
    }
    else {
        for (uint32_t i = 0; i < params->count; i++) {
            if (!lift_flat(cx, vi, params->params[i].type, &values[i])) {
                for (uint32_t j = 0; j < i; j++)
                    free_wit_value(values[j]);
                wasm_runtime_free((void *)values);
                return false;
            }
        }
    }

    *out = wit_values_list_ctor(values, params->count);
    if (!*out) {
        for (uint32_t i = 0; i < params->count; i++)
            free_wit_value(values[i]);
        wasm_runtime_free((void *)values);
        return false;
    }
    return true;
}

static bool
lift_flat_values_results(LiftLowerContext *cx, uint32_t max_flat,
                         CoreValueIter *vi,
                         WASMComponentResultListInstance *results,
                         wit_value_t *out)
{
    if (results->tag == WASM_COMP_RESULT_LIST_EMPTY || results->count == 0) {
        *out = NULL;
        return true;
    }

    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_result_types(cx, results, &flat_types)) {
        set_component_exception(
            cx, "Could not flatten param types in lift_flat_values");
        return false;
    }

    wit_value_t *values = (wit_value_t *)wasm_runtime_malloc(
        results->count * sizeof(wit_value_t));
    if (!values) {
        set_component_exception(cx, "allocation failed");
        return false;
    }

    if (flat_types.count > max_flat) {
        uint32_t ptr = vi_next_i32(vi);
        const WASMMemoryInstance *mem = get_mem_from_cx(cx);

        WASMComponentTypeInstance *tuple_type = results_to_tuple_type(results);
        if (!tuple_type) {
            wasm_runtime_free((void *)values);
            return false;
        }

        tuple_type->alignment = compute_alignment(tuple_type);
        tuple_type->elem_size = compute_elem_size(tuple_type);

        if ((ptr != align_to(ptr, tuple_type->alignment))
            || (((uint64_t)ptr + (uint64_t)tuple_type->elem_size)
                > mem->memory_data_size)) {
            set_component_exception(
                cx, "lift results pointer out of bounds or misaligned");
            free_temp_tuple_type(tuple_type);
            wasm_runtime_free((void *)values);
            return false;
        }

        wit_value_t loaded = NULL;
        if (!load(cx, ptr, tuple_type, &loaded)) {
            free_temp_tuple_type(tuple_type);
            wasm_runtime_free((void *)values);
            return false;
        }

        for (uint32_t i = 0; i < results->count; i++) {
            values[i] = loaded->value.tuple_value.elems[i];
        }
        wasm_runtime_free((void *)loaded->value.tuple_value.elems);
        wasm_runtime_free(loaded);
        free_temp_tuple_type(tuple_type);
    }
    else {
        for (uint32_t i = 0; i < results->count; i++) {
            if (!lift_flat(cx, vi, &results->result[i], &values[i])) {
                for (uint32_t j = 0; j < i; j++)
                    free_wit_value(values[j]);
                wasm_runtime_free((void *)values);
                return false;
            }
        }
    }

    *out = wit_values_list_ctor(values, results->count);
    if (!*out) {
        for (uint32_t i = 0; i < results->count; i++)
            free_wit_value(values[i]);
        wasm_runtime_free((void *)values);
        return false;
    }
    return true;
}

bool
lift_flat_values(LiftLowerContext *cx, uint32_t max_flat, CoreValueIter *vi,
                 WASMComponentParamListInstance *params,
                 WASMComponentResultListInstance *results, wit_value_t *out)
{
    if (params && !results) {
        return lift_flat_values_params(cx, max_flat, vi, params, out);
    }

    if (!params && results) {
        return lift_flat_values_results(cx, max_flat, vi, results, out);
    }

    return false;
}

bool
lower_flat_values_params(LiftLowerContext *cx, uint32_t max_flat,
                         wit_value_t values,
                         WASMComponentParamListInstance *params,
                         CoreValueIter *out_param, CoreValueList *out)
{
    if (params->count == 0) {
        cvl_init(out);
        return true;
    }

    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_param_types(cx, params, &flat_types)) {
        set_component_exception(
            cx, "Could not flatten param types in lift_flat_values");
        return false;
    }

    if (flat_types.count > max_flat) {
        if (values->type != COMPONENT_VAL_TYPE_LIST) {
            set_component_exception(cx, "Expected list value");
            return false;
        }

        WASMComponentTypeInstance *tuple_type = params_to_tuple_type(params);
        if (!tuple_type) {
            set_component_exception(cx, "Could not create tuple type");
            return false;
        }

        wit_value_t tuple_value = wit_tuple_ctor(values->value.list_value.elems,
                                                 values->value.list_value.size);
        if (!tuple_value) {
            set_component_exception(cx, "Could not create tuple value");
            free_temp_tuple_type(tuple_type);
            return false;
        }

        bool params_ok = false;
        uint32_t ptr = 0;
        cvl_init(out);
        if (out_param == NULL) {
            ptr = wasm_runtime_call_realloc(cx, 0, 0,
                                            (int32_t)tuple_type->alignment,
                                            (int32_t)tuple_type->elem_size);
            if (ptr == 0) {
                goto done_lower_params;
            }

            cvl_push_i32(out, ptr);
        }
        else {
            ptr = vi_next_i32(out_param);
        }

        const WASMMemoryInstance *mem = get_mem_from_cx(cx);

        if ((ptr != align_to(ptr, tuple_type->alignment))
            || ((uint64_t)ptr + (uint64_t)tuple_type->elem_size
                > mem->memory_data_size)) {
            set_component_exception(
                cx, "store pointer out of bounds or misaligned");
            goto done_lower_params;
        }

        params_ok = store(cx, ptr, tuple_type, tuple_value);

    done_lower_params:
        // Don't free elems - they are borrowed from the input values list
        wasm_runtime_free(tuple_value);
        free_temp_tuple_type(tuple_type);
        return params_ok;
    }
    else {
        cvl_init(out);
        for (uint32_t i = 0; i < params->count; i++) {
            CoreValueList flat;
            cvl_init(&flat);
            if (!lower_flat(cx, params->params[i].type, &flat,
                            values->value.list_value.elems[i])) {
                return false;
            }
            cvl_extend(out, &flat);
        }
        return true;
    }
}

bool
lower_flat_values_results(LiftLowerContext *cx, uint32_t max_flat,
                          wit_value_t values,
                          WASMComponentResultListInstance *results,
                          CoreValueIter *out_param, CoreValueList *out)
{
    if (results->tag == WASM_COMP_RESULT_LIST_EMPTY || results->count == 0) {
        cvl_init(out);
        return true;
    }

    FlatTypes flat_types;
    flat_types_init(&flat_types);

    if (!flatten_result_types(cx, results, &flat_types)) {
        set_component_exception(
            cx, "Could not flatten param types in lift_flat_values");
        return false;
    }

    if (flat_types.count > max_flat) {
        if (values->type != COMPONENT_VAL_TYPE_LIST) {
            set_component_exception(cx, "Expected list value");
            return false;
        }

        WASMComponentTypeInstance *tuple_type = results_to_tuple_type(results);
        if (!tuple_type) {
            set_component_exception(cx, "Could not create tuple type");
            return false;
        }

        tuple_type->alignment = compute_alignment(tuple_type);
        tuple_type->elem_size = compute_elem_size(tuple_type);

        wit_value_t tuple_value = wit_tuple_ctor(values->value.list_value.elems,
                                                 values->value.list_value.size);
        if (!tuple_value) {
            set_component_exception(cx, "Could not create tuple value");
            free_temp_tuple_type(tuple_type);
            return false;
        }

        bool results_ok = false;
        uint32_t ptr = 0;
        cvl_init(out);
        if (out_param == NULL) {
            ptr = wasm_runtime_call_realloc(cx, 0, 0,
                                            (int32_t)tuple_type->alignment,
                                            (int32_t)tuple_type->elem_size);
            if (ptr == 0) {
                goto done_lower_results;
            }
            cvl_push_i32(out, ptr);
        }
        else {
            ptr = vi_next_i32(out_param);
        }

        const WASMMemoryInstance *mem = get_mem_from_cx(cx);

        if ((ptr != align_to(ptr, tuple_type->alignment)
             || (uint64_t)ptr + (uint64_t)tuple_type->elem_size
                    > mem->memory_data_size)) {
            set_component_exception(
                cx, "store pointer out of bounds or misaligned");
            goto done_lower_results;
        }

        results_ok = store(cx, ptr, tuple_type, tuple_value);

    done_lower_results:
        // Don't free elems - they are borrowed from the input values list
        wasm_runtime_free(tuple_value);
        free_temp_tuple_type(tuple_type);
        return results_ok;
    }
    else {
        cvl_init(out);
        for (uint32_t i = 0; i < results->count; i++) {
            CoreValueList flat;
            cvl_init(&flat);
            if (!lower_flat(cx, &results->result[i], &flat,
                            values->value.list_value.elems[i])) {
                return false;
            }
            cvl_extend(out, &flat);
        }
        return true;
    }
}

bool
lower_flat_values(LiftLowerContext *cx, uint32_t max_flat, wit_value_t values,
                  WASMComponentParamListInstance *params,
                  WASMComponentResultListInstance *results,
                  CoreValueIter *out_param, CoreValueList *out)
{
    cx->inst->may_leave = false;
    bool ok = false;
    if (params && !results) {
        ok = lower_flat_values_params(cx, max_flat, values, params, out_param,
                                      out);
    }

    if (!params && results) {
        ok = lower_flat_values_results(cx, max_flat, values, results, out_param,
                                       out);
    }

    cx->inst->may_leave = true;

    return ok;
}

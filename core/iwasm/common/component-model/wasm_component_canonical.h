/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_CANONICAL_H
#define WASM_COMPONENT_CANONICAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wasm_runtime.h"
#include "wasm_canonical_abi.h"

#define trap_if(condition)                           \
    do {                                             \
        if (condition) {                             \
            set_component_exception(cx, #condition); \
            return false;                            \
        }                                            \
    } while (0)

typedef struct WASMMemoryInstance WASMMemoryInstance;
typedef struct WASMComponentInstance WASMComponentInstance;
typedef struct WASMComponentTypeInstance WASMComponentTypeInstance;
typedef struct Supertask Supertask;
typedef struct Task Task;
typedef struct Subtask Subtask;

typedef enum BorrowScopeType {
    BORROW_SCOPE_NONE = 0, // No borrow types
    BORROW_SCOPE_TASK,     // For lowering borrows (in canon_lift)
    BORROW_SCOPE_SUBTASK,  // For lifting borrows (in canon_lower)
} BorrowScopeType;

typedef struct LiftOptions {
    StringEncoding string_encoding; // "utf8", "utf16", "latin1+utf16"
    WASMMemoryInstance *memory;     // Linear memory to read from
} LiftOptions;

typedef struct LiftLowerOptions {
    LiftOptions *lift_opts;
    WASMFunctionInstance *realloc_func; // Lowering requires allocation
} LiftLowerOptions;

typedef struct CanonicalOptions {
    LiftLowerOptions *lift_lower_opts;
    WASMFunctionInstance
        *post_return_func; // Optional cleanup function (canon_lift only)
    bool async;            // true = async mode, false = sync mode
    WASMFunctionInstance
        *callback_func; // For async callback mode (canon_lift only)
} CanonicalOptions;

typedef struct LiftLowerContext {
    CanonicalOptions *canonical_opts; // For canon_lift/canon_lower
    WASMComponentInstance *inst;
    BorrowScopeType borrow_scope_type; // For resource tracking
    union {
        Task *task;       // Used by lower_borrow
        Subtask *subtask; // Used by lift_borrow
    } borrow_scope;
} LiftLowerContext;

/**
 * @brief Sets a pending exception message in the component instance.
 * @param cx The lifting/lowering context.
 * @param msg The error message string.
 */
void
set_component_exception(LiftLowerContext *cx, const char *msg);

// Layer 1
/**
 * @brief Recursively lifts a WIT value from WebAssembly linear memory (canon
 * lift).
 * @param cx The lifting context.
 * @param ptr Absolute offset in linear memory.
 * @param type The metadata of the type to load.
 * @param out Pointer to receive the high-level WIT value.
 * @return bool True if the value was loaded successfully, false otherwise.
 */
bool
load(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
     wit_value_t *out);

/**
 * @brief Recursively lowers a high-level WIT value into linear memory (canon
 * lower).
 * @param cx The lowering context.
 * @param ptr Absolute offset in linear memory.
 * @param type The metadata of the type to store.
 * @param value The WIT value to be stored.
 * @return bool True if the value was stored successfully, false otherwise.
 */
bool
store(LiftLowerContext *cx, uint32_t ptr, WASMComponentTypeInstance *type,
      wit_value_t value);

/**
 * @brief Lifts a string from a specified memory range.
 * @param cx The lifting context.
 * @param ptr Address of the string bytes in memory.
 * @param tagged_code_units The unit count (bytes or code units).
 * @param out Pointer to receive the constructed WIT string.
 * @return bool True if the string was loaded successfully.
 */
bool
load_string_from_range(LiftLowerContext *cx, uint32_t ptr,
                       uint32_t tagged_code_units, wit_value_t *out);

/**
 * @brief Lifts a list from a specified memory range.
 * @param cx The lifting context.
 * @param ptr Address of the list elements.
 * @param length The number of elements in the list.
 * @param type The type metadata for the list elements.
 * @param out Pointer to receive the constructed WIT list.
 * @return bool True if the list was loaded successfully.
 */
bool
load_list_from_range(LiftLowerContext *cx, uint32_t ptr, uint32_t length,
                     WASMComponentTypeInstance *type, wit_value_t *out);

/**
 * @brief Lowers a string into a memory range, potentially using realloc for
 * allocation.
 * @param cx The lowering context.
 * @param str The high-level WIT string to store.
 * @param out_ptr Pointer to receive the resulting address in memory.
 * @param out_code_units Pointer to receive the resulting unit count.
 * @return bool True if storage succeeded.
 */
bool
store_string_into_range(LiftLowerContext *cx, wit_value_t str,
                        uint32_t *out_ptr, uint32_t *out_code_units);

/**
 * @brief Lowers a list into a memory range, allocating memory if necessary.
 * @param cx The lowering context.
 * @param length The number of elements to store.
 * @param type The type metadata for the elements.
 * @param begin_out Pointer to receive the start address of the stored list.
 * @param length_out Pointer to receive the stored element count.
 * @param value The high-level WIT list to store.
 * @return bool True if storage succeeded.
 */
bool
store_list_into_range(LiftLowerContext *cx, uint32_t length,
                      WASMComponentTypeInstance *type, uint32_t *begin_out,
                      uint32_t *length_out, wit_value_t value);

/**
 * @brief Validates and converts an i32 value to a Unicode character code.
 * @param cx The context for error reporting.
 * @param i The input integer.
 * @param out Pointer to receive the validated character code.
 * @return bool True if the integer is a valid Unicode scalar value.
 */
bool
convert_i32_to_char(LiftLowerContext *cx, uint32_t i, uint32_t *out);

/**
 * @brief Lifts an error context from the component's internal table.
 * @param cx The lifting context.
 * @param errctx_val The index in the error context table.
 * @param out Pointer to receive the lifted WIT error context value.
 * @return bool True if the context was lifted successfully.
 */
bool
lift_error_context(LiftLowerContext *cx, uint32_t errctx_val, wit_value_t *out);

/**
 * @brief Transfers ownership of a resource from the table to a WIT value.
 * @param cx The lifting context.
 * @param index The handle index in the table.
 * @param type Metadata for the resource handle.
 * @param out Pointer to receive the lifted resource representation.
 * @return bool True if the context was lifted successfully.
 */
bool
lift_own(LiftLowerContext *cx, uint32_t index,
         WASMComponentResourceHandleInstance *type, wit_value_t *out);

/**
 * @brief Extracts the representation from a resource handle without
 * transferring ownership.
 * @param cx The lifting context.
 * @param index The handle index in the table.
 * @param type Metadata for the resource handle.
 * @param out Pointer to receive the lifted resource representation.
 * @return uint32_t True if the context was lifted successfully.
 */
bool
lift_borrow(LiftLowerContext *cx, uint32_t index,
            const WASMComponentResourceHandleInstance *type, wit_value_t *out);

/**
 * @brief Lowers an owned resource value into the component's handle table.
 * @param cx The lowering context.
 * @param type Metadata for the resource handle.
 * @param value The WIT resource value to lower.
 * @return uint32_t True if the context was lowered successfully.
 */
bool
lower_own(LiftLowerContext *cx, WASMComponentResourceHandleInstance *type,
          wit_value_t value, uint32_t *out_index);

/**
 * @brief Lowers a borrowed resource value into the component's handle table.
 * @param cx The lowering context.
 * @param type Metadata for the resource handle.
 * @param value The WIT resource value to lower.
 * @return uint32_t True if the context was lowered successfully.
 */
bool
lower_borrow(LiftLowerContext *cx, WASMComponentResourceHandleInstance *type,
             wit_value_t value, uint32_t *out_index);

/**
 * @brief Bit-reinterpret an i32 as an f32.
 * @param i The 32-bit integer.
 * @return float The resulting float.
 */
float
core_f32_reinterpret_i32(uint32_t i);

/**
 * @brief Standardizes 32-bit float NaN bit patterns for ABI security.
 * @param f The input float.
 * @return float The canonicalized NaN or original value.
 */
float
canonicalize_nan32(float f);

/**
 * @brief Bit-reinterpret an i64 as an f64.
 * @param i The 64-bit integer.
 * @return double The resulting double.
 */
double
core_f64_reinterpret_i64(uint64_t i);

/**
 * @brief Standardizes 64-bit float NaN bit patterns for ABI security.
 * @param d The input double.
 * @return double The canonicalized NaN or original value.
 */
double
canonicalize_nan64(double d);

float
maybe_scramble_nan32(float f);
double
maybe_scramble_nan64(double d);
bool
char_to_i32(LiftLowerContext *cx, uint32_t c, uint32_t *out);
bool
lower_error_context(LiftLowerContext *cx, wit_value_t value,
                    uint32_t *out_index);
bool
pack_flags_into_int(LiftLowerContext *cx, const WASMComponentFlagType *labels,
                    uint64_t *out, wit_value_t value);
bool
match_case(LiftLowerContext *cx, const WASMComponentVariantInstance *type,
           wit_value_t value, uint32_t *case_index, wit_value_t *case_value);
uint32_t
encode_float_as_i32(float f);
uint64_t
encode_float_as_i64(double d);

/**
 * @brief Converts an integer bitmask into high-level WIT flag fields.
 * @param cx The lifting context.
 * @param i The source integer bitmask.
 * @param labels Metadata containing flag label names.
 * @param out Pointer to receive the WIT flags value.
 * @return bool True if unpacking succeeded.
 */
bool
unpack_flags_from_int(LiftLowerContext *cx, uint64_t i,
                      WASMComponentFlagType *labels, wit_value_t *out);

// Helpers

/**
 * @brief Retrieves the lifting-specific configuration options from the context.
 * @param cx Pointer to the lift/lower context.
 * @return LiftOptions* Pointer to the lifting options structure.
 */
static inline LiftOptions *
get_lift_opts(LiftLowerContext *cx)
{
    return cx->canonical_opts->lift_lower_opts->lift_opts;
}

/**
 * @brief Retrieves the combined lifting and lowering configuration options from
 * the context.
 * @param cx Pointer to the lift/lower context.
 * @return LiftLowerOptions* Pointer to the lift/lower options structure.
 */
static inline LiftLowerOptions *
get_liftlower_opts(LiftLowerContext *cx)
{
    return cx->canonical_opts->lift_lower_opts;
}

/**
 * @brief Retrieves the primary canonical options associated with the context.
 * @param cx Pointer to the lift/lower context.
 * @return CanonicalOptions* Pointer to the canonical options structure.
 */
static inline CanonicalOptions *
get_canonical_opts(const LiftLowerContext *cx)
{
    return cx->canonical_opts;
}

/**
 * @brief Accesses the WebAssembly linear memory instance associated with the
 * context.
 * @param cx Pointer to the lift/lower context.
 * @return WASMMemoryInstance* Pointer to the memory instance used for
 * lifting/lowering.
 */
static inline WASMMemoryInstance *
get_mem_from_cx(const LiftLowerContext *cx)
{
    return cx->canonical_opts->lift_lower_opts->lift_opts->memory;
}

/**
 * @brief Retrieves the string encoding format required by the current
 * lifting/lowering operation.
 * @param cx Pointer to the lift/lower context.
 * @return StringEncoding The enum value representing the active encoding
 * (UTF-8, UTF-16, etc.).
 */
static inline StringEncoding
get_string_encoding(const LiftLowerContext *cx)
{
    return cx->canonical_opts->lift_lower_opts->lift_opts->string_encoding;
}

/**
 * @brief Retrieves the WebAssembly function instance used for memory
 * reallocation during lowering.
 * @param cx Pointer to the lift/lower context.
 * @return WASMFunctionInstance* Pointer to the function instance representing
 * 'cabi_realloc'.
 */
static inline WASMFunctionInstance *
get_realloc_func(const LiftLowerContext *cx)
{
    return cx->canonical_opts->lift_lower_opts->realloc_func;
}

bool
encode_string(LiftLowerContext *cx, const char *utf8_str, uint32_t utf8_len,
              StringEncoding encoding, uint8_t **out_bytes,
              uint32_t *out_byte_len, uint32_t *out_code_units);

/**
 * @brief Initializes runtime canonical options from parsed component metadata.
 * @param parsed_opts The raw options parsed from the Wasm component.
 * @param error_buf Buffer for error messages.
 * @param error_buf_size Size of the error buffer.
 * @return CanonicalOptions* Pointer to initialized runtime options.
 */
CanonicalOptions *
convert_canon_opts_to_runtime(WASMComponentCanonOptsInstance *parsed_opts,
                              char *error_buf, uint32 error_buf_size);

/**
 * @brief Deeply frees a CanonicalOptions structure and its children.
 * @param opts The options structure to free.
 */
void
free_canonical_options(CanonicalOptions *opts);

#ifdef __cplusplus
}
#endif

#endif

/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_H
#define WASM_COMPONENT_H

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "wasm_export.h"
#include "../interpreter/wasm.h"
#include <stdint.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// Forward Declarations for Reusing Existing WASM Structures
// -----------------------------------------------------------------------------
struct WASMFuncType;        // From wasm.h - for core function types
struct WASMRefTypeMap;      // From wasm.h - for reference type mapping
struct WASMStructType;      // From wasm.h - for struct types
struct WASMArrayType;       // From wasm.h - for array types
struct WASMStructFieldType; // From wasm.h - for struct field types

// -----------------------------------------------------------------------------
// Forward Declarations for Component-Specific Structures
// -----------------------------------------------------------------------------
struct WASMComponentCoreName;
struct WASMComponentSort;
struct WASMComponentSortIdx;
struct WASMComponentValueType;
struct WASMComponentValueBound;
struct WASMComponentTypeBound;
struct WASMComponentExternDesc;
struct WASMComponentImportName;
struct WASMComponentExportName;
struct WASMComponentImportExportName;
struct WASMComponentAliasDefinition;
struct WASMComponentLabelValType;
struct WASMComponentCaseValType;
struct WASMComponentDefValType;
struct WASMComponentFuncType;
struct WASMComponentResourceType;
struct WASMComponentComponentType;
struct WASMComponentInstType;
struct WASMComponentTypes;
struct WASMComponentSection;
struct WASMComponent;
struct WASMComponentInstArg;
struct WASMComponentInlineExport;

// -----------------------------------------------------------------------------
// Constants and Macros
// -----------------------------------------------------------------------------
#define INVALID_VALUE (uint8_t)0xFF
#define MAX_DEPTH_RECURSION 100

// -----------------------------------------------------------------------------
// Enums
// -----------------------------------------------------------------------------
// Core Sort Values
// -----------------------------------------------------------------------------
typedef enum WASMComponentCoreSort {
    WASM_COMP_CORE_SORT_FUNC = 0x00,    // func
    WASM_COMP_CORE_SORT_TABLE = 0x01,   // table
    WASM_COMP_CORE_SORT_MEMORY = 0x02,  // memory
    WASM_COMP_CORE_SORT_GLOBAL = 0x03,  // global
    WASM_COMP_CORE_SORT_TYPE = 0x10,    // type
    WASM_COMP_CORE_SORT_MODULE = 0x11,  // module
    WASM_COMP_CORE_SORT_INSTANCE = 0x12 // instance
} WASMComponentCoreSort;

// Component Sort Values
// -----------------------------------------------------------------------------
typedef enum WASMComponentSortValues {
    WASM_COMP_SORT_CORE_SORT = 0x00, // core func
    WASM_COMP_SORT_FUNC = 0x01,      // func
    WASM_COMP_SORT_VALUE = 0x02,     // value
    WASM_COMP_SORT_TYPE = 0x03,      // type
    WASM_COMP_SORT_COMPONENT = 0x04, // component
    WASM_COMP_SORT_INSTANCE = 0x05   // instance
} WASMComponentSortValues;

// Component Model Primitive Value Types - Those are different from Core
// WebAssembly
// -----------------------------------------------------------------------------
typedef enum WASMComponentPrimValType {
    WASM_COMP_PRIMVAL_BOOL = 0x7f,         // 0x7f => bool
    WASM_COMP_PRIMVAL_S8 = 0x7e,           // 0x7e => s8
    WASM_COMP_PRIMVAL_U8 = 0x7d,           // 0x7d => u8
    WASM_COMP_PRIMVAL_S16 = 0x7c,          // 0x7c => s16
    WASM_COMP_PRIMVAL_U16 = 0x7b,          // 0x7b => u16
    WASM_COMP_PRIMVAL_S32 = 0x7a,          // 0x7a => s32
    WASM_COMP_PRIMVAL_U32 = 0x79,          // 0x79 => u32
    WASM_COMP_PRIMVAL_S64 = 0x78,          // 0x78 => s64
    WASM_COMP_PRIMVAL_U64 = 0x77,          // 0x77 => u64
    WASM_COMP_PRIMVAL_F32 = 0x76,          // 0x76 => f32
    WASM_COMP_PRIMVAL_F64 = 0x75,          // 0x75 => f64
    WASM_COMP_PRIMVAL_CHAR = 0x74,         // 0x74 => char
    WASM_COMP_PRIMVAL_STRING = 0x73,       // 0x73 => string
    WASM_COMP_PRIMVAL_ERROR_CONTEXT = 0x64 // 0x64 => error-context
} WASMComponentPrimValType;

// -----------------------------------------------------------------------------
// Section IDs for WASM Component Model
// -----------------------------------------------------------------------------
typedef enum WASMComponentSectionType {
    WASM_COMP_SECTION_CORE_CUSTOM = 0x00,   // section_0(<core:custom>)
    WASM_COMP_SECTION_CORE_MODULE = 0x01,   // section_1(<core:module>)
    WASM_COMP_SECTION_CORE_INSTANCE = 0x02, // section_2(vec(<core:instance>))
    WASM_COMP_SECTION_CORE_TYPE = 0x03,     // section_3(vec(<core:type>))
    WASM_COMP_SECTION_COMPONENT = 0x04,     // section_4(<component>)
    WASM_COMP_SECTION_INSTANCES = 0x05,     // section_5(vec(<instance>))
    WASM_COMP_SECTION_ALIASES = 0x06,       // section_6(vec(<alias>))
    WASM_COMP_SECTION_TYPE = 0x07,          // section_7(vec(<type>))
    WASM_COMP_SECTION_CANONS = 0x08,        // section_8(vec(<canon>))
    WASM_COMP_SECTION_START = 0x09,         // section_9(<start>)
    WASM_COMP_SECTION_IMPORTS = 0x0A,       // section_10(vec(<import>))
    WASM_COMP_SECTION_EXPORTS = 0x0B,       // section_11(vec(<export>))
    WASM_COMP_SECTION_VALUES = 0x0C         // section_12(vec(<value>))
} WASMComponentSectionType;

// -----------------------------------------------------------------------------
// Import/Export Name Tag - Distinguishes simple vs versioned names
// importname' ::= 0x00 len:<u32> in:<importname> => in
//              |  0x01 len:<u32> in:<importname> vs:<versionsuffix'> => in vs
// -----------------------------------------------------------------------------
typedef enum WASMComponentImportExportTypeTag {
    WASM_COMP_IMPORTNAME_SIMPLE = 0x00,   // 0x00 (simple name)
    WASM_COMP_IMPORTNAME_VERSIONED = 0x01 // 0x01 (versioned name)
} WASMComponentImportExportTypeTag;

// -----------------------------------------------------------------------------
// WASMComponentValueTypeTag Tag - Distinguishes primitive value vs type index
// valtype ::= i:<typeidx> => i
//          | pvt:<primvaltype> => pvt
// -----------------------------------------------------------------------------
typedef enum WASMComponentValueTypeTag {
    WASM_COMP_VAL_TYPE_IDX = 0x00,    // 0x00 (type index)
    WASM_COMP_VAL_TYPE_PRIMVAL = 0x01 // 0x01 (primitive value)
} WASMComponentValueTypeTag;

// -----------------------------------------------------------------------------
// External Descriptor Type - Identifies the kind of import/export
// externdesc ::= 0x00 0x11 i:<core:typeidx> => (core module (type i))
//             | 0x01 i:<typeidx> => (func (type i))
//             | 0x02 b:<valuebound> => (value b)
//             | 0x03 b:<typebound> => (type b)
//             | 0x04 i:<typeidx> => (component (type i))
//             | 0x05 i:<typeidx> => (instance (type i))
// -----------------------------------------------------------------------------
typedef enum WASMComponentExternDescType {
    WASM_COMP_EXTERN_CORE_MODULE = 0x00, // 0x00 0x11 (core module)
    WASM_COMP_EXTERN_FUNC = 0x01,        // 0x01 (func)
    WASM_COMP_EXTERN_VALUE = 0x02,       // 0x02 (value)
    WASM_COMP_EXTERN_TYPE = 0x03,        // 0x03 (type)
    WASM_COMP_EXTERN_COMPONENT = 0x04,   // 0x04 (component)
    WASM_COMP_EXTERN_INSTANCE = 0x05     // 0x05 (instance)
} WASMComponentExternDescType;

// -----------------------------------------------------------------------------
// Value Bound Tag - Distinguishes equality vs type bounds
// valuebound ::= 0x00 v:<valueidx> => (eq v)
//             |  0x01 t:<valtype>  => (eq t)
// -----------------------------------------------------------------------------
typedef enum WASMComponentValueBoundTag {
    WASM_COMP_VALUEBOUND_EQ = 0x00,  // 0x00 (equality bound)
    WASM_COMP_VALUEBOUND_TYPE = 0x01 // 0x01 (type bound)
} WASMComponentValueBoundTag;

// -----------------------------------------------------------------------------
// Type Bound Tag - Distinguishes equality vs subtype bounds
// typebound ::= 0x00 t:<typeidx> => (eq t)
//            |  0x01 t:<typeidx> => (sub t)
// -----------------------------------------------------------------------------
typedef enum WASMComponentTypeBoundTag {
    WASM_COMP_TYPEBOUND_EQ = 0x00,  // 0x00 (equality bound)
    WASM_COMP_TYPEBOUND_TYPE = 0x01 // 0x01 (subtype bound)
} WASMComponentTypeBoundTag;

// -----------------------------------------------------------------------------
// Alias Target Type - Identifies the kind of alias target
// aliastarget ::= 0x00 i:<instanceidx> n:<name> => export i n
//              | 0x01 i:<core:instanceidx> n:<core:name> => core export i n
//              | 0x02 ct:<u32> idx:<u32> => outer ct idx
// -----------------------------------------------------------------------------
typedef enum WASMComponentAliasTargetType {
    WASM_COMP_ALIAS_TARGET_EXPORT = 0x00,      // 0x00 (export alias)
    WASM_COMP_ALIAS_TARGET_CORE_EXPORT = 0x01, // 0x01 (core export alias)
    WASM_COMP_ALIAS_TARGET_OUTER = 0x02        // 0x02 (outer alias)
} WASMComponentAliasTargetType;

// -----------------------------------------------------------------------------
// Optional Field Tag
// <T>? ::= 0x00 =>
//      | 0x01 t:<T> => t
// -----------------------------------------------------------------------------
typedef enum WASMComponentOptionalField {
    WASM_COMP_OPTIONAL_FALSE = 0x00, // 0x00 (absent)
    WASM_COMP_OPTIONAL_TRUE = 0x01,  // 0x01 (present)
} WASMComponentOptionalField;

// -----------------------------------------------------------------------------
// Component Types Tag - Identifies the kind of type definition
// type ::= 0x63 pvt:<primvaltype> => pvt
//       | 0x72 lt*:vec(<labelvaltype>) => (record lt*)
//       | 0x71 case*:vec(<case>) => (variant case*)
//       | 0x70 t:<valtype> => (list t)
//       | 0x67 t:<valtype> len:<u32> => (list t len)
//       | 0x6f t*:vec(<valtype>) => (tuple t*)
//       | 0x6e l*:vec(<label'>) => (flags l*)
//       | 0x6d l*:vec(<label'>) => (enum l*)
//       | 0x6b t:<valtype> => (option t)
//       | 0x6a t?:<valtype>? u?:<valtype>? => (result t? u?)
//       | 0x69 i:<typeidx> => (own i)
//       | 0x68 i:<typeidx> => (borrow i)
//       | 0x66 t?:<valtype>? => (stream t?)
//       | 0x65 t?:<valtype>? => (future t?)
//       | 0x40 pt*:vec(<paramtype>) rt*:vec(<resulttype>) => (func pt* rt*)
//       | 0x41 ct*:vec(<componenttype>) => (component ct*)
//       | 0x42 it*:vec(<instancetype>) => (instance it*)
//       | 0x3f i:<typeidx> => (resource (rep i32) (dtor i))
//       | 0x3e i:<typeidx> => (resource (rep i32) (dtor i) (dtor i))
// -----------------------------------------------------------------------------
typedef enum WASMComponentTypesTag {
    WASM_COMP_DEF_TYPE = 0x00,            // defvaltype (internal tag)
    WASM_COMP_FUNC_TYPE = 0x40,           // functype
    WASM_COMP_COMPONENT_TYPE = 0x41,      // componenttype
    WASM_COMP_INSTANCE_TYPE = 0x42,       // instancetype
    WASM_COMP_RESOURCE_TYPE_SYNC = 0x3f,  // resourcetype (sync)
    WASM_COMP_RESOURCE_TYPE_ASYNC = 0x3e, // resourcetype (async)
    WASM_COMP_INVALID_TYPE = 0xFF         // invalid type
} WASMComponentTypesTag;

// -----------------------------------------------------------------------------
// Instance Expression Tag - Distinguishes with/without args
// instanceexpr ::= 0x00 c:<componentidx> arg*:vec(<instantiatearg>) =>
// (instantiate c arg*)
//               | 0x01 e*:vec(<inlineexport>) => e*
// -----------------------------------------------------------------------------
typedef enum WASMComponentInstExpressionTag {
    WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS = 0x00,
    WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS = 0x01,
} WASMComponentInstExpressionTag;

// -----------------------------------------------------------------------------
// Result List Tag - Distinguishes with/without type
// instanceexpr ::= 0x00 c:<componentidx> arg*:vec(<instantiatearg>) =>
// (instantiate c arg*)
//            | 0x01 e*:vec(<inlineexport>) => e*
// -----------------------------------------------------------------------------
typedef enum WASMComponentResultListTag {
    WASM_COMP_RESULT_LIST_WITH_TYPE = 0x00,
    WASM_COMP_RESULT_LIST_EMPTY = 0x01,
} WASMComponentResultListTag;

// -----------------------------------------------------------------------------
// Component Declaration Tag - Identifies import vs instance declarations
// componentdecl ::= 0x03 id:<importdecl> => id
//                | id:<instancedecl> => id
// -----------------------------------------------------------------------------
typedef enum WASMComponentComponentDeclTag {
    WASM_COMP_COMPONENT_DECL_CORE_TYPE = 0x00,
    WASM_COMP_COMPONENT_DECL_TYPE = 0x01,
    WASM_COMP_COMPONENT_DECL_ALIAS = 0x02,
    WASM_COMP_COMPONENT_DECL_IMPORT = 0x03,
    WASM_COMP_COMPONENT_DECL_EXPORT = 0x04,
} WASMComponentComponentDeclTag;

// -----------------------------------------------------------------------------
// Instance Declaration Tag - Identifies the kind of instance declaration
// instancedecl  ::= 0x00 t:<core:type>                      => t
//                 | 0x01 t:<type>                           => t
//                 | 0x02 a:<alias>                          => a
//                 | 0x04 ed:<exportdecl>                    => ed
// -----------------------------------------------------------------------------
typedef enum WASMComponentInstDeclTag {
    WASM_COMP_COMPONENT_DECL_INSTANCE_CORE_TYPE = 0x00,
    WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE = 0x01,
    WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS = 0x02,
    WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL = 0x04,
} WASMComponentInstDeclTag;

// -----------------------------------------------------------------------------
// Defined Value Type Tag - Identifies the kind of value type in defvaltype
// defvaltype ::= pvt:<primvaltype> => pvt
//             | 0x72 lt*:vec(<labelvaltype>) => (record (field lt)*)
//             | 0x71 case*:vec(<case>) => (variant case+)
//             | 0x70 t:<valtype> => (list t)
//             | 0x67 t:<valtype> len:<u32> => (list t len)
//             | 0x6f t*:vec(<valtype>) => (tuple t+)
//             | 0x6e l*:vec(<label'>) => (flags l+)
//             | 0x6d l*:vec(<label'>) => (enum l+)
//             | 0x6b t:<valtype> => (option t)
//             | 0x6a t?:<valtype>? u?:<valtype>? => (result t? (error u)?)
//             | 0x69 i:<typeidx> => (own i)
//             | 0x68 i:<typeidx> => (borrow i)
//             | 0x66 t?:<valtype>? => (stream t?)
//             | 0x65 t?:<valtype>? => (future t?)
// -----------------------------------------------------------------------------
typedef enum WASMComponentDefValTypeTag {
    // Primitive value type
    WASM_COMP_DEF_VAL_PRIMVAL = 0x63, // pvt:<primvaltype>
    // Record type (labeled fields)
    WASM_COMP_DEF_VAL_RECORD = 0x72, // 0x72 lt*:vec(<labelvaltype>)
    // Variant type (labeled cases)
    WASM_COMP_DEF_VAL_VARIANT = 0x71, // 0x71 case*:vec(<case>)
    // List types
    WASM_COMP_DEF_VAL_LIST = 0x70,     // 0x70 t:<valtype>
    WASM_COMP_DEF_VAL_LIST_LEN = 0x67, // 0x67 t:<valtype> len:<u32>
    // Tuple type
    WASM_COMP_DEF_VAL_TUPLE = 0x6f, // 0x6f t*:vec(<valtype>)
    // Flags type
    WASM_COMP_DEF_VAL_FLAGS = 0x6e, // 0x6e l*:vec(<label'>)
    // Enum type
    WASM_COMP_DEF_VAL_ENUM = 0x6d, // 0x6d l*:vec(<label'>)
    // Option type
    WASM_COMP_DEF_VAL_OPTION = 0x6b, // 0x6b t:<valtype>
    // Result type
    WASM_COMP_DEF_VAL_RESULT = 0x6a, // 0x6a t?:<valtype>? u?:<valtype>?
    // Handle types
    WASM_COMP_DEF_VAL_OWN = 0x69,    // 0x69 i:<typeidx>
    WASM_COMP_DEF_VAL_BORROW = 0x68, // 0x68 i:<typeidx>
    // Async types
    WASM_COMP_DEF_VAL_STREAM = 0x66, // 0x66 t?:<valtype>?
    WASM_COMP_DEF_VAL_FUTURE = 0x65, // 0x65 t?:<valtype>?
} WASMComponentDefValTypeTag;

// -----------------------------------------------------------------------------
// Resource Representation Tag - Always i32
// -----------------------------------------------------------------------------
typedef enum WASMComponentResourceRepTag {
    WASM_COMP_RESOURCE_REP_I32 = 0x7f, // Always 0x7f for i32 representation
} WASMComponentResourceRepTag;

// -----------------------------------------------------------------------------
// Case End Tag - Always 0x00 at end of case
// -----------------------------------------------------------------------------
typedef enum WASMComponentCaseEndTag {
    WASM_COMP_CASE_END = 0x00, // Always 0x00 at end of case
} WASMComponentCaseEndTag;

// -----------------------------------------------------------------------------
// Simple Structs
// -----------------------------------------------------------------------------
// Core Name Structure - UTF-8 encoded name
// name ::= len:<u32> n:<byte>^len => n
// -----------------------------------------------------------------------------
typedef struct WASMComponentCoreName {
    uint32_t name_len;
    char *name;
} WASMComponentCoreName;

// -----------------------------------------------------------------------------
// Sort Structure - Identifies the kind of definition
// sort ::= 0x00 s:<core:sort> => s
//       | 0x01 => type
//       | 0x02 => func
//       | 0x03 => value
//       | 0x04 => type
//       | 0x05 => component
//       | 0x06 => instance
// -----------------------------------------------------------------------------
typedef struct WASMComponentSort {
    uint8_t sort; // Main sort byte (0x00 for core sorts, 0x01..0x05 for others)
    uint8_t
        core_sort; // If sort==0x00, this is the core sort; otherwise is ignored
} WASMComponentSort;

// -----------------------------------------------------------------------------
// Sort Index Structure - Combines sort with index
// sortidx ::= s:<sort> i:<u32> => s i
// -----------------------------------------------------------------------------
typedef struct WASMComponentSortIdx {
    WASMComponentSort *sort;
    uint32_t idx;
} WASMComponentSortIdx;

// -----------------------------------------------------------------------------
// Value Type Structure - Primitive value or type index
// valtype ::= i:<typeidx> => i
//         | pvt:<primvaltype> => pvt
// -----------------------------------------------------------------------------
typedef struct WASMComponentValueType {
    WASMComponentValueTypeTag type;
    union {
        uint32_t type_idx;
        uint8_t primval_type;
    } type_specific;
} WASMComponentValueType;

// -----------------------------------------------------------------------------
// Value Bound Structure - Equality or type bound
// valuebound ::= 0x00 v:<valueidx> => (eq v)
//            | 0x01 t:<valtype> => (eq t)
// -----------------------------------------------------------------------------
typedef struct WASMComponentValueBound {
    WASMComponentValueBoundTag tag;
    union {
        uint32_t value_idx;
        WASMComponentValueType *value_type;
    } bound;
} WASMComponentValueBound;

// -----------------------------------------------------------------------------
// Type Bound Structure - Equality or subtype bound
// typebound ::= 0x00 t:<typeidx> => (eq t)
//           | 0x01 t:<typeidx> => (sub t)
// -----------------------------------------------------------------------------
typedef struct WASMComponentTypeBound {
    WASMComponentTypeBoundTag tag;
    uint32_t type_idx;
} WASMComponentTypeBound;

// -----------------------------------------------------------------------------
// External Descriptor Structure - Describes import/export kind
// externdesc ::= 0x00 0x11 i:<core:typeidx> => (core module (type i))
//            | 0x01 i:<typeidx> => (func (type i))
//            | 0x02 b:<valuebound> => (value b)
//            | 0x03 b:<typebound> => (type b)
//            | 0x04 i:<typeidx> => (component (type i))
//            | 0x05 i:<typeidx> => (instance (type i))
// -----------------------------------------------------------------------------
typedef struct WASMComponentExternDesc {
    WASMComponentExternDescType type;
    union {
        struct {
            uint8_t type_specific;
            uint32_t type_idx;
        } core_module;
        struct {
            uint32_t type_idx;
        } func;
        struct {
            WASMComponentValueBound *value_bound;
        } value;
        struct {
            WASMComponentTypeBound *type_bound;
        } type;
        struct {
            uint32_t type_idx;
        } component;
        struct {
            uint32_t type_idx;
        } instance;
    } extern_desc;
} WASMComponentExternDesc;

// -----------------------------------------------------------------------------
// Import Name Structure - Simple or versioned name
// importname' ::= 0x00 len:<u32> in:<importname> => in
//             | 0x01 len:<u32> in:<importname> vs:<versionsuffix'> => in vs
// -----------------------------------------------------------------------------
typedef struct WASMComponentImportName {
    WASMComponentImportExportTypeTag tag;
    union {
        struct {
            WASMComponentCoreName *name;
        } simple;
        struct {
            WASMComponentCoreName *name;
            WASMComponentCoreName *version;
        } versioned;
    } imported;
} WASMComponentImportName;

// -----------------------------------------------------------------------------
// Export Name Structure - Simple or versioned name
// exportname' ::= 0x00 len:<u32> ex:<exportname> => ex
//             | 0x01 len:<u32> ex:<exportname> vs:<versionsuffix'> => ex vs
// -----------------------------------------------------------------------------
typedef struct WASMComponentExportName {
    WASMComponentImportExportTypeTag tag;
    union {
        struct {
            WASMComponentCoreName *name;
        } simple;
        struct {
            WASMComponentCoreName *name;
            WASMComponentCoreName *version;
        } versioned;
    } exported;
} WASMComponentExportName;

// -----------------------------------------------------------------------------
// Section 0: Custom Section Structs
// -----------------------------------------------------------------------------
// Custom Section Structure - Arbitrary named data sections
typedef struct WASMComponentCoreCustomSection {
    char *name;          // Name of the custom section
    const uint8_t *data; // Pointer to the custom data
    uint32_t data_len;   // Length of the custom data
} WASMComponentCoreCustomSection;

// -----------------------------------------------------------------------------
// Section 1: Module Section Structs
// -----------------------------------------------------------------------------
// Module Wrapper Structure - Contains core WebAssembly module
typedef struct WASMComponentCoreModuleWrapper {
    struct WASMModule *module;
    void *module_handle;
} WASMComponentCoreModuleWrapper;

// -----------------------------------------------------------------------------
// Section 2: Core Instance Section Structs
// -----------------------------------------------------------------------------
// Core Instance Structure - Core WebAssembly instance
// Inline Export Structure - Exports from core instances
typedef struct WASMComponentInlineExport {
    WASMComponentCoreName *name;
    WASMComponentSortIdx *sort_idx;
} WASMComponentInlineExport;

// Instantiate Argument Structure - Arguments for instantiation
typedef struct WASMComponentInstArg {
    WASMComponentCoreName *name;
    union {
        WASMComponentSortIdx *sort_idx;
        uint32_t instance_idx;
    } idx;
} WASMComponentInstArg;

typedef union WASMInstExpr {
    struct {
        uint32_t idx;
        uint32_t arg_len;
        WASMComponentInstArg *args;
    } with_args;
    struct {
        uint32_t inline_expr_len;
        WASMComponentInlineExport *inline_expr;
    } without_args;
} WASMInstExpr;

typedef struct WASMComponentCoreInst {
    WASMComponentInstExpressionTag instance_expression_tag;
    WASMInstExpr expression;
} WASMComponentCoreInst;

// Core Instance Section Structure - Vector of core instances
typedef struct WASMComponentCoreInstSection {
    uint32_t count;
    WASMComponentCoreInst *instances;
} WASMComponentCoreInstSection;

// -----------------------------------------------------------------------------
// Section 3: Type Section Structs (Core Types)
// -----------------------------------------------------------------------------

// Core Number Types - From WebAssembly Core spec
// numtype ::= 0x7F => i32 | 0x7E => i64 | 0x7D => f32 | 0x7C => f64
typedef enum WASMCoreNumTypeTag {
    WASM_CORE_NUM_TYPE_I32 = 0x7F, // i32
    WASM_CORE_NUM_TYPE_I64 = 0x7E, // i64
    WASM_CORE_NUM_TYPE_F32 = 0x7D, // f32
    WASM_CORE_NUM_TYPE_F64 = 0x7C, // f64
} WASMCoreNumTypeTag;

// Core Vector Types - From WebAssembly Core spec
// vectype ::= 0x7B => v128
typedef enum WASMCoreVectorTypeTag {
    WASM_CORE_VECTOR_TYPE_V128 = 0x7B, // v128
} WASMCoreVectorTypeTag;

// Core Packed Types - From WebAssembly Core spec
// packedtype ::= 0x78 => i8 | 0x77 => i16
typedef enum WASMCorePackedTypeTag {
    WASM_CORE_PACKED_TYPE_I8 = 0x78,  // i8
    WASM_CORE_PACKED_TYPE_I16 = 0x77, // i16
} WASMCorePackedTypeTag;

// Core Abstract Heap Types - From WebAssembly Core spec
// absheaptype ::= 0x73 => nofunc | 0x72 => noextern | 0x71 => none | 0x70 =>
// func
//               | 0x6F => extern | 0x6E => any | 0x6D => eq | 0x6C => i31
//               | 0x6B => struct | 0x6A => array
typedef enum WASMCoreAbsHeapTypeTag {
    WASM_CORE_ABS_HEAP_TYPE_NOFUNC = 0x73,   // nofunc
    WASM_CORE_ABS_HEAP_TYPE_NOEXTERN = 0x72, // noextern
    WASM_CORE_ABS_HEAP_TYPE_NONE = 0x71,     // none
    WASM_CORE_ABS_HEAP_TYPE_FUNC = 0x70,     // func
    WASM_CORE_ABS_HEAP_TYPE_EXTERN = 0x6F,   // extern
    WASM_CORE_ABS_HEAP_TYPE_ANY = 0x6E,      // any
    WASM_CORE_ABS_HEAP_TYPE_EQ = 0x6D,       // eq
    WASM_CORE_ABS_HEAP_TYPE_I31 = 0x6C,      // i31
    WASM_CORE_ABS_HEAP_TYPE_STRUCT = 0x6B,   // struct
    WASM_CORE_ABS_HEAP_TYPE_ARRAY = 0x6A,    // array
} WASMCoreAbsHeapTypeTag;

// Core Heap Type - Can be abstract heap type or type index
// heaptype ::= ht:absheaptype => ht | x:s33 => x (if x >= 0)
typedef enum WASMComponentCoreHeapTypeTag {
    WASM_CORE_HEAP_TYPE_ABSTRACT,
    WASM_CORE_HEAP_TYPE_CONCRETE
} WASMComponentCoreHeapTypeTag;

typedef struct WASMComponentCoreHeapType {
    WASMComponentCoreHeapTypeTag tag;
    union {
        WASMCoreAbsHeapTypeTag abstract_type;
        uint32_t concrete_index; // Type index for heap type (s33)
    } heap_type;
} WASMComponentCoreHeapType;

// Core Value Type Structure - Can be number, vector, or reference type
// valtype ::= t:numtype => t | t:vectype => t | t:reftype => t
typedef enum WASMComponentCoreValTypeTag {
    WASM_CORE_VALTYPE_NUM,
    WASM_CORE_VALTYPE_VECTOR,
    WASM_CORE_VALTYPE_REF
} WASMComponentCoreValTypeTag;

typedef enum WASMComponentCoreRefType {
    WASM_CORE_REFTYPE_FUNC_REF = 0x70,
    WASM_CORE_REFTYPE_EXTERN_REF = 0x6F,
} WASMComponentCoreRefType;

typedef struct WASMComponentCoreValType {
    WASMComponentCoreValTypeTag tag;
    union {
        WASMCoreNumTypeTag num_type;
        WASMCoreVectorTypeTag vector_type;
        WASMComponentCoreRefType ref_type;
    } type;
} WASMComponentCoreValType;

// Core Storage Type - Can be value type or packed type
// storagetype ::= t:valtype => t | t:packedtype => t
typedef enum WASMComponentCoreStorageTypeTag {
    WASM_CORE_STORAGETYPE_VAL,
    WASM_CORE_STORAGETYPE_PACKED
} WASMComponentCoreStorageTypeTag;

typedef struct WASMComponentCoreStorageType {
    WASMComponentCoreStorageTypeTag tag;
    union {
        WASMComponentCoreValType val_type;
        WASMCorePackedTypeTag packed_type;
    } storage_type;
} WASMComponentCoreStorageType;

// Core Field Type Structure - Has mutability and storage type
// fieldtype ::= st:storagetype m:mut => m st
typedef struct WASMComponentCoreFieldType {
    bool is_mutable; // true for var, false for const
    WASMComponentCoreStorageType storage_type;
} WASMComponentCoreFieldType;

// Core Result Type Structure - Vector of value types
// resulttype ::= t*:vec(valtype) => [t*]
typedef struct WASMComponentCoreResultType {
    uint32_t count;
    WASMComponentCoreValType *val_types;
} WASMComponentCoreResultType;

// Core Function Type Structure
// functype ::= rt1:resulttype rt2:resulttype => rt1 -> rt2
typedef struct WASMComponentCoreFuncType {
    WASMComponentCoreResultType params;  // rt1
    WASMComponentCoreResultType results; // rt2
} WASMComponentCoreFuncType;

// Core Array Type Structure
// arraytype ::= ft:fieldtype => ft
typedef struct WASMComponentCoreArrayType {
    WASMComponentCoreFieldType field_type;
} WASMComponentCoreArrayType;

// Core Struct Type Structure - Vector of field types
// structtype ::= ft*:vec(fieldtype) => ft*
typedef struct WASMComponentCoreStructType {
    uint32_t field_count;
    WASMComponentCoreFieldType *fields;
} WASMComponentCoreStructType;

// Core Composite Type Structure
// comptype ::= 0x5E at:arraytype => array at
//           | 0x5F st:structtype => struct st
//           | 0x60 ft:functype => func ft
typedef enum WASMComponentCoreCompTypeTag {
    WASM_CORE_COMPTYPE_ARRAY = 0x5E,
    WASM_CORE_COMPTYPE_STRUCT = 0x5F,
    WASM_CORE_COMPTYPE_FUNC = 0x60
} WASMComponentCoreCompTypeTag;

typedef struct WASMComponentCoreCompType {
    WASMComponentCoreCompTypeTag tag;
    union {
        WASMComponentCoreArrayType array_type;
        WASMComponentCoreStructType struct_type;
        WASMComponentCoreFuncType func_type;
    } type;
} WASMComponentCoreCompType;

// Core SubType Structure
// subtype ::= 0x50 x*:vec(typeidx) ct:comptype => sub x* ct
//          | 0x4F x*:vec(typeidx) ct:comptype => sub final x* ct
//          | ct:comptype => sub final ε ct
typedef struct WASMComponentCoreSubType {
    bool is_final;
    uint32_t supertype_count;
    uint32_t *supertypes; // Vector of type indices (can be empty for final with
                          // no supertypes)
    WASMComponentCoreCompType comptype;
} WASMComponentCoreSubType;

// Core RecType Structure - Recursive type
// rectype ::= 0x4E st*:vec(subtype) => rec st*
//          | st:subtype => rec st
typedef struct WASMComponentCoreRecType {
    uint32_t subtype_count;
    WASMComponentCoreSubType *subtypes;
} WASMComponentCoreRecType;

// Core Module Type Structure

// Core Import and Import Description Structures
// Based on WebAssembly specification:
// https://webassembly.github.io/gc/core/binary/types.html core:import ::=
// nm:<core:name> d:<core:importdesc> => (import nm d) core:importdesc ::= 0x00
// ft:<core:functype> => (func (type ft))
//                   | 0x01 tt:<core:tabletype> => (table tt)
//                   | 0x02 mt:<core:memtype> => (memory mt)
//                   | 0x03 gt:<core:globaltype> => (global gt)

typedef enum WASMComponentCoreImportDescType {
    WASM_CORE_IMPORTDESC_FUNC =
        0x00, // 0x00 ft:<core:functype> => (func (type ft))
    WASM_CORE_IMPORTDESC_TABLE = 0x01, // 0x01 tt:<core:tabletype> => (table tt)
    WASM_CORE_IMPORTDESC_MEMORY = 0x02, // 0x02 mt:<core:memtype> => (memory mt)
    WASM_CORE_IMPORTDESC_GLOBAL =
        0x03 // 0x03 gt:<core:globaltype> => (global gt)
} WASMComponentCoreImportDescType;

typedef enum WASMComponentCoreLimitsTag {
    WASM_CORE_LIMITS_MIN = 0x00,
    WASM_CORE_LIMITS_MAX = 0x01
} WASMComponentCoreLimitsTag;

typedef struct WASMComponentCoreLimits {
    WASMComponentCoreLimitsTag tag;
    union {
        struct {
            uint32_t min;
        } limits;
        struct {
            uint32_t min;
            uint32_t max;
        } limits_max;
    } lim;
} WASMComponentCoreLimits;

typedef enum WASMComponentCoreGlobalTag {
    WASM_CORE_GLOBAL_MUTABLE = 0x00,
    WASM_CORE_GLOBAL_IMMUTABLE = 0x01
} WASMComponentCoreGlobalTag;

typedef struct WASMComponentCoreImportDesc {
    WASMComponentCoreImportDescType type;
    union {
        uint32_t func_type_idx;
        struct {
            WASMComponentCoreRefType ref_type;
            WASMComponentCoreLimits *limits;
        } table_type;
        struct {
            WASMComponentCoreLimits *limits;
        } memory_type;
        struct {
            WASMComponentCoreValType val_type;
            bool is_mutable;
        } global_type;
    } desc;
} WASMComponentCoreImportDesc;

typedef struct WASMComponentCoreImport {
    WASMComponentCoreName *mod_name;
    WASMComponentCoreName *nm;
    WASMComponentCoreImportDesc *import_desc;
} WASMComponentCoreImport;

// Core Export Declaration Structure
// core:exportdecl ::= nm:<core:name> d:<core:importdesc> => (export nm d)
typedef struct WASMComponentCoreExportDecl {
    WASMComponentCoreName *name;
    WASMComponentCoreImportDesc *export_desc;
} WASMComponentCoreExportDecl;

// Core Alias Structure
// core:alias ::= 0x00 x:<core:outeridx> n:<core:name> => (alias outer x n)
//              | 0x01 x:<core:outeridx> n:<core:name> => (alias outer x n)
//              | 0x02 x:<core:outeridx> n:<core:name> => (alias outer x n)
//              | 0x03 x:<core:outeridx> n:<core:name> => (alias outer x n)

typedef enum WASMComponentCoreAliasType {
    WASM_CORE_ALIAS_FUNC =
        0x00, // 0x00 x:<core:outeridx> n:<core:name> => (alias outer x n)
    WASM_CORE_ALIAS_TABLE =
        0x01, // 0x01 x:<core:outeridx> n:<core:name> => (alias outer x n)
    WASM_CORE_ALIAS_MEMORY =
        0x02, // 0x02 x:<core:outeridx> n:<core:name> => (alias outer x n)
    WASM_CORE_ALIAS_GLOBAL =
        0x03 // 0x03 x:<core:outeridx> n:<core:name> => (alias outer x n)
} WASMComponentCoreAliasType;

typedef struct WASMComponentCoreAliasTarget {
    uint32_t ct;
    uint32_t index;
} WASMComponentCoreAliasTarget;

typedef struct WASMComponentCoreAlias {
    WASMComponentSort sort;
    WASMComponentCoreAliasTarget alias_target;
} WASMComponentCoreAlias;

// Core Module Declaration Structure
// moduletype ::= 0x50 md*:vec(moduledecl) => (module md*)
// moduledecl ::= 0x00 i:<core:import> => i
//              | 0x01 t:<core:type> => t
//              | 0x02 a:<core:alias> => a
//              | 0x03 e:<core:exportdecl> => e
typedef enum WASMComponentCoreModuleDeclTag {
    WASM_CORE_MODULEDECL_IMPORT = 0x00,
    WASM_CORE_MODULEDECL_TYPE = 0x01,
    WASM_CORE_MODULEDECL_ALIAS = 0x02,
    WASM_CORE_MODULEDECL_EXPORT = 0x03
} WASMComponentCoreModuleDeclTag;

typedef struct WASMComponentCoreModuleDecl {
    WASMComponentCoreModuleDeclTag tag;
    union {
        struct {
            WASMComponentCoreImport *import;
        } import_decl;
        struct {
            struct WASMComponentCoreType *type;
        } type_decl;
        struct {
            WASMComponentCoreAlias *alias;
        } alias_decl;
        struct {
            WASMComponentCoreExportDecl *export_decl;
        } export_decl;
    } decl;
} WASMComponentCoreModuleDecl;

typedef struct WASMComponentCoreModuleType {
    uint32_t decl_count;
    WASMComponentCoreModuleDecl *declarations;
} WASMComponentCoreModuleType;

// Core DefType Structure
// core:deftype ::= rt:<core:rectype> => rt (WebAssembly 3.0)
//               | 0x00 0x50 x*:vec(<core:typeidx>) ct:<core:comptype> => sub x*
//               ct (WebAssembly 3.0) | mt:<core:moduletype> => mt
typedef enum WASMComponentCoreDefTypeTag {
    WASM_CORE_DEFTYPE_RECTYPE,
    WASM_CORE_DEFTYPE_SUBTYPE,
    WASM_CORE_DEFTYPE_MODULETYPE
} WASMComponentCoreDefTypeTag;

typedef struct WASMComponentCoreModuleSubType {
    uint32_t supertype_count;
    uint32_t *supertypes;
    WASMComponentCoreCompType *comptype;
} WASMComponentCoreModuleSubType;

typedef struct WASMComponentCoreDefType {
    WASMComponentCoreDefTypeTag tag;
    union {
        WASMComponentCoreRecType *rectype;
        WASMComponentCoreModuleSubType *subtype;
        WASMComponentCoreModuleType *moduletype;
    } type;
} WASMComponentCoreDefType;

// Core Type Structure
// core:type ::= dt:<core:deftype> => (type dt)
typedef struct WASMComponentCoreType {
    WASMComponentCoreDefType *deftype;
} WASMComponentCoreType;

// Core Type Section Structure - Vector of core types
typedef struct WASMComponentCoreTypeSection {
    uint32_t count;
    WASMComponentCoreType *types;
} WASMComponentCoreTypeSection;

// -----------------------------------------------------------------------------
// Section 4: Component Section Structs
// -----------------------------------------------------------------------------
// Note: Component structure is defined later in Main Component Structures

// -----------------------------------------------------------------------------
// Section 5: Instances Section Structs
// -----------------------------------------------------------------------------
// Instance Structure - Component instance
typedef struct WASMComponentInst {
    WASMComponentInstExpressionTag instance_expression_tag;
    WASMInstExpr expression;
} WASMComponentInst;

// Instance Section Structure - Vector of instances
typedef struct WASMComponentInstSection {
    uint32_t count;
    WASMComponentInst *instances;
} WASMComponentInstSection;

// -----------------------------------------------------------------------------
// Section 6: Alias Section Structs
// -----------------------------------------------------------------------------
// Alias Target Structures - Used in alias definitions
typedef struct WASMComponentAliasTargetExport {
    uint32_t instance_idx;
    WASMComponentCoreName *name;
} WASMComponentAliasTargetExport;

typedef struct WASMComponentAliasTargetCoreExport {
    uint32_t instance_idx;
    WASMComponentCoreName *name;
} WASMComponentAliasTargetCoreExport;

typedef struct WASMComponentAliasTargetOuter {
    uint32_t ct;
    uint32_t idx;
} WASMComponentAliasTargetOuter;

// Alias Definition Structure - Projects definitions from other components
// alias ::= s:<sort> t:<aliastarget> => (alias t (s))
typedef struct WASMComponentAliasDefinition {
    WASMComponentSort *sort;
    WASMComponentAliasTargetType alias_target_type;
    union {
        struct {
            uint32_t instance_idx;
            WASMComponentCoreName *name;
        } exported;
        struct {
            uint32_t instance_idx;
            WASMComponentCoreName *name;
        } core_exported;
        struct {
            uint32_t ct;
            uint32_t idx;
        } outer;
    } target;
} WASMComponentAliasDefinition;

// Alias Section Structure - Vector of alias definitions
typedef struct WASMComponentAliasSection {
    uint32_t count;
    WASMComponentAliasDefinition *aliases;
} WASMComponentAliasSection;

// -----------------------------------------------------------------------------
// Section 7: Types Section Structs
// -----------------------------------------------------------------------------
// Label-Value Type Structure - Labeled value type
// labelvaltype ::= l:<label'> t:<valtype> => l t
typedef struct WASMComponentLabelValType {
    WASMComponentCoreName *label;
    WASMComponentValueType *value_type;
} WASMComponentLabelValType;

// Case Value Type Structure - Labeled case type
// case ::= l:<label'> t?:<valtype>? => l t?
typedef struct WASMComponentCaseValType {
    WASMComponentCoreName *label;
    WASMComponentValueType *value_type;
} WASMComponentCaseValType;

// Record Type Structure - Labeled fields
// record ::= lt*:vec(<labelvaltype>) => (record lt*)
typedef struct WASMComponentRecordType {
    uint32_t count;
    WASMComponentLabelValType *fields;
} WASMComponentRecordType;

// Variant Type Structure - Labeled cases
// variant ::= case*:vec(<case>) => (variant case*)
typedef struct WASMComponentVariantType {
    uint32_t count;
    WASMComponentCaseValType *cases;
} WASMComponentVariantType;

// List Type Structure - Homogeneous list
// list ::= t:<valtype> => (list t)
typedef struct WASMComponentListType {
    WASMComponentValueType *element_type;
} WASMComponentListType;

// List Length Type Structure - Fixed-length list
// list-len ::= t:<valtype> len:<u32> => (list t len)
typedef struct WASMComponentListLenType {
    uint32_t len;
    WASMComponentValueType *element_type;
} WASMComponentListLenType;

// Tuple Type Structure - Heterogeneous tuple
// tuple ::= t*:vec(<valtype>) => (tuple t*)
typedef struct WASMComponentTupleType {
    uint32_t count;
    WASMComponentValueType *element_types;
} WASMComponentTupleType;

// Flag Type Structure - Named flags
// flags ::= l*:vec(<label'>) => (flags l*)
typedef struct WASMComponentFlagType {
    uint32_t count;
    WASMComponentCoreName *flags;
} WASMComponentFlagType;

// Enum Type Structure - Named enum
// enum ::= l*:vec(<label'>) => (enum l*)
typedef struct WASMComponentEnumType {
    uint32_t count;
    WASMComponentCoreName *labels;
} WASMComponentEnumType;

// Option Type Structure - Optional value
// option ::= t:<valtype> => (option t)
typedef struct WASMComponentOptionType {
    WASMComponentValueType *element_type;
} WASMComponentOptionType;

// Result Type Structure - Success/error result
// result ::= t?:<valtype>? u?:<valtype>? => (result t? u?)
typedef struct WASMComponentResultType {
    WASMComponentValueType *result_type; // Optional (can be NULL)
    WASMComponentValueType *error_type;  // Optional (can be NULL)
} WASMComponentResultType;

// Own Type Structure - Owned handle
// own ::= i:<typeidx> => (own i)
typedef struct WASMComponentOwnType {
    uint32_t type_idx;
} WASMComponentOwnType;

// Borrow Type Structure - Borrowed handle
// borrow ::= i:<typeidx> => (borrow i)
typedef struct WASMComponentBorrowType {
    uint32_t type_idx;
} WASMComponentBorrowType;

// Stream Type Structure - Async stream
// stream ::= t?:<valtype>? => (stream t?)
typedef struct WASMComponentStreamType {
    WASMComponentValueType *element_type;
} WASMComponentStreamType;

// Future Type Structure - Async future
// future ::= t?:<valtype>? => (future t?)
typedef struct WASMComponentFutureType {
    WASMComponentValueType *element_type;
} WASMComponentFutureType;

// DefValType Structure - Defined value type
// defvaltype ::= 0x63 pvt:<primvaltype> => pvt
//             | 0x72 lt*:vec(<labelvaltype>) => (record lt*)
//             | 0x71 case*:vec(<case>) => (variant case*)
//             | 0x70 t:<valtype> => (list t)
//             | 0x67 t:<valtype> len:<u32> => (list t len)
//             | 0x6f t*:vec(<valtype>) => (tuple t*)
//             | 0x6e l*:vec(<label'>) => (flags l*)
//             | 0x6d l*:vec(<label'>) => (enum l*)
//             | 0x6b t:<valtype> => (option t)
//             | 0x6a t?:<valtype>? u?:<valtype>? => (result t? u?)
//             | 0x69 i:<typeidx> => (own i)
//             | 0x68 i:<typeidx> => (borrow i)
//             | 0x66 t?:<valtype>? => (stream t?)
//             | 0x65 t?:<valtype>? => (future t?)
typedef struct WASMComponentDefValType {
    WASMComponentDefValTypeTag tag;
    union {
        WASMComponentPrimValType primval;
        WASMComponentRecordType *record;
        WASMComponentVariantType *variant;
        WASMComponentListType *list;
        WASMComponentListLenType *list_len;
        WASMComponentTupleType *tuple;
        WASMComponentFlagType *flag;
        WASMComponentEnumType *enum_type;
        WASMComponentOptionType *option;
        WASMComponentResultType *result;
        WASMComponentOwnType *owned;
        WASMComponentBorrowType *borrow;
        WASMComponentStreamType *stream;
        WASMComponentFutureType *future;
    } def_val;
} WASMComponentDefValType;

// Parameter List Structure - Function parameters
// paramtype ::= l:<label'> t:<valtype> => l t
// paramlist ::= pt*:vec(<paramtype>) => pt*
typedef struct WASMComponentParamList {
    uint32_t count;
    WASMComponentLabelValType *params;
} WASMComponentParamList;

// Result List Structure - Function results
// resulttype ::= l:<label'> t:<valtype> => l t
// resultlist ::= rt*:vec(<resulttype>) => rt*
typedef struct WASMComponentResultList {
    WASMComponentResultListTag tag;
    WASMComponentValueType *results;
} WASMComponentResultList;

// Function Type Structure - Function signature
// functype ::= pt*:vec(<paramtype>) rt*:vec(<resulttype>) => (func pt* rt*)
typedef struct WASMComponentFuncType {
    WASMComponentParamList *params;
    WASMComponentResultList *results;
} WASMComponentFuncType;

// Import Declaration Structure - Component import
// importdecl ::= n:<importname> d:<externdesc> => (import n d)
typedef struct WASMComponentImportDecl {
    WASMComponentImportName *import_name;
    WASMComponentExternDesc *extern_desc;
} WASMComponentImportDecl;

// Export Declaration Structure - Component export
// exportdecl ::= n:<exportname> d:<externdesc> => (export n d)
typedef struct WASMComponentComponentDeclExport {
    WASMComponentExportName *export_name;
    WASMComponentExternDesc *extern_desc;
} WASMComponentComponentDeclExport;

// Instance Declaration Structure - Instance definition
// instancedecl ::= 0x00 ct:<core:typeidx> => (core type ct)
//               | 0x01 t:<typeidx> => (type t)
//               | 0x02 a:<alias> => a
//               | 0x04 ed:<exportdecl> => ed
typedef struct WASMComponentInstDecl {
    WASMComponentInstDeclTag tag;
    union {
        WASMComponentCoreType *core_type;
        struct WASMComponentTypes *type;
        WASMComponentAliasDefinition *alias;
        WASMComponentComponentDeclExport *export_decl;
    } decl;
} WASMComponentInstDecl;

// Component Declaration Structure - Component definition
// componentdecl ::= 0x03 id:<importdecl> => id
//                | 0x00 id:<instancedecl> => id
typedef struct WASMComponentComponentDecl {
    WASMComponentComponentDeclTag tag;
    union {
        WASMComponentImportDecl *import_decl;
        WASMComponentInstDecl *instance_decl;
    } decl;
} WASMComponentComponentDecl;

// Component Type Structure - Component interface
// componenttype ::= ct*:vec(<componentdecl>) => (component ct*)
typedef struct WASMComponentComponentType {
    uint32_t count;
    WASMComponentComponentDecl *component_decls;
} WASMComponentComponentType;

// Instance Type Structure - Instance interface
// instancetype ::= it*:vec(<instancedecl>) => (instance it*)
typedef struct WASMComponentInstType {
    uint32_t count;
    WASMComponentInstDecl *instance_decls;
} WASMComponentInstType;

// Resource Type Sync Structure - Resource definition
// resourcetype ::= 0x3f i:<typeidx> => (resource (rep i32) (dtor i))
typedef struct WASMComponentResourceTypeSync {
    bool has_dtor;
    uint32_t dtor_func_idx;
} WASMComponentResourceTypeSync;

// Resource Type Async Structure - Resource definition
// resourcetype ::= 0x3e i:<typeidx> => (resource (rep i32) (dtor i) (dtor i))
typedef struct WASMComponentResourceTypeAsync {
    uint32_t dtor_func_idx;
    uint32_t callback_func_idx;
} WASMComponentResourceTypeAsync;

// Resource Type Structure - Resource definition
// resourcetype ::= 0x3f i:<typeidx> => (resource (rep i32) (dtor i))
//               | 0x3e i:<typeidx> => (resource (rep i32) (dtor i) (dtor i))
typedef struct WASMComponentResourceType {
    WASMComponentTypesTag tag;
    union {
        WASMComponentResourceTypeSync *sync;
        WASMComponentResourceTypeAsync *async;
    } resource;
} WASMComponentResourceType;

// Types Structure - Union of all type kinds
// type ::= 0x63 pvt:<primvaltype> => pvt
//       | 0x72 lt*:vec(<labelvaltype>) => (record lt*)
//       | 0x71 case*:vec(<case>) => (variant case*)
//       | 0x70 t:<valtype> => (list t)
//       | 0x67 t:<valtype> len:<u32> => (list t len)
//       | 0x6f t*:vec(<valtype>) => (tuple t*)
//       | 0x6e l*:vec(<label'>) => (flags l*)
//       | 0x6d l*:vec(<label'>) => (enum l*)
//       | 0x6b t:<valtype> => (option t)
//       | 0x6a t?:<valtype>? u?:<valtype>? => (result t? u?)
//       | 0x69 i:<typeidx> => (own i)
//       | 0x68 i:<typeidx> => (borrow i)
//       | 0x66 t?:<valtype>? => (stream t?)
//       | 0x65 t?:<valtype>? => (future t?)
//       | 0x40 pt*:vec(<paramtype>) rt*:vec(<resulttype>) => (func pt* rt*)
//       | 0x41 ct*:vec(<componenttype>) => (component ct*)
//       | 0x42 it*:vec(<instancetype>) => (instance it*)
//       | 0x3f i:<typeidx> => (resource (rep i32) (dtor i))
//       | 0x3e i:<typeidx> => (resource (rep i32) (dtor i) (dtor i))
typedef struct WASMComponentTypes {
    WASMComponentTypesTag tag;
    union {
        struct WASMComponentDefValType *def_val_type;
        struct WASMComponentFuncType *func_type;
        struct WASMComponentComponentType *component_type;
        struct WASMComponentInstType *instance_type;
        struct WASMComponentResourceType *resource_type;
    } type;
} WASMComponentTypes;

// Type Section Structure - Vector of types
typedef struct WASMComponentTypeSection {
    uint32_t count;
    WASMComponentTypes *types;
} WASMComponentTypeSection;

// -----------------------------------------------------------------------------
// Section 8: Canons Section Structs
// -----------------------------------------------------------------------------
// Canonical definitions for lifting/lowering and built-ins
typedef enum WASMComponentCanonType {
    WASM_COMP_CANON_LIFT = 0x00,                 // canon lift
    WASM_COMP_CANON_LOWER = 0x01,                // canon lower
    WASM_COMP_CANON_RESOURCE_NEW = 0x02,         // canon resource.new
    WASM_COMP_CANON_RESOURCE_DROP = 0x03,        // canon resource.drop
    WASM_COMP_CANON_RESOURCE_REP = 0x04,         // canon resource.rep
    WASM_COMP_CANON_RESOURCE_DROP_ASYNC = 0x07,  // canon resource.drop async
    WASM_COMP_CANON_BACKPRESSURE_SET = 0x08,     // canon backpressure.set
    WASM_COMP_CANON_TASK_RETURN = 0x09,          // canon task.return
    WASM_COMP_CANON_TASK_CANCEL = 0x05,          // canon task.cancel
    WASM_COMP_CANON_CONTEXT_GET = 0x0a,          // canon context.get
    WASM_COMP_CANON_CONTEXT_SET = 0x0b,          // canon context.set
    WASM_COMP_CANON_YIELD = 0x0c,                // canon yield
    WASM_COMP_CANON_SUBTASK_CANCEL = 0x06,       // canon subtask.cancel
    WASM_COMP_CANON_SUBTASK_DROP = 0x0d,         // canon subtask.drop
    WASM_COMP_CANON_STREAM_NEW = 0x0e,           // canon stream.new
    WASM_COMP_CANON_STREAM_READ = 0x0f,          // canon stream.read
    WASM_COMP_CANON_STREAM_WRITE = 0x10,         // canon stream.write
    WASM_COMP_CANON_STREAM_CANCEL_READ = 0x11,   // canon stream.cancel-read
    WASM_COMP_CANON_STREAM_CANCEL_WRITE = 0x12,  // canon stream.cancel-write
    WASM_COMP_CANON_STREAM_DROP_READABLE = 0x13, // canon stream.drop-readable
    WASM_COMP_CANON_STREAM_DROP_WRITABLE = 0x14, // canon stream.drop-writable
    WASM_COMP_CANON_FUTURE_NEW = 0x15,           // canon future.new
    WASM_COMP_CANON_FUTURE_READ = 0x16,          // canon future.read
    WASM_COMP_CANON_FUTURE_WRITE = 0x17,         // canon future.write
    WASM_COMP_CANON_FUTURE_CANCEL_READ = 0x18,   // canon future.cancel-read
    WASM_COMP_CANON_FUTURE_CANCEL_WRITE = 0x19,  // canon future.cancel-write
    WASM_COMP_CANON_FUTURE_DROP_READABLE = 0x1a, // canon future.drop-readable
    WASM_COMP_CANON_FUTURE_DROP_WRITABLE = 0x1b, // canon future.drop-writable
    WASM_COMP_CANON_ERROR_CONTEXT_NEW = 0x1c,    // canon error-context.new
    WASM_COMP_CANON_ERROR_CONTEXT_DEBUG =
        0x1d, // canon error-context.debug-message
    WASM_COMP_CANON_ERROR_CONTEXT_DROP = 0x1e,    // canon error-context.drop
    WASM_COMP_CANON_WAITABLE_SET_NEW = 0x1f,      // canon waitable-set.new
    WASM_COMP_CANON_WAITABLE_SET_WAIT = 0x20,     // canon waitable-set.wait
    WASM_COMP_CANON_WAITABLE_SET_POLL = 0x21,     // canon waitable-set.poll
    WASM_COMP_CANON_WAITABLE_SET_DROP = 0x22,     // canon waitable-set.drop
    WASM_COMP_CANON_WAITABLE_JOIN = 0x23,         // canon waitable.join
    WASM_COMP_CANON_THREAD_SPAWN_REF = 0x40,      // canon thread.spawn_ref
    WASM_COMP_CANON_THREAD_SPAWN_INDIRECT = 0x41, // canon thread.spawn_indirect
    WASM_COMP_CANON_THREAD_AVAILABLE_PAR =
        0x42 // canon thread.available_parallelism
} WASMComponentCanonType;

// Canonical options for lift/lower operations
typedef enum WASMComponentCanonOptTag {
    WASM_COMP_CANON_OPT_STRING_UTF8 = 0x00,  // string-encoding=utf8
    WASM_COMP_CANON_OPT_STRING_UTF16 = 0x01, // string-encoding=utf16
    WASM_COMP_CANON_OPT_STRING_LATIN1_UTF16 =
        0x02,                               // string-encoding=latin1+utf16
    WASM_COMP_CANON_OPT_MEMORY = 0x03,      // (memory m)
    WASM_COMP_CANON_OPT_REALLOC = 0x04,     // (realloc f)
    WASM_COMP_CANON_OPT_POST_RETURN = 0x05, // (post-return f)
    WASM_COMP_CANON_OPT_ASYNC = 0x06,       // async
    WASM_COMP_CANON_OPT_CALLBACK = 0x07     // (callback f)
} WASMComponentCanonOptTag;

// Canon option with payload, opts := vec<canonopt> where some options carry an
// immediate:
//  - 0x03 (memory m)       -> core:memidx (u32)
//  - 0x04 (realloc f)      -> core:funcidx (u32)
//  - 0x05 (post-return f)  -> core:funcidx (u32)
//  - 0x07 (callback f)     -> core:funcidx (u32)
//  Others (string-encoding, async) carry no immediates.
typedef struct WASMComponentCanonOpt {
    WASMComponentCanonOptTag tag;
    union {
        struct {               /* no payload */
        } string_utf8;         /* 0x00 */
        struct {               /* no payload */
        } string_utf16;        /* 0x01 */
        struct {               /* no payload */
        } string_latin1_utf16; /* 0x02 */
        struct {
            uint32_t mem_idx;
        } memory; /* 0x03 */
        struct {
            uint32_t func_idx;
        } realloc_opt; /* 0x04 */
        struct {
            uint32_t func_idx;
        } post_return; /* 0x05 */
        struct {       /* no payload */
        } async;       /* 0x06 */
        struct {
            uint32_t func_idx;
        } callback; /* 0x07 */
    } payload;
} WASMComponentCanonOpt;

typedef struct WASMComponentCanonOpts {
    uint32_t canon_opts_count;
    WASMComponentCanonOpt *canon_opts;
} WASMComponentCanonOpts;

// Canonical definition structure
typedef struct WASMComponentCanon {
    WASMComponentCanonType tag;
    union {
        // 0x00 0x00 f:<core:funcidx> opts:<opts> ft:<typeidx>
        struct {
            uint32_t core_func_idx;
            struct WASMComponentCanonOpts *canon_opts;
            uint32_t type_idx;
        } lift;
        // 0x01 0x00 f:<funcidx> opts:<opts>
        struct {
            uint32_t func_idx;
            struct WASMComponentCanonOpts *canon_opts;
        } lower;
        // 0x02 rt:<typeidx>
        struct {
            uint32_t resource_type_idx;
        } resource_new;
        // 0x03 rt:<typeidx> or 0x07 rt:<typeidx> (async)
        struct {
            uint32_t resource_type_idx;
            bool async;
        } resource_drop;
        // 0x04 rt:<typeidx>
        struct {
            uint32_t resource_type_idx;
        } resource_rep;
        // 0x08 (no parameters)
        struct {
            // No parameters
        } backpressure_set;
        // 0x09 rs:<resultlist> opts:<opts>
        struct {
            WASMComponentResultList *result_list;
            struct WASMComponentCanonOpts *canon_opts;
        } task_return;
        // 0x05 (no parameters)
        struct {
            // No parameters
        } task_cancel;
        // 0x0a 0x7f i:<u32> or 0x0b 0x7f i:<u32>
        struct {
            uint32_t context_idx;
        } context_get_set;
        // 0x0c cancel?:<cancel?>
        struct {
            bool cancellable;
        } yield;
        // 0x06 async?:<async?>
        struct {
            bool async;
        } subtask_cancel;
        // 0x0d (no parameters)
        struct {
            // No parameters
        } subtask_drop;
        // 0x0e t:<typeidx>
        struct {
            uint32_t stream_type_idx;
        } stream_new;
        // 0x0f t:<typeidx> opts:<opts> or 0x10 t:<typeidx> opts:<opts>
        struct {
            uint32_t stream_type_idx;
            struct WASMComponentCanonOpts *canon_opts;
        } stream_read_write;
        // 0x11 t:<typeidx> async?:<async?> or 0x12 t:<typeidx> async?:<async?>
        struct {
            uint32_t stream_type_idx;
            bool async;
        } stream_cancel_read_write;
        // 0x13 t:<typeidx> or 0x14 t:<typeidx>
        struct {
            uint32_t stream_type_idx;
        } stream_drop_readable_writable;
        // 0x15 t:<typeidx>
        struct {
            uint32_t future_type_idx;
        } future_new;
        // 0x16 t:<typeidx> opts:<opts> or 0x17 t:<typeidx> opts:<opts>
        struct {
            uint32_t future_type_idx;
            struct WASMComponentCanonOpts *canon_opts;
        } future_read_write;
        // 0x18 t:<typeidx> async?:<async?> or 0x19 t:<typeidx> async?:<async?>
        struct {
            uint32_t future_type_idx;
            bool async;
        } future_cancel_read_write;
        // 0x1a t:<typeidx> or 0x1b t:<typeidx>
        struct {
            uint32_t future_type_idx;
        } future_drop_readable_writable;
        // 0x1c opts:<opts> or 0x1d opts:<opts>
        struct {
            struct WASMComponentCanonOpts *canon_opts;
        } error_context_new_debug;
        // 0x1e (no parameters)
        struct {
            // No parameters
        } error_context_drop;
        // 0x1f (no parameters)
        struct {
            // No parameters
        } waitable_set_new;
        // 0x20 cancel?:<cancel?> m:<core:memidx> or 0x21 cancel?:<cancel?>
        // m:<core:memidx>
        struct {
            bool cancellable;
            uint32_t mem_idx;
        } waitable_set_wait_poll;
        // 0x22 (no parameters)
        struct {
            // No parameters
        } waitable_set_drop;
        // 0x23 (no parameters)
        struct {
            // No parameters
        } waitable_join;
        // 0x40 ft:<typeidx>
        struct {
            uint32_t func_type_idx;
        } thread_spawn_ref;
        // 0x41 ft:<typeidx> tbl:<core:tableidx>
        struct {
            uint32_t func_type_idx;
            uint32_t table_idx;
        } thread_spawn_indirect;
        // 0x42 (no parameters)
        struct {
            // No parameters
        } thread_available_parallelism;
    } canon_data;
} WASMComponentCanon;

// Canonical section structure
typedef struct WASMComponentCanonSection {
    uint32_t count;
    WASMComponentCanon *canons;
} WASMComponentCanonSection;

// -----------------------------------------------------------------------------
// Section 9: Start Section Structs
// -----------------------------------------------------------------------------
// Start definition for component-level start function
typedef struct WASMComponentStartSection {
    uint32_t func_idx;         // Function index to call
    uint32_t value_args_count; // Number of value arguments
    uint32_t *value_args;      // Array of value indices for arguments
    uint32_t result; // Number of result values to append to value index space
} WASMComponentStartSection;

// -----------------------------------------------------------------------------
// Section 10: Import Section Structs
// -----------------------------------------------------------------------------
// Import Structure - Component import
typedef struct WASMComponentImport {
    WASMComponentImportName *import_name;
    WASMComponentExternDesc *extern_desc;
} WASMComponentImport;

// Import Section Structure - Vector of imports
typedef struct WASMComponentImportSection {
    uint32_t count;
    WASMComponentImport *imports;
} WASMComponentImportSection;

// -----------------------------------------------------------------------------
// Section 11: Export Section Structs
// -----------------------------------------------------------------------------
// Export Structure - Component export
typedef struct WASMComponentExport {
    WASMComponentExportName *export_name;
    WASMComponentSortIdx *sort_idx;
    WASMComponentExternDesc *extern_desc;
} WASMComponentExport;

// Export Section Structure - Vector of exports
typedef struct WASMComponentExportSection {
    uint32_t count;
    WASMComponentExport *exports;
} WASMComponentExportSection;

// -----------------------------------------------------------------------------
// Section 12: Values Section Structs
// -----------------------------------------------------------------------------
// Value definition for component-level values
typedef struct WASMComponentValue {
    WASMComponentValueType *val_type; // Type of the value
    uint32_t core_data_len;   // Length of the value data (len:<core:u32>)
    const uint8_t *core_data; // Binary data of the value (v:<val(t)>)
} WASMComponentValue;

// Values section structure
typedef struct WASMComponentValueSection {
    uint32_t count;
    WASMComponentValue *values;
} WASMComponentValueSection;

// -----------------------------------------------------------------------------
// Main Component Structures
// -----------------------------------------------------------------------------
// Component Section Structure - Generic section container
typedef struct WASMComponentSection {
    WASMComponentSectionType id;
    const uint8_t *payload;
    uint32_t payload_len;
    union {
        struct WASMComponentCoreCustomSection *core_custom;
        struct WASMComponentCoreTypeSection *core_type_section;
        struct WASMComponentCoreModuleWrapper *core_module;
        struct WASMComponent *component;
        struct WASMComponentAliasSection *alias_section;
        struct WASMComponentImportSection *import_section;
        struct WASMComponentExportSection *export_section;
        struct WASMComponentInstSection *instance_section;
        struct WASMComponentCoreInstSection *core_instance_section;
        struct WASMComponentCanonSection *canon_section;
        struct WASMComponentStartSection *start_section;
        struct WASMComponentValueSection *value_section;
        struct WASMComponentTypeSection *type_section;
        void *any;
    } parsed;
} WASMComponentSection;

// Main Component Structure - Complete component
typedef struct WASMComponent {
    WASMHeader header;
    WASMComponentSection *sections;
    uint32_t section_count;
#if WASM_ENABLE_LIBC_WASI != 0
    WASIArguments wasi_args;
    bool import_wasi_api;
#endif
} WASMComponent;

// -----------------------------------------------------------------------------
// Function Declarations
// -----------------------------------------------------------------------------
#ifdef __cplusplus
extern "C" {
#endif
void
set_error_buf_ex(char *error_buf, uint32_t error_buf_size, const char *format,
                 ...);

bool
parse_valtype(const uint8_t **payload, const uint8_t *end,
              WASMComponentValueType *out, char *error_buf,
              uint32_t error_buf_size);
bool
parse_labelvaltype(const uint8_t **payload, const uint8_t *end,
                   WASMComponentLabelValType *out, char *error_buf,
                   uint32_t error_buf_size);
bool
parse_case(const uint8_t **payload, const uint8_t *end,
           WASMComponentCaseValType *out, char *error_buf,
           uint32_t error_buf_size);
void
free_labelvaltype(WASMComponentLabelValType *labelvaltype);
void
free_case(WASMComponentCaseValType *case_valtype);
bool
parse_sort(const uint8_t **payload, const uint8_t *end, WASMComponentSort *out,
           char *error_buf, uint32_t error_buf_size, bool is_core);
bool
parse_sort_idx(const uint8_t **payload, const uint8_t *end,
               WASMComponentSortIdx *out, char *error_buf,
               uint32_t error_buf_size, bool is_core);
bool
parse_extern_desc(const uint8_t **payload, const uint8_t *end,
                  WASMComponentExternDesc *out, char *error_buf,
                  uint32_t error_buf_size);
void
free_extern_desc(WASMComponentExternDesc *desc);
bool
parse_core_name(const uint8_t **payload, const uint8_t *end,
                WASMComponentCoreName **out, char *error_buf,
                uint32_t error_buf_size);
void
free_core_name(WASMComponentCoreName *core_name);
bool
parse_component_import_name(const uint8_t **payload, const uint8_t *end,
                            WASMComponentImportName *out, char *error_buf,
                            uint32_t error_buf_size);
bool
parse_component_export_name(const uint8_t **payload, const uint8_t *end,
                            WASMComponentExportName *out, char *error_buf,
                            uint32_t error_buf_size);
void
free_component_import_name(WASMComponentImportName *name_struct);
void
free_component_export_name(WASMComponentExportName *name_struct);
bool
parse_label_prime(const uint8_t **payload, const uint8_t *end,
                  WASMComponentCoreName **out, char *error_buf,
                  uint32_t error_buf_size);
bool
parse_label_prime_vector(const uint8_t **payload, const uint8_t *end,
                         WASMComponentCoreName **out_labels,
                         uint32_t *out_count, char *error_buf,
                         uint32_t error_buf_size);
void
free_label_prime(WASMComponentCoreName *label);
void
free_label_prime_vector(WASMComponentCoreName *labels, uint32_t count);

// UTF-8 validation helpers for component values
// Validates that the given byte slice is well-formed UTF-8 (no overlongs, no
// surrogates, <= U+10FFFF)
bool
wasm_component_validate_utf8(const uint8_t *bytes, uint32_t len);
// Validates that the given byte slice encodes exactly one UTF-8 scalar value
bool
wasm_component_validate_single_utf8_scalar(const uint8_t *bytes, uint32_t len);

bool
parse_single_type(const uint8_t **payload, const uint8_t *end,
                  WASMComponentTypes *out, char *error_buf,
                  uint32_t error_buf_size);
bool
parse_single_alias(const uint8_t **payload, const uint8_t *end,
                   WASMComponentAliasDefinition *out, char *error_buf,
                   uint32_t error_buf_size);
bool
parse_single_core_type(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreDefType *out, char *error_buf,
                       uint32_t error_buf_size);
bool
parse_alias_target(const uint8_t **payload, const uint8_t *end,
                   WASMComponentCoreAliasTarget *out, char *error_buf,
                   uint32_t error_buf_size);
bool
parse_core_export_decl(const uint8_t **payload, const uint8_t *end,
                       WASMComponentCoreExportDecl *out, char *error_buf,
                       uint32_t error_buf_size);
bool
parse_component_decl(const uint8_t **payload, const uint8_t *end,
                     WASMComponentComponentDecl **out, char *error_buf,
                     uint32_t error_buf_size);
bool
parse_component_type(const uint8_t **payload, const uint8_t *end,
                     WASMComponentComponentType **out, char *error_buf,
                     uint32_t error_buf_size);
bool
parse_result_list(const uint8_t **payload, const uint8_t *end,
                  WASMComponentResultList **out, char *error_buf,
                  uint32_t error_buf_size);

// Core Type Parsing Functions
bool
parse_core_moduletype(const uint8_t **payload, const uint8_t *end,
                      WASMComponentCoreModuleType *out, char *error_buf,
                      uint32_t error_buf_size);
bool
parse_core_valtype(const uint8_t **payload, const uint8_t *end,
                   WASMComponentCoreValType *out, char *error_buf,
                   uint32_t error_buf_size);

bool
is_wasm_component(WASMHeader header);
bool
wasm_component_parse_sections(const uint8_t *buf, uint32_t size,
                              WASMComponent *out_component, LoadArgs *args,
                              unsigned int depth);
bool
wasm_component_parse_core_custom_section(const uint8_t **payload,
                                         uint32_t payload_len,
                                         WASMComponentCoreCustomSection *out,
                                         char *error_buf,
                                         uint32_t error_buf_size,
                                         uint32_t *consumed_len);
bool
wasm_component_parse_core_module_section(const uint8_t **payload,
                                         uint32_t payload_len,
                                         WASMComponentCoreModuleWrapper *out,
                                         LoadArgs *args, char *error_buf,
                                         uint32_t error_buf_size,
                                         uint32_t *consumed_len);
bool
wasm_component_parse_core_instance_section(const uint8_t **payload,
                                           uint32_t payload_len,
                                           WASMComponentCoreInstSection *out,
                                           char *error_buf,
                                           uint32_t error_buf_size,
                                           uint32_t *consumed_len);
bool
wasm_component_parse_core_type_section(const uint8_t **payload,
                                       uint32_t payload_len,
                                       WASMComponentCoreTypeSection *out,
                                       char *error_buf, uint32_t error_buf_size,
                                       uint32_t *consumed_len);
bool
wasm_component_parse_component_section(const uint8_t **payload,
                                       uint32_t payload_len, WASMComponent *out,
                                       char *error_buf, uint32_t error_buf_size,
                                       LoadArgs *args, unsigned int depth,
                                       uint32_t *consumed_len);
bool
wasm_component_parse_instances_section(const uint8_t **payload,
                                       uint32_t payload_len,
                                       WASMComponentInstSection *out,
                                       char *error_buf, uint32_t error_buf_size,
                                       uint32_t *consumed_len);
bool
wasm_component_parse_alias_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentAliasSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len);
bool
wasm_component_parse_types_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentTypeSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len);
bool
wasm_component_parse_canons_section(const uint8_t **payload,
                                    uint32_t payload_len,
                                    WASMComponentCanonSection *out,
                                    char *error_buf, uint32_t error_buf_size,
                                    uint32_t *consumed_len);
bool
wasm_component_parse_start_section(const uint8_t **payload,
                                   uint32_t payload_len,
                                   WASMComponentStartSection *out,
                                   char *error_buf, uint32_t error_buf_size,
                                   uint32_t *consumed_len);
bool
wasm_component_parse_imports_section(const uint8_t **payload,
                                     uint32_t payload_len,
                                     WASMComponentImportSection *out,
                                     char *error_buf, uint32_t error_buf_size,
                                     uint32_t *consumed_len);
bool
wasm_component_parse_exports_section(const uint8_t **payload,
                                     uint32_t payload_len,
                                     WASMComponentExportSection *out,
                                     char *error_buf, uint32_t error_buf_size,
                                     uint32_t *consumed_len);
bool
wasm_component_parse_values_section(const uint8_t **payload,
                                    uint32_t payload_len,
                                    WASMComponentValueSection *out,
                                    char *error_buf, uint32_t error_buf_size,
                                    uint32_t *consumed_len);

// Free functions for each section
void
wasm_component_free_start_section(WASMComponentSection *section);
void
wasm_component_free_values_section(WASMComponentSection *section);
void
wasm_component_free_core_custom_section(WASMComponentSection *section);
void
wasm_component_free_core_module_section(WASMComponentSection *section);
void
wasm_component_free_core_instance_section(WASMComponentSection *section);
void
wasm_component_free_core_type_section(WASMComponentSection *section);
void
wasm_component_free_component_section(WASMComponentSection *section);
void
wasm_component_free_instances_section(WASMComponentSection *section);
void
wasm_component_free_alias_section(WASMComponentSection *section);
void
wasm_component_free_types_section(WASMComponentSection *section);
void
wasm_component_free_canons_section(WASMComponentSection *section);
void
wasm_component_free_imports_section(WASMComponentSection *section);
void
wasm_component_free_exports_section(WASMComponentSection *section);

void
wasm_component_free(WASMComponent *component);
#ifdef __cplusplus
}
#endif

// Utility functions
static inline bool
is_primitive_type(uint8_t value)
{
    // Component-model primitive value types
    // 0x7f=bool, 0x7e=s8, 0x7d=u8, 0x7c=s16, 0x7b=u16, 0x7a=s32, 0x79=u32,
    // 0x78=s64, 0x77=u64, 0x76=f32, 0x75=f64, 0x74=char, 0x73=string,
    // 0x64=error-context
    switch (value) {
        case WASM_COMP_PRIMVAL_BOOL:
        case WASM_COMP_PRIMVAL_S8:
        case WASM_COMP_PRIMVAL_U8:
        case WASM_COMP_PRIMVAL_S16:
        case WASM_COMP_PRIMVAL_U16:
        case WASM_COMP_PRIMVAL_S32:
        case WASM_COMP_PRIMVAL_U32:
        case WASM_COMP_PRIMVAL_S64:
        case WASM_COMP_PRIMVAL_U64:
        case WASM_COMP_PRIMVAL_F32:
        case WASM_COMP_PRIMVAL_F64:
        case WASM_COMP_PRIMVAL_CHAR:
        case WASM_COMP_PRIMVAL_STRING:
        case WASM_COMP_PRIMVAL_ERROR_CONTEXT:
            return true;
        default:
            return false;
    }
}

// Core type utility functions
static inline bool
is_core_numtype(uint8_t value)
{
    // numtype ::= 0x7F => i32 | 0x7E => i64 | 0x7D => f32 | 0x7C => f64
    return (value >= WASM_CORE_NUM_TYPE_F64 && value <= WASM_CORE_NUM_TYPE_I32);
}

static inline bool
is_core_vectype(uint8_t value)
{
    // vectype ::= 0x7B => v128
    return (value == WASM_CORE_VECTOR_TYPE_V128);
}

static inline bool
is_core_reftype(uint8_t value)
{
    // reftype ::= 0x63 ht:heaptype => (ref ht) | 0x64 ht:heaptype => (ref null
    // ht) heaptype ::= ht:absheaptype => ht | x:s33 => x (if x >= 0)
    // absheaptype ::= 0x73 => nofunc | 0x72 => noextern | 0x71 => none | 0x70
    // => func
    //               | 0x6F => extern | 0x6E => any | 0x6D => eq | 0x6C => i31
    //               | 0x6B => struct | 0x6A => array
    return (value == 0x63 || value == 0x64)
           || (value >= WASM_CORE_ABS_HEAP_TYPE_ARRAY
               && value <= WASM_CORE_ABS_HEAP_TYPE_NOFUNC);
}

static inline bool
is_core_absheaptype(uint8_t value)
{
    // absheaptype ::= 0x73 => nofunc | 0x72 => noextern | 0x71 => none | 0x70
    // => func
    //               | 0x6F => extern | 0x6E => any | 0x6D => eq | 0x6C => i31
    //               | 0x6B => struct | 0x6A => array
    return (value >= WASM_CORE_ABS_HEAP_TYPE_ARRAY
            && value <= WASM_CORE_ABS_HEAP_TYPE_NOFUNC);
}

static inline bool
is_core_packedtype(uint8_t value)
{
    // packedtype ::= 0x78 => i8 | 0x77 => i16
    return (value == WASM_CORE_PACKED_TYPE_I16
            || value == WASM_CORE_PACKED_TYPE_I8);
}

static inline bool
is_core_storagetype(uint8_t value)
{
    // storagetype ::= t:valtype => t | t:packedtype => t
    return is_core_numtype(value) || is_core_vectype(value)
           || is_core_reftype(value) || is_core_packedtype(value);
}

static inline bool
is_core_comptype(uint8_t value)
{
    // comptype ::= 0x5E at:arraytype => array at
    //           | 0x5F st:structtype => struct st
    //           | 0x60 ft:functype => func ft
    return (value == WASM_CORE_COMPTYPE_ARRAY
            || value == WASM_CORE_COMPTYPE_STRUCT
            || value == WASM_CORE_COMPTYPE_FUNC);
}

// Core type parsing constants
#define CORE_TYPE_REC_GROUP_TAG 0x4E
#define CORE_TYPE_SUBTYPE_FINAL_TAG 0x4F
#define CORE_TYPE_SUBTYPE_NONFINAL_TAG 0x50
#define CORE_TYPE_MODULE_TAG 0x50
#define CORE_TYPE_REF_TAG 0x63
#define CORE_TYPE_REF_NULL_TAG 0x64

// Core type parsing enums
typedef enum WASMCoreTypeParsingTag {
    WASM_CORE_TYPE_REC_GROUP = 0x4E,
    WASM_CORE_TYPE_SUBTYPE_FINAL = 0x4F,
    WASM_CORE_TYPE_SUBTYPE_NONFINAL = 0x50,
    WASM_CORE_TYPE_MODULE = 0x50,
    WASM_CORE_TYPE_REF = 0x63,
    WASM_CORE_TYPE_REF_NULL = 0x64
} WASMCoreTypeParsingTag;

// Core type parsing helper functions
static inline bool
is_core_subtype_tag(uint8_t value)
{
    return value == WASM_CORE_TYPE_SUBTYPE_FINAL
           || value == WASM_CORE_TYPE_SUBTYPE_NONFINAL;
}

static inline bool
is_core_rectype_tag(uint8_t value)
{
    return value == WASM_CORE_TYPE_REC_GROUP;
}

static inline bool
is_core_moduletype_tag(uint8_t value)
{
    return value == WASM_CORE_TYPE_MODULE;
}

// Core type validation functions
static inline bool
is_valid_core_type_index(uint32_t index, uint32_t max_types)
{
    return index < max_types;
}

static inline bool
is_valid_core_heap_type_index(uint64_t index)
{
    // s33 validation - check if it's a valid signed 33-bit value
    return index <= 0x1FFFFFFFF; // 2^33 - 1
}

// Additional utility functions for component model
bool
is_defvaltype_tag(uint8_t byte);
WASMComponentTypesTag
get_type_tag(uint8_t first_byte);

// Additional helper functions for core type validation

// Core type memory management functions
void
free_core_resulttype(WASMComponentCoreResultType *resulttype);
void
free_core_structtype(WASMComponentCoreStructType *structtype);
void
free_core_type(WASMComponentCoreType *type);
void
free_core_type_section(WASMComponentCoreTypeSection *section);

// Additional helper functions for freeing core structures
void
free_core_import_desc(WASMComponentCoreImportDesc *import_desc);
void
free_core_import(WASMComponentCoreImport *import);
void
free_core_export_decl(WASMComponentCoreExportDecl *export_decl);
void
free_core_module_decl(WASMComponentCoreModuleDecl *module_decl);
void
free_core_moduletype(WASMComponentCoreModuleType *moduletype);
void
free_core_deftype(WASMComponentCoreDefType *deftype);
void
free_core_functype(WASMComponentCoreFuncType *functype);
void
free_core_rectype(WASMComponentCoreRecType *rectype);
void
free_core_subtype(WASMComponentCoreSubType *subtype);
void
free_core_module_subtype(WASMComponentCoreModuleSubType *module_subtype);
void
free_core_comptype(WASMComponentCoreCompType *comptype);

#endif // WASM_COMPONENT_H

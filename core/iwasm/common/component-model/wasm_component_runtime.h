/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_RUNTIME_H
#define WASM_COMPONENT_RUNTIME_H

#include "wasm.h"
#include "bh_vector.h"
#include "wasm_runtime_common.h"
#include "wasm_component.h"
#include "wasm_runtime.h"
#include "wasm_component_resource_table.h"
#include "mem_alloc.h"
#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXCEPTION_BUF_LEN
#define EXCEPTION_BUF_LEN 128
#endif

#define HEAP_SIZE (100 * 1024 * 1024) // 100 MB
#define STACK_SIZE (16 * 1024)        // 100 MB

#define MAX_REG_FLOATS 8
#define MAX_REG_INTS 6

typedef struct WASMComponentTypeInstance WASMComponentTypeInstance;
typedef struct WASMComponentInstance WASMComponentInstance;
typedef struct WASMComponentExportInstance WASMComponentExportInstance;
typedef struct WASMComponentFunctionInstance WASMComponentFunctionInstance;
typedef struct WASMMemoryInstance WASMMemoryInstance;
typedef struct WASMTableInstance WASMTableInstance;
typedef struct WASMGlobalInstance WASMGlobalInstance;
typedef struct WASMModuleInstance WASMModuleInstance;
typedef struct CanonicalOptions CanonicalOptions;
typedef struct LiftLowerContext LiftLowerContext;

typedef enum CoreExportType {
    CORE_IMPORT_KIND_FUNC = 0,
    CORE_IMPORT_KIND_TABLE = 1,
    CORE_IMPORT_KIND_MEMORY = 2,
    CORE_IMPORT_KIND_GLOBAL = 3,
} CoreExportType;

typedef enum WASMComponentValType {
    COMPONENT_VAL_TYPE_PRIMVAL = 0x63,
    COMPONENT_VAL_TYPE_RECORD = 0x72,
    COMPONENT_VAL_TYPE_VARIANT = 0x71,
    COMPONENT_VAL_TYPE_LIST = 0x70,
    COMPONENT_VAL_TYPE_TUPLE = 0x6f,
    COMPONENT_VAL_TYPE_FLAGS = 0x6e,
    COMPONENT_VAL_TYPE_ENUM = 0x6d,
    COMPONENT_VAL_TYPE_OPTION = 0x6b,
    COMPONENT_VAL_TYPE_RESULT = 0x6a,
    COMPONENT_VAL_TYPE_OWN = 0x69,
    COMPONENT_VAL_TYPE_BORROW = 0x68,
    COMPONENT_VAL_TYPE_FIXED_SIZE_LIST = 0x67,
    COMPONENT_VAL_TYPE_STREAM = 0x66,
    COMPONENT_VAL_TYPE_FUTURE = 0x65,
    COMPONENT_VAL_TYPE_ERROR_CONTEXT = 0x64,
    COMPONENT_VAL_TYPE_INSTANCE = 0x42,
    COMPONENT_VAL_TYPE_COMPONENT = 0x41,
    COMPONENT_VAL_TYPE_FUNCTION = 0x40,
    COMPONENT_VAL_TYPE_RESOURCE_SYNC = 0x3f,
    COMPONENT_VAL_TYPE_RESOURCE_ASYNC = 0x3E,
} WASMComponentValType;

// Differentiates between Normal / Host / Canonical
typedef struct WASMComponentIndexCount {
    uint32 core_functions;
    uint32 core_types;
    uint32 core_modules;
    uint32 core_instances;
    uint32 functions;
    uint32 types;
    uint32 components;
    uint32 instances;
    uint32 imports;
    uint32 exports;
    uint32 core_tables;
    uint32 core_memories;
    uint32 core_globals;
    uint32 values;
    uint64 types_total_size;
    uint32 defined_core_functions;
    uint32 defined_functions;
    uint32 defined_instances;
    uint32 defined_core_instances;
    uint32 canon_options;
    uint32 canon_options_funcs;
} WASMComponentIndexCount;

typedef struct WASMComponentInstArgInstance {
    WASMComponentCoreName *name;
    WASMComponentSortIdx *sort_idx;
    // void *sort_instance;
    union {
        WASMModule *core_module;
        WASMComponentFunctionInstance *function;
        WASMComponentValue *value;
        WASMComponentTypeInstance *type;
        WASMComponent *component;
        WASMComponentInstance *instance;
    } arg;
} WASMComponentInstArgInstance;

typedef struct WASMComponentInstArgInstances {
    uint32 arg_len;
    WASMComponentInstArgInstance *args;
    WASMComponentInstance *parent;
} WASMComponentInstArgInstances;

// ***********************************************
// Component Instance types to be used at runtime:
// ***********************************************

// Labeled value type
typedef struct WASMComponentLabelValTypeInstance {
    WASMComponentCoreName *label;
    WASMComponentTypeInstance *type;
} WASMComponentLabelValTypeInstance;

// Variant labeled case
typedef struct WASMComponentCaseValInstance {
    WASMComponentCoreName *label; // TODO: can this struct just be replaced with
                                  // WASMComponentLabelValTypeInstance??
    WASMComponentTypeInstance
        *value_type; // TODO: It should accept untyped cases ==> DONE by setting
                     // tag to COMPONENT_VAL_INSTANCE_EMPTY
} WASMComponentCaseValInstance;

// Record Type struture
// record ::= lt*:vec(<labelvaltype>) => (record lt*)
typedef struct WASMComponentRecordInstance {
    uint32_t count;
    WASMComponentLabelValTypeInstance
        fields[]; // Instantiated as an array of size 'count'
} WASMComponentRecordInstance;

// Variant Type Structure - Labeled cases
// variant ::= case*:vec(<case>) => (variant case*)
typedef struct WASMComponentVariantInstance {
    uint32_t count;
    WASMComponentCaseValInstance
        cases[]; // Instantiated as an array of size 'count'
} WASMComponentVariantInstance;

// List Type Structure - Homogeneous list
// list ::= t:<valtype> => (list t)
typedef struct WASMComponentListInstance {
    WASMComponentTypeInstance *element_type;
} WASMComponentListInstance;

// List Length Type Structure - Fixed-length list
typedef struct WASMComponentListLenInstance {
    uint32_t len;
    WASMComponentTypeInstance *element_type;
} WASMComponentListLenInstance;

// Tuple Type Structure - Heterogeneous tuple
// tuple ::= t*:vec(<valtype>) => (tuple t*)
typedef struct WASMComponentTupleInstance {
    uint32_t count;
    WASMComponentTypeInstance *
        *element_types; // Array of pointers to type instances
} WASMComponentTupleInstance;

// Option Type Structure - Optional value
// option ::= t:<valtype> => (option t)
typedef struct WASMComponentOptionInstance {
    WASMComponentTypeInstance *element_type;
} WASMComponentOptionInstance;

// Result Type Structure - Success/error result
// result ::= t?:<valtype>? u?:<valtype>? => (result t? u?)
typedef struct WASMComponentResultInstance {
    WASMComponentTypeInstance *result_type; // Optional (can be NULL)
    WASMComponentTypeInstance *error_type;  // Optional (can be NULL)
} WASMComponentResultInstance;

// Function parameters
typedef struct WASMComponentParamListInstance {
    uint32 count;
    WASMComponentLabelValTypeInstance
        params[]; // Instantiated as an array of size 'count'
} WASMComponentParamListInstance;

// Function return value
typedef struct WASMComponentResultListInstance {
    WASMComponentResultListTag tag;
    uint32 count;
    WASMComponentTypeInstance *result;
} WASMComponentResultListInstance;

typedef struct WASMComponentFuncTypeInstance {
    WASMComponentParamListInstance *params;
    WASMComponentResultListInstance *results;
} WASMComponentFuncTypeInstance;

typedef struct WASMComponentCanonOptInstance {
    WASMComponentCanonOptTag tag;
    union {
        WASMMemoryInstance *memory;             /* 0x03 */
        WASMFunctionInstance *realloc_func;     /* 0x04 */
        WASMFunctionInstance *post_return_func; /* 0x05 */
        WASMFunctionInstance *callback_func;    /* 0x07 */
    } payload;
} WASMComponentCanonOptInstance;

typedef struct WASMComponentCanonOptsInstance {
    uint32_t canon_opts_count;
    WASMComponentCanonType type;
    WASMComponentCanonOptInstance *canon_opts;
} WASMComponentCanonOptsInstance;

// Function instance structure
typedef struct WASMComponentFunctionInstance {
    WASMComponentFuncTypeInstance *func_type;
    WASMFunctionInstance *core_func;
    CanonicalOptions *canon_options;
} WASMComponentFunctionInstance;

// Used for storing the sizes of the defined types/ exports/ index spaces of the
// Component instance type
typedef struct WASMComponentInstanceDeclTypeSize {
    uint32 types_size;
    uint32 types_count;
    uint32 func_count;
    uint32 exports_count;
    uint32 resource_count;
} WASMComponentInstanceDeclTypeSize;

// Structure for defining component instance types (used for importing component
// instances)
typedef struct WASMComponentInstTypeInstance {

    uint32 types_count;
    WASMComponentTypeInstance **types;

    uint32 func_count;
    WASMComponentFuncTypeInstance **funcs;

    uint32 exports_count;
    WASMComponentExportInstance *exports;

    WASMFunctionInstance *defined_core_funcs;

    // Memory layout at instantiation:
    /*
        +-------------------------------------------+
        | WASMComponentInstTypeInstance             | ==> will hold pointers to
       the vectors defined below
        +-------------------------------------------+
        | WASMComponentTypeInstance[types_count]    | ==> holds pointer to the
       Defined Types implementations (or outer types)
        +-------------------------------------------+
        | WASMComponentFuncTypeInstance[func_count] |
        +-------------------------------------------+
        | WASMComponentExport[exports_count]        |
        +-------------------------------------------+
        | -- Defined Types --                       |
        +-------------------------------------------+
    */

} WASMComponentInstTypeInstance;

typedef struct WASMComponentResourceInstance {
    char *name;
    char *interface_name;
    bool is_wasi;
    WASMComponentInstance *impl;
    WASMFunctionInstance *drop_method;
    WASMFunctionInstance *new_method;
    WASMFunctionInstance *rep_method;
    WASMFunctionInstance *dtor_method;
    WASMFunctionInstance *ctor_method;
} WASMComponentResourceInstance;

typedef struct WASMComponentResourceHandleInstance {
    WASMComponentResourceInstance *resource;
    bool is_borrow;
} WASMComponentResourceHandleInstance;

// Structure for the runtime defined types ==> Used to populate the component's
// Type index space
typedef struct WASMComponentTypeInstance {
    WASMComponentValType type;
    uint32_t alignment;
    uint32_t elem_size;
    union {
        WASMComponentPrimValType primval;
        WASMComponentRecordInstance *record;
        WASMComponentVariantInstance *variant;
        WASMComponentListInstance *list;
        WASMComponentListLenInstance *list_len;
        WASMComponentTupleInstance *tuple;
        WASMComponentFlagType *flag;
        WASMComponentEnumType *enum_type;
        WASMComponentOptionInstance *option;
        WASMComponentResultInstance *result;
        WASMComponentFuncTypeInstance *function;
        WASMComponentInstTypeInstance *instance;
        WASMComponentResourceInstance *resource;
        WASMComponentResourceHandleInstance *resource_handle;
        // WASMComponent .......... TODO: i have found no examples in test wasm
        // files ==> how would that differ from a normal inner component?

    } type_specific;
} WASMComponentTypeInstance;

// Each component will save all types defined inside it after the component
// structure definition The component's index space will hold pointers to type
// implementations (defined in the current component OR a different one)

/*
    +------------------------------+
    | WASMComponentInstance        | ==> will contain pointers to type instances
   + a WASMComponentValType
    +------------------------------+
    | vec <type instance> [size]   | ==> 'size' = cummulative size of all
   defined type instances
    +------------------------------+
*/

// *****************************
// Import and export definitions
// *****************************
typedef struct WASMCoreExport {
    CoreExportType kind;
    union {
        WASMFunctionInstance *func_instance;
        WASMTableInstance *table_instance;
        WASMMemoryInstance *mem_instance;
        WASMGlobalInstance *global_instance;
    } exp;
} WASMCoreExport;

typedef struct WASMCoreImports {
    uint32 func_count;
    uint32 mem_count;
    uint32 tables_count;
    uint32 globals_count;
    WASMFunctionInstance **func_instance;
    WASMTableInstance **table_instance;
    WASMMemoryInstance **mem_instance;
    WASMGlobalInstance **global_instance;
} WASMCoreImports;

typedef struct WASMComponentExportInstance {
    WASMComponentExportName *export_name;
    WASMComponentExternDescType type;
    union {
        WASMModule *core_module;
        WASMComponentFunctionInstance *function;
        WASMComponentFuncTypeInstance
            *func_type; // will be used only for exports of Componet Instance
                        // type definition, as core implementation part is not
                        // available
        WASMComponentTypeInstance *type;
        WASMComponent *component;
        WASMComponentInstance *instance;
        WASMComponentValue *value;
    } exp;
} WASMComponentExportInstance;

// Component runtime instance structure
typedef struct WASMComponentInstance {
    char cur_exception[EXCEPTION_BUF_LEN];

    WASMExecEnv *exec_env_singleton; // Single exec env per component
    WASMExecEnv *cur_exec_env;       // Currently active exec env
    uint32 default_wasm_stack_size;  // Stack configuration
    void *custom_data;
    WASMComponent *component; // Reference to component definition

    WASMComponentResourceTable *table;
#if WASM_ENABLE_LIBC_WASI != 0
    WASIContext *wasi_ctx;
#endif
    // Index spaces:

    // 1 - Component functions
    WASMComponentFunctionInstance **functions;
    uint32 functions_count;

    // 2 - Component values
    WASMComponentValue **values;
    uint32 values_count;

    // 3 - Component types
    WASMComponentTypeInstance **types;
    uint32 types_count;

    // 4 - Component instances
    struct WASMComponentInstance **component_instances;
    uint32 component_instances_count;

    // 5 - Components
    WASMComponent **components;
    uint32 components_count;

    // 6 - Core functions
    WASMFunctionInstance **core_functions;
    uint32 core_functions_count;

    // 7 - Core tables
    WASMTableInstance **core_tables;
    uint32 core_tables_count;

    // 8 - Core memories
    WASMMemoryInstance **core_memories;
    uint32 core_memories_count;

    // 9  Core globals
    WASMGlobalInstance **core_globals;
    uint32 core_globals_count;

    // 10 - Core types
    WASMType **core_types;
    uint32 core_types_count;

    // 11 - Core module instances
    WASMModuleInstance **core_module_instances;
    uint32 core_module_instances_count;

    // 12 - Core modules
    WASMModule **core_modules;
    uint32 core_modules_count;

    //  ***** Helper fields neede for instantiation
    struct WASMComponentInstance *parent;

    WASMComponentExportInstance *exports;
    uint32 exports_count;

    void *defined_types;
    WASMFunctionInstance *defined_core_functions;
    WASMComponentFunctionInstance *defined_functions;

    struct WASMComponentInstance **defined_instances;
    uint32 defined_instances_count;

    WASMModuleInstance **defined_core_instances;
    uint32 defined_core_instances_count;

    uint32 defined_types_size;
    uint32 defined_core_functions_count;
    uint32 defined_functions_count;
    uint32 defined_types_idx;

    void *defined_canon_opts;
    uint32 defined_canon_opts_size;

    uint32 resources_count;
    bool may_leave;

    union {
        uint64 _make_it_8_byte_aligned_;
        uint8 bytes[1];
    } component_data;

} WASMComponentInstance;

typedef struct WASMComponentAliasTarget {
    // bool is_core;
    // bool ref_by_name;
    WASMComponentAliasTargetType type;
    WASMComponentSort *sort;
    union {
        WASMComponentCoreName *name;
        uint32 idx;
    } ref;
    union {
        WASMModuleInstance *core_instance;
        WASMComponentInstance *instance;
    } target;
} WASMComponentAliasTarget;

WASMComponentIndexCount *
wasm_component_get_index_count(WASMComponent *component, char *error_buf,
                               uint32 error_buf_size);

WASMComponentInstance *
wasm_component_instance_allocate(WASMComponentIndexCount *index_count,
                                 char *error_buf, uint32 error_buf_size);

uint32
wasm_get_inst_decl_size(WASMComponentInstType *instance_type,
                        WASMComponentInstanceDeclTypeSize *instance_type_size);

bool
wasm_resolve_types(WASMComponentTypeSection *type_section,
                   WASMComponentInstance *comp_instance, char *error_buf,
                   uint32 error_buf_size);

bool
wasm_resolve_exports(WASMComponentExportSection *export_section,
                     WASMComponentInstance *comp_instance, char *error_buf,
                     uint32 error_buf_size);

bool
wasm_resolve_imports(WASMComponentImportSection *import_section,
                     WASMComponentInstance *comp_instance,
                     WASMComponentInstArgInstances *instance_expression,
                     char *error_buf, uint32 error_buf_size);

bool
wasm_resolve_imports_WASI(WASMComponentImportSection *import_section,
                          WASMComponentInstance *comp_instance, char *error_buf,
                          uint32 error_buf_size);

bool
wasm_resolve_alias(WASMComponentAliasSection *alias_section,
                   WASMComponentInstance *comp_instance, char *error_buf,
                   uint32 error_buf_size);

void *
get_alias(WASMComponentAliasTarget *target, char *error_buf,
          uint32 error_buf_size);

bool
wasm_resolve_instance(struct WASMComponentInstSection *instance_section,
                      WASMComponentInstance *comp_instance, char *error_buf,
                      uint32 error_buf_size);

bool
wasm_resolve_core_instance(WASMComponentCoreInstSection *instance_section,
                           WASMComponentInstance *comp_instance,
                           char *error_buf, uint32 error_buf_size);

bool
wasm_resolve_core_imports(WASMInstExpr *expression, WASMModule *target,
                          WASMComponentInstance *comp_instance,
                          WASMCoreImports *found_imports, char *error_buf,
                          uint32 error_buf_size);

bool
wasm_create_core_inst_from_expression(WASMComponentCoreInst *core_inst,
                                      WASMComponentInstance *comp_instance,
                                      char *error_buf, uint32 error_buf_size);

uint32
wasm_get_func_type_size(WASMComponentFuncType *func_type);

bool
wasm_resolve_canon(WASMComponentCanonSection *canon_section,
                   WASMComponentInstance *comp_instance, char *error_buf,
                   uint32 error_buf_size);

bool
wasm_component_application_execute_main(WASMComponentInstance *, int32 argc,
                                        char *argv[]);
bool
wasm_component_application_execute_func(WASMComponentInstance *, char *argv);
bool
wasm_component_application_execute_func_ex(WASMComponentInstance *, char *argv,
                                           uint32 *argc1, uint32 **argv1);

uint32_t
align_to(uint32_t ptr, uint32_t alignment);
uint32_t
compute_discriminant_alignment(uint32_t num_cases);
uint32_t
compute_alignment_primitive_value(WASMComponentPrimValType primval);
uint32_t
compute_alignment(WASMComponentTypeInstance *type);
uint32_t
compute_elem_size_primitive_value(WASMComponentPrimValType primval);
uint32_t
compute_elem_size(WASMComponentTypeInstance *type);
uint32_t
compute_max_case_alignment(WASMComponentVariantInstance *type);

WASMComponentFunctionInstance *
wasm_component_lookup_function(const WASMComponentInstance *component_inst,
                               const char *name);

#if WASM_ENABLE_LIBC_WASI != 0

bool
wasm_component_runtime_init_wasi(
    WASMComponentInstance *comp_instance, const char *dir_list[],
    uint32 dir_count, const char *map_dir_list[], uint32 map_dir_count,
    const char *env[], uint32 env_count, const char *addr_pool[],
    uint32 addr_pool_size, const char *ns_lookup_pool[],
    uint32 ns_lookup_pool_size, char *argv[], uint32 argc,
    os_raw_file_handle stdinfd, os_raw_file_handle stdoutfd,
    os_raw_file_handle stderrfd, char *error_buf, uint32 error_buf_size);

void
wasm_component_runtime_set_wasi_args(WASMComponent *component,
                                     const char *dir_list[], uint32 dir_count,
                                     const char *map_dir_list[],
                                     uint32 map_dir_count,
                                     const char *env_list[], uint32 env_count,
                                     char *argv[], int argc);

void
wasm_component_runtime_set_wasi_options(WASMComponent *component,
                                        libc_wasi_options_t *wasi_options);

void
wasm_component_runtime_set_wasi_addr_pool(WASMComponent *component,
                                          const char *addr_pool[],
                                          uint32 addr_pool_size);

void
wasm_component_runtime_set_wasi_ns_lookup_pool(WASMComponent *component,
                                               const char *ns_lookup_pool[],
                                               uint32 ns_lookup_pool_size);

WASMComponentFunctionInstance *
wasm_component_runtime_lookup_wasi_start_function(
    WASMComponentInstance *component_inst);
#endif
WASMComponentInstance *
wasm_component_instantiate_internal(
    WASMComponent *component,
    WASMComponentInstArgInstances *instance_expression, char *error_buf,
    uint32 error_buf_size);

WASMComponentInstance *
wasm_component_instantiate(WASMComponent *component, char *error_buf,
                           uint32 error_buf_size);

void
wasm_component_deinstantiate(WASMComponentInstance *comp_instance);

void *
wasm_runtime_addr_app_to_native_p2(WASMExecEnv *exec_env, uint64 app_offset);

uint64
wasm_runtime_addr_native_to_app_p2(WASMExecEnv *exec_env, void *native_ptr);

bool
wasm_runtime_invoke_native_p2(WASMExecEnv *exec_env,
                              WASMFunctionInstance *func_instance, uint32 *argv,
                              uint32 argc, uint32 *argv_ret);

WASMComponentFuncTypeInstance *
wasm_get_component_func_type(WASMExecEnv *exec_env);

uint64
wasm_module_malloc_p2(WASMExecEnv *exec_env, uint64 size, void **p_native_addr);

bool
wasm_runtime_validate_app_addr_p2(wasm_exec_env_t exec_env, uint64 app_offset,
                                  uint64 size);

uint32_t
wasm_runtime_call_realloc(LiftLowerContext *cx, int32_t old_ptr,
                          int32_t old_size, int32_t align, int32_t new_size);

#ifdef __cplusplus
}
#endif

#endif /* end of WASM_COMPONENT_RUNTIME_H */

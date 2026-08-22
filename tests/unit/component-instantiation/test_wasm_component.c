/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "test_wasm_component.h"
#include "wasm.h"

// =============================================================================
// Mock Data and Forward Declarations
// =============================================================================

// =============================================================================
// Section 0: Core Custom Section
// =============================================================================
static const uint8_t custom_section_data[] = { 2, 2, 2, 2 };
static WASMComponentCoreCustomSection core_custom_section_instance = {
    .name = "test",
    .data = custom_section_data,
    .data_len = sizeof(custom_section_data)
};

// =============================================================================
// Section 1: Core Module Section
// =============================================================================
static WASMModule mock_module = { .module_type = 2,
                                  .import_count = 0,
                                  .function_count = 0 };
static WASMComponentCoreModuleWrapper core_module_wrapper_instance = {
    .module = &mock_module,
    .module_handle = NULL
};

// =============================================================================
// Section 2: Core Instance Section
// =============================================================================
static WASMComponentCoreName core_instance_export_name_inst = {
    .name_len = 4,
    .name = "test module"
};

static WASMComponentCoreName core_instance_export_names[] = {
    { .name = "test func" },
    { .name = "test table" },
    { .name = "test memory" },
    { .name = "test global" }
};

static WASMComponentSort core_instance_sorts[] = {
    { .sort = WASM_COMP_SORT_CORE_SORT, .core_sort = WASM_COMP_CORE_SORT_FUNC },
    { .sort = WASM_COMP_SORT_CORE_SORT,
      .core_sort = WASM_COMP_CORE_SORT_TABLE },
    { .sort = WASM_COMP_SORT_CORE_SORT,
      .core_sort = WASM_COMP_CORE_SORT_MEMORY },
    { .sort = WASM_COMP_SORT_CORE_SORT,
      .core_sort = WASM_COMP_CORE_SORT_GLOBAL }
};

static WASMComponentSortIdx core_instance_sort_idxs[] = {
    { .sort = &core_instance_sorts[0], .idx = 0 },
    { .sort = &core_instance_sorts[1], .idx = 0 },
    { .sort = &core_instance_sorts[2], .idx = 0 },
    { .sort = &core_instance_sorts[3], .idx = 0 },
};

static WASMComponentInlineExport core_instance_inline_export_inst = {
    .name = &core_instance_export_name_inst,
    .sort_idx = &core_instance_sort_idxs[0]
};
static WASMComponentInlineExport core_instance_inline_exports[] = {
    { .name = &core_instance_export_names[0],
      .sort_idx = &core_instance_sort_idxs[0] },
    { .name = &core_instance_export_names[1],
      .sort_idx = &core_instance_sort_idxs[1] },
    { .name = &core_instance_export_names[2],
      .sort_idx = &core_instance_sort_idxs[2] },
    { .name = &core_instance_export_names[3],
      .sort_idx = &core_instance_sort_idxs[3] },
};
static WASMComponentCoreName core_instance_arg_name_inst = {
    .name_len = 4,
    .name = "test module"
};
static WASMComponentInstArg core_instance_arg_inst = {
    .name = &core_instance_arg_name_inst,
    .idx.instance_idx = 0
};

static WASMComponentCoreInst core_instances_inst[] = {
    { .instance_expression_tag = WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS,
      .expression.with_args = { .idx = 0,
                                .arg_len = 1,
                                .args = &core_instance_arg_inst } },
    { .instance_expression_tag = WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS,
      .expression.without_args = { .inline_expr_len =
                                       sizeof(core_instance_inline_exports)
                                       / sizeof(WASMComponentInlineExport),
                                   .inline_expr =
                                       core_instance_inline_exports } }
};
static WASMComponentCoreInstSection core_instance_section_instance = {
    .count = sizeof(core_instances_inst) / sizeof(WASMComponentCoreInst),
    .instances = core_instances_inst
};

// =============================================================================
// Section 3: Core Type Section
// =============================================================================
static WASMComponentCoreType
    core_type_def_inst = { .deftype = &(WASMComponentCoreDefType){
                               .tag = WASM_CORE_DEFTYPE_RECTYPE,
                               .type.rectype = &(WASMComponentCoreRecType){
                                   .subtype_count = 1,
                                   .subtypes = &(WASMComponentCoreSubType){
                                       .is_final = true,
                                       .supertype_count = 0,
                                       .supertypes = NULL,
                                       .comptype = {
                                           .tag = WASM_CORE_COMPTYPE_FUNC,
                                           .type.func_type = {
                                               .params = { .count = 0,
                                                           .val_types = NULL },
                                               .results = {
                                                   .count = 0,
                                                   .val_types =
                                                       NULL } } } } } } };
static WASMComponentCoreTypeSection core_type_section_instance = {
    .count = 1,
    .types = &core_type_def_inst
};

// =============================================================================
// Section 4: Component Section
// =============================================================================
static WASMComponent nested_component_instance = { .section_count = 0,
                                                   .sections = NULL };

// =============================================================================
// Section 5: Instances Section
// =============================================================================
static WASMComponentInst instances_def_inst[] = {
    { .instance_expression_tag = WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS,
      .expression.with_args = { .idx = 0,
                                .arg_len = 1,
                                .args = &core_instance_arg_inst } },
    { .instance_expression_tag = WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS,
      .expression.without_args = { .inline_expr_len = 1,
                                   .inline_expr =
                                       &core_instance_inline_export_inst } }
};
static WASMComponentInstSection instance_section_instance = {
    .count = 2,
    .instances = instances_def_inst
};

// =============================================================================
// Section 6: Alias Section
// =============================================================================
static WASMComponentCoreName alias_name_inst = { .name_len = 4,
                                                 .name = "test" };
static WASMComponentCoreName alias_name_type = { .name_len = 4,
                                                 .name = "test type" };
static WASMComponentCoreName alias_name_func = { .name_len = 4,
                                                 .name = "test func" };

static WASMComponentCoreName alias_name_core_func = { .name_len = 4,
                                                      .name =
                                                          "test core func" };
static WASMComponentCoreName alias_name_core_memory = {
    .name_len = 4,
    .name = "test core memory"
};
static WASMComponentCoreName alias_name_core_global = {
    .name_len = 4,
    .name = "test core global"
};
static WASMComponentCoreName alias_name_core_table = { .name_len = 4,
                                                       .name =
                                                           "test core table" };

static WASMComponentSort sort_func = { .sort = WASM_COMP_SORT_FUNC };
static WASMComponentSort sort_value = { .sort = WASM_COMP_SORT_VALUE };
static WASMComponentSort sort_type = { .sort = WASM_COMP_SORT_TYPE };
static WASMComponentSort sort_component = { .sort = WASM_COMP_SORT_COMPONENT };
static WASMComponentSort sort_instance = { .sort = WASM_COMP_SORT_INSTANCE };
static WASMComponentSort sort_core_func = { .sort = 0,
                                            .core_sort =
                                                WASM_COMP_CORE_SORT_FUNC };
static WASMComponentSort sort_core_table = { .sort = 0,
                                             .core_sort =
                                                 WASM_COMP_CORE_SORT_TABLE };
static WASMComponentSort sort_core_memory = { .sort = 0,
                                              .core_sort =
                                                  WASM_COMP_CORE_SORT_MEMORY };
static WASMComponentSort sort_core_global = { .sort = 0,
                                              .core_sort =
                                                  WASM_COMP_CORE_SORT_GLOBAL };
static WASMComponentSort sort_core_type = { .sort = 0,
                                            .core_sort =
                                                WASM_COMP_CORE_SORT_TYPE };
static WASMComponentSort sort_core_module = { .sort = 0,
                                              .core_sort =
                                                  WASM_COMP_CORE_SORT_MODULE };
static WASMComponentSort sort_core_instance = {
    .sort = 0,
    .core_sort = WASM_COMP_CORE_SORT_INSTANCE
};

static WASMComponentAliasDefinition aliases_def_inst[] = {
    { .sort = &sort_func,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 2,
                           .name = &alias_name_inst } }, // 0 - export func
    { .sort = &sort_value,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 2,
                           .name = &alias_name_inst } }, // 1 - export value
    { .sort = &sort_type,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_OUTER,
      .target.outer = { .ct = 1, .idx = 10 } }, // 2 - outer type
    { .sort = &sort_component,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 2,
                           .name = &alias_name_inst } }, // 3 - export component
    { .sort = &sort_instance,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 2,
                           .name = &alias_name_inst } }, // 4 - export instance
    { .sort = &sort_core_func,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 5 - export core
                                                              // func
    { .sort = &sort_core_table,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 6 - export core
                                                              // table
    { .sort = &sort_core_memory,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 7 - export core
                                                              // memory
    { .sort = &sort_core_global,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 8 - export core
                                                              // global
    { .sort = &sort_core_type,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 9 - export core
                                                              // type
    { .sort = &sort_core_module,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 10 - export
                                                              // core module
    { .sort = &sort_core_instance,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 2,
                                .name = &alias_name_inst } }, // 11 - export
                                                              // core instance
};
static WASMComponentAliasSection alias_section_instance = {
    .count = sizeof(aliases_def_inst) / sizeof(WASMComponentAliasDefinition),
    .aliases = aliases_def_inst
};

static WASMComponentExportName alias_export_names[] = {
    { .tag = WASM_COMP_IMPORTNAME_SIMPLE,
      .exported.simple.name = &alias_name_type },
    { .tag = WASM_COMP_IMPORTNAME_SIMPLE,
      .exported.simple.name = &alias_name_func },
};

WASMComponentExportInstance alias_exports[] = {
    { .export_name = &alias_export_names[0], .type = WASM_COMP_EXTERN_TYPE },
    { .export_name = &alias_export_names[1], .type = WASM_COMP_EXTERN_FUNC }
};

static WASMComponentAliasDefinition aliases_def_inst_2[] = {
    { .sort = &sort_component,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_OUTER,
      .target.outer = { .ct = 1, .idx = 0 } }, // 0 - outer component
    { .sort = &sort_instance,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_OUTER,
      .target.outer = { .ct = 1, .idx = 0 } }, // 1 - outer component instance
    { .sort = &sort_func,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 0,
                           .name = &alias_name_func } }, // 2 - export func
    { .sort = &sort_type,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_EXPORT,
      .target.exported = { .instance_idx = 0,
                           .name = &alias_name_type } }, // 3 - export type
    { .sort = &sort_core_func,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 0,
                                .name = &alias_name_core_func } }, // 4 - export
                                                                   // core type
    { .sort = &sort_core_global,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 0,
                                .name =
                                    &alias_name_core_global } }, // 5 - export
                                                                 // core global
    { .sort = &sort_core_table,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 0,
                                .name =
                                    &alias_name_core_table } }, // 6 - export
                                                                // core table
    { .sort = &sort_core_memory,
      .alias_target_type = WASM_COMP_ALIAS_TARGET_CORE_EXPORT,
      .target.core_exported = { .instance_idx = 0,
                                .name =
                                    &alias_name_core_memory } }, // 7 - export
                                                                 // core memory
};
static WASMComponentAliasSection alias_section_instance_2 = {
    .count = sizeof(aliases_def_inst_2) / sizeof(WASMComponentAliasDefinition),
    .aliases = aliases_def_inst_2
};

// =============================================================================
// Section 7: Types Section
// =============================================================================
static WASMComponentCoreName type_label_inst = { .name_len = 4,
                                                 .name = "test" };
static WASMComponentCoreName type_names_list_inst[] = {
    { .name_len = 5, .name = "test1" },
    { .name_len = 5, .name = "test2" },
};
static WASMComponentValueType val_type_prim_inst = {
    .type = WASM_COMP_VAL_TYPE_PRIMVAL,
    .type_specific.primval_type = WASM_COMP_PRIMVAL_S32
};
static WASMComponentValueType val_type_list_inst[] = {
    { .type = WASM_COMP_VAL_TYPE_PRIMVAL,
      .type_specific.primval_type = WASM_COMP_PRIMVAL_S32 },
    { .type = WASM_COMP_VAL_TYPE_PRIMVAL,
      .type_specific.primval_type = WASM_COMP_PRIMVAL_S32 },
    { .type = WASM_COMP_VAL_TYPE_PRIMVAL,
      .type_specific.primval_type = WASM_COMP_PRIMVAL_S32 }
};
static WASMComponentValueType val_type_indexed_inst = {
    .type = WASM_COMP_VAL_TYPE_IDX,
    .type_specific.type_idx = 0
};
static WASMComponentLabelValType label_val_type_inst[] = {
    { .label = &type_label_inst, .value_type = &val_type_prim_inst },
    { .label = &type_label_inst, .value_type = &val_type_indexed_inst }
};
static WASMComponentCaseValType case_val_type_inst[] = {
    { .label = &type_label_inst, .value_type = &val_type_prim_inst },
    { .label = &type_label_inst, .value_type = &val_type_indexed_inst }
};
static WASMComponentRecordType record_type_inst = {
    .count = sizeof(label_val_type_inst) / sizeof(WASMComponentLabelValType),
    .fields = label_val_type_inst
};
static WASMComponentVariantType variant_type_inst = {
    .count = sizeof(case_val_type_inst) / sizeof(WASMComponentCaseValType),
    .cases = case_val_type_inst
};
static WASMComponentListType list_type_inst = { .element_type =
                                                    &val_type_prim_inst };
static WASMComponentListLenType list_len_type_inst = {
    .len = 2,
    .element_type = &val_type_prim_inst
};
static WASMComponentTupleType tuple_type_inst = {
    .count = sizeof(val_type_list_inst) / sizeof(WASMComponentValueType),
    .element_types = val_type_list_inst
};
static WASMComponentFlagType flag_type_inst = {
    .count = sizeof(type_names_list_inst) / sizeof(WASMComponentCoreName),
    .flags = type_names_list_inst
};
static WASMComponentEnumType enum_type_inst = {
    .count = sizeof(type_names_list_inst) / sizeof(WASMComponentCoreName),
    .labels = type_names_list_inst
};
static WASMComponentOptionType option_type_inst = { .element_type =
                                                        &val_type_prim_inst };
static WASMComponentResultType result_type_inst = { .result_type =
                                                        &val_type_prim_inst,
                                                    .error_type =
                                                        &val_type_prim_inst };
static WASMComponentOwnType own_type_inst = { .type_idx = 2 };
static WASMComponentBorrowType borrow_type_inst = { .type_idx = 2 };
static WASMComponentStreamType stream_type_inst = { .element_type =
                                                        &val_type_prim_inst };
static WASMComponentFutureType future_type_inst = { .element_type =
                                                        &val_type_prim_inst };

static WASMComponentParamList param_list_inst = {
    .count = sizeof(label_val_type_inst) / sizeof(WASMComponentLabelValType),
    .params = label_val_type_inst
};
static WASMComponentResultList result_list_inst = {
    .tag = WASM_COMP_RESULT_LIST_WITH_TYPE,
    .results = &val_type_prim_inst
};
static WASMComponentFuncType func_type_inst = { .params = &param_list_inst,
                                                .results = &result_list_inst };
// Component instance definition types

static WASMComponentTypeBound type_bound_eq = { .tag = WASM_COMP_TYPEBOUND_EQ,
                                                .type_idx = 2 };
static WASMComponentTypeBound type_bound_type = { .tag =
                                                      WASM_COMP_TYPEBOUND_TYPE,
                                                  .type_idx = 2 };

static WASMComponentDefValType def_types_in_instance[] = {
    { .tag = WASM_COMP_DEF_VAL_PRIMVAL,
      .def_val.primval = WASM_COMP_PRIMVAL_BOOL },
    { .tag = WASM_COMP_DEF_VAL_RECORD, .def_val.record = &record_type_inst },
};

static WASMComponentExternDesc export_desc_in_instance[] = {
    { .type = WASM_COMP_EXTERN_FUNC, .extern_desc.func.type_idx = 7 },
    { .type = WASM_COMP_EXTERN_TYPE,
      .extern_desc.type.type_bound = &type_bound_eq }
};

static WASMComponentCoreName export_name_simple_in_inst = { .name_len = 4,
                                                            .name = "test" };
static WASMComponentExportName export_name_in_inst = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .exported.simple.name = &export_name_simple_in_inst
};

static WASMComponentComponentDeclExport exports_decl_in_instance[] = {
    { .export_name = &export_name_in_inst,
      .extern_desc = &export_desc_in_instance[0] },
    { .export_name = &export_name_in_inst,
      .extern_desc = &export_desc_in_instance[1] }
};

static WASMComponentTypes types_in_inst[] = {
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_types_in_instance[0] },
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_types_in_instance[1] },
    { .tag = WASM_COMP_FUNC_TYPE, .type.func_type = &func_type_inst },
};

static WASMComponentInstDecl instance_decl[] = {
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[1] }, // record
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS,
      .decl.alias = &aliases_def_inst[0] },
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS,
      .decl.alias = &aliases_def_inst[2] },
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL,
      .decl.export_decl = &exports_decl_in_instance[0] },
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL,
      .decl.export_decl = &exports_decl_in_instance[1] },
};

static WASMComponentInstDecl instance_decl_2[] = {
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[1] }, // record
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[1] }, // record
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS,
      .decl.alias = &aliases_def_inst[2] }, // primval
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE,
      .decl.type = &types_in_inst[0] }, // primval

    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL,
      .decl.export_decl = &exports_decl_in_instance[1] }, // variant ??
    { .tag = WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL,
      .decl.export_decl = &exports_decl_in_instance[0] },
};

static WASMComponentInstType instance_type_inst = {
    .count = sizeof(instance_decl) / sizeof(WASMComponentInstDecl),
    .instance_decls = instance_decl
};

static WASMComponentInstType instance_type_inst_2 = {
    .count = sizeof(instance_decl_2) / sizeof(WASMComponentInstDecl),
    .instance_decls = instance_decl_2
};

static WASMComponentDefValType def_val_types_inst[] = {
    { .tag = WASM_COMP_DEF_VAL_PRIMVAL,
      .def_val.primval = WASM_COMP_PRIMVAL_BOOL },
    { .tag = WASM_COMP_DEF_VAL_RECORD, .def_val.record = &record_type_inst },
    { .tag = WASM_COMP_DEF_VAL_VARIANT, .def_val.variant = &variant_type_inst },
    { .tag = WASM_COMP_DEF_VAL_LIST, .def_val.list = &list_type_inst },
    { .tag = WASM_COMP_DEF_VAL_LIST_LEN,
      .def_val.list_len = &list_len_type_inst },
    { .tag = WASM_COMP_DEF_VAL_TUPLE, .def_val.tuple = &tuple_type_inst },
    { .tag = WASM_COMP_DEF_VAL_FLAGS, .def_val.flag = &flag_type_inst },
    { .tag = WASM_COMP_DEF_VAL_ENUM, .def_val.enum_type = &enum_type_inst },
    { .tag = WASM_COMP_DEF_VAL_OPTION, .def_val.option = &option_type_inst },
    { .tag = WASM_COMP_DEF_VAL_RESULT, .def_val.result = &result_type_inst },
    { .tag = WASM_COMP_DEF_VAL_OWN, .def_val.owned = &own_type_inst },
    { .tag = WASM_COMP_DEF_VAL_BORROW, .def_val.borrow = &borrow_type_inst },
    { .tag = WASM_COMP_DEF_VAL_STREAM, .def_val.stream = &stream_type_inst },
    { .tag = WASM_COMP_DEF_VAL_FUTURE, .def_val.future = &future_type_inst },
};

static WASMComponentTypes types_def_inst[] = {
    { .tag = WASM_COMP_FUNC_TYPE, .type.func_type = &func_type_inst },
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[0] }, // primeval
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[1] }, // record
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[2] }, // variant
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[3] }, // list
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[4] }, // list_len
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[5] }, // tuple
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[6] }, // flags
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[7] }, // enum
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[8] }, // option
    { .tag = WASM_COMP_DEF_TYPE,
      .type.def_val_type = &def_val_types_inst[9] }, // result
    { .tag = WASM_COMP_INSTANCE_TYPE,
      .type.instance_type = &instance_type_inst }
};
static WASMComponentTypeSection type_section_instance = {
    .count = sizeof(types_def_inst) / sizeof(WASMComponentTypes),
    .types = types_def_inst
};

static WASMComponentTypes types_def_inst_2[] = {
    { .tag = WASM_COMP_COMPONENT_TYPE, .type.func_type = &func_type_inst },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[0] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[1] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[2] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[3] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[4] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[5] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[6] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[7] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[8] },
    { .tag = WASM_COMP_DEF_TYPE, .type.def_val_type = &def_val_types_inst[9] },
    { .tag = WASM_COMP_FUNC_TYPE, .type.func_type = &func_type_inst },
    { .tag = WASM_COMP_INSTANCE_TYPE,
      .type.instance_type = &instance_type_inst_2 }
};
static WASMComponentTypeSection type_section_instance_2 = {
    .count = sizeof(types_def_inst_2) / sizeof(WASMComponentTypes),
    .types = types_def_inst_2
};

// =============================================================================
// Section 8: Canons Section
// =============================================================================
static WASMComponentCanonOpt canon_opts_def_inst[] = {
    { .tag = WASM_COMP_CANON_OPT_STRING_UTF8 },
    { .tag = WASM_COMP_CANON_OPT_STRING_UTF16 },
    { .tag = WASM_COMP_CANON_OPT_STRING_LATIN1_UTF16 },
    { .tag = WASM_COMP_CANON_OPT_MEMORY, .payload.memory.mem_idx = 2 },
    { .tag = WASM_COMP_CANON_OPT_REALLOC, .payload.realloc_opt.func_idx = 2 },
    { .tag = WASM_COMP_CANON_OPT_POST_RETURN,
      .payload.post_return.func_idx = 2 },
    { .tag = WASM_COMP_CANON_OPT_ASYNC },
};
static WASMComponentCanonOpts canon_opts_list_inst = {
    .canon_opts_count = 7,
    .canon_opts = canon_opts_def_inst
};
static WASMComponentCanon canons_def_inst[] = {
    { .tag = WASM_COMP_CANON_LIFT,
      .canon_data.lift = { .core_func_idx = 0,
                           .canon_opts = &canon_opts_list_inst,
                           .type_idx = 0 } },
    { .tag = WASM_COMP_CANON_LOWER,
      .canon_data.lower = { .func_idx = 0,
                            .canon_opts = &canon_opts_list_inst } }
};
static WASMComponentCanonSection canon_section_instance = {
    .count = 2,
    .canons = canons_def_inst
};

// =============================================================================
// Section 9: Start Section
// =============================================================================
static uint32_t start_args_inst[] = { 2, 2 };
static WASMComponentStartSection start_section_instance = { .func_idx = 2,
                                                            .value_args_count =
                                                                2,
                                                            .value_args =
                                                                start_args_inst,
                                                            .result = 2 };

// =============================================================================
// Section 10: Import Section
// =============================================================================

static WASMComponentCoreName import_name_simple_func = { .name_len = 4,
                                                         .name = "test func" };
static WASMComponentCoreName import_name_simple_type = { .name_len = 4,
                                                         .name = "test type" };
static WASMComponentCoreName import_name_simple_value = { .name_len = 4,
                                                          .name =
                                                              "test value" };
static WASMComponentCoreName import_name_simple_component = {
    .name_len = 4,
    .name = "test component"
};
static WASMComponentCoreName import_name_simple_instance = {
    .name_len = 4,
    .name = "test instance"
};
static WASMComponentCoreName import_name_version_core_module = {
    .name_len = 4,
    .name = "test core module"
};

static WASMComponentSort import_sort_core_sort = {
    .sort = WASM_COMP_SORT_CORE_SORT,
    .core_sort = WASM_COMP_CORE_SORT_MODULE
};
static WASMComponentSort import_sort_func = { .sort = WASM_COMP_SORT_FUNC };
static WASMComponentSort import_sort_value = { .sort = WASM_COMP_SORT_VALUE };
static WASMComponentSort import_sort_type = { .sort = WASM_COMP_SORT_TYPE };
static WASMComponentSort import_sort_component = {
    .sort = WASM_COMP_SORT_COMPONENT
};
static WASMComponentSort import_sort_inst = { .sort = WASM_COMP_SORT_INSTANCE };

static WASMComponentSortIdx import_sort_idx_core_module = {
    .sort = &import_sort_core_sort,
    .idx = 2
};
static WASMComponentSortIdx import_sort_idx_func = { .sort = &import_sort_func,
                                                     .idx = 0 };
static WASMComponentSortIdx import_sort_idx_value = { .sort =
                                                          &import_sort_value,
                                                      .idx = 2 };
static WASMComponentSortIdx import_sort_idx_type = { .sort = &import_sort_type,
                                                     .idx = 2 };
static WASMComponentSortIdx import_sort_idx_component = {
    .sort = &import_sort_component,
    .idx = 2
};
static WASMComponentSortIdx import_sort_idx_inst = { .sort = &import_sort_inst,
                                                     .idx = 0 };

static WASMComponentImportName import_name_inst_simple_func = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .imported.simple.name = &import_name_simple_func
};
static WASMComponentImportName import_name_inst_simple_type = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .imported.simple.name = &import_name_simple_type
};
static WASMComponentImportName import_name_inst_simple_value = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .imported.simple.name = &import_name_simple_value
};
static WASMComponentImportName import_name_inst_simple_component = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .imported.simple.name = &import_name_simple_component
};
static WASMComponentImportName import_name_inst_simple_instance = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .imported.simple.name = &import_name_simple_instance
};

static WASMComponentImportName import_name_inst_versioned_core_module = {
    .tag = WASM_COMP_IMPORTNAME_VERSIONED,
    .imported.versioned = { .name = &import_name_version_core_module,
                            .version = &import_name_version_core_module }
};

static WASMComponentExternDesc import_extern_desc_core_module = {
    .type = WASM_COMP_EXTERN_CORE_MODULE,
    .extern_desc.func.type_idx = 2
};
static WASMComponentExternDesc import_extern_desc_func = {
    .type = WASM_COMP_EXTERN_FUNC,
    .extern_desc.func.type_idx = 2
};
static WASMComponentExternDesc import_extern_desc_value = {
    .type = WASM_COMP_EXTERN_VALUE,
    .extern_desc.func.type_idx = 2
};
static WASMComponentTypeBound import_type_bound = { .tag =
                                                        WASM_COMP_TYPEBOUND_EQ,
                                                    .type_idx = 2 };
static WASMComponentExternDesc import_extern_desc_type = {
    .type = WASM_COMP_EXTERN_TYPE,
    .extern_desc.type.type_bound = &import_type_bound
};
static WASMComponentExternDesc import_extern_desc_component = {
    .type = WASM_COMP_EXTERN_COMPONENT,
    .extern_desc.func.type_idx = 2
};
static WASMComponentExternDesc import_extern_desc_inst = {
    .type = WASM_COMP_EXTERN_INSTANCE,
    .extern_desc.func.type_idx = 2
};

static WASMComponentImport imports_def_inst[] = {
    { .import_name = &import_name_inst_simple_func,
      .extern_desc = &import_extern_desc_func },
    { .import_name = &import_name_inst_simple_value,
      .extern_desc = &import_extern_desc_value },
    { .import_name = &import_name_inst_simple_type,
      .extern_desc = &import_extern_desc_type },

    { .import_name = &import_name_inst_simple_component,
      .extern_desc = &import_extern_desc_component },
    { .import_name = &import_name_inst_simple_instance,
      .extern_desc = &import_extern_desc_inst },
    { .import_name = &import_name_inst_versioned_core_module,
      .extern_desc = &import_extern_desc_core_module },
};
static WASMComponentImportSection import_section_instance = {
    .count = sizeof(imports_def_inst) / sizeof(WASMComponentImport),
    .imports = imports_def_inst
};

static WASMComponentFunctionInstance func_inst;
static WASMComponentValue value_inst;
static WASMComponentTypeInstance type_inst;
static WASMComponent component_inst;
static WASMComponentInstance instance_inst;
static WASMModule core_module_inst;

static WASMComponentInstArgInstance args_list[] = {
    { .name = &import_name_simple_func,
      .sort_idx = &import_sort_idx_func,
      .arg.function = &func_inst },
    { .name = &import_name_simple_value,
      .sort_idx = &import_sort_idx_value,
      .arg.value = &value_inst },
    { .name = &import_name_simple_type,
      .sort_idx = &import_sort_idx_type,
      .arg.type = &type_inst },
    { .name = &import_name_simple_component,
      .sort_idx = &import_sort_idx_component,
      .arg.component = &component_inst },
    { .name = &import_name_simple_instance,
      .sort_idx = &import_sort_idx_inst,
      .arg.instance = &instance_inst },
    { .name = &import_name_version_core_module,
      .sort_idx = &import_sort_idx_core_module,
      .arg.core_module = &core_module_inst },
};

WASMComponentInstArgInstances inst_args = {
    .arg_len = sizeof(args_list) / sizeof(WASMComponentInstArgInstance),
    .args = args_list
};

// =============================================================================
// Section 11: Export Section
// =============================================================================
static WASMComponentCoreName export_name_simple_inst = { .name_len = 4,
                                                         .name = "test" };
static WASMComponentExportName export_name_inst = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .exported.simple.name = &export_name_simple_inst
};

static WASMComponentCoreName export_name_simple_type = { .name_len = 9,
                                                         .name = "test type" };
static WASMComponentExportName export_name_type = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .exported.simple.name = &export_name_simple_type
};

static WASMComponentCoreName export_name_simple_func = { .name_len = 9,
                                                         .name = "test func" };
static WASMComponentExportName export_name_func = {
    .tag = WASM_COMP_IMPORTNAME_SIMPLE,
    .exported.simple.name = &export_name_simple_func
};

static WASMComponentSort export_sort_core_sort = {
    .sort = WASM_COMP_SORT_CORE_SORT,
    .core_sort = WASM_COMP_CORE_SORT_MODULE
};
static WASMComponentSort export_sort_func = { .sort = WASM_COMP_SORT_FUNC };
static WASMComponentSort export_sort_value = { .sort = WASM_COMP_SORT_VALUE };
static WASMComponentSort export_sort_type = { .sort = WASM_COMP_SORT_TYPE };
static WASMComponentSort export_sort_component = {
    .sort = WASM_COMP_SORT_COMPONENT
};
static WASMComponentSort export_sort_inst = { .sort = WASM_COMP_SORT_INSTANCE };
static WASMComponentSort export_sort_INVALID = { .sort = 0x6 };

static WASMComponentSortIdx export_sort_idx_core_module = {
    .sort = &export_sort_core_sort,
    .idx = 2
};
static WASMComponentSortIdx export_sort_idx_func = { .sort = &export_sort_func,
                                                     .idx = 0 };
static WASMComponentSortIdx export_sort_idx_value = { .sort =
                                                          &export_sort_value,
                                                      .idx = 2 };
static WASMComponentSortIdx export_sort_idx_type = { .sort = &export_sort_type,
                                                     .idx = 2 };
static WASMComponentSortIdx export_sort_idx_component = {
    .sort = &export_sort_component,
    .idx = 2
};
static WASMComponentSortIdx export_sort_idx_inst = { .sort = &export_sort_inst,
                                                     .idx = 0 };
static WASMComponentSortIdx export_sort_idx_INVALID = {
    .sort = &export_sort_INVALID,
    .idx = 2
};

static WASMComponentExternDesc extern_desc_core_INVALID = {
    .type = 0x06,
    .extern_desc = { .component = { .type_idx = 1 } }
};
static WASMComponentExternDesc extern_desc_core_module = {
    .type = WASM_COMP_EXTERN_CORE_MODULE,
    .extern_desc = { .core_module = { .type_idx = 2, .type_specific = 2 } }
};
static WASMComponentExternDesc extern_desc_func = {
    .type = WASM_COMP_EXTERN_FUNC,
    .extern_desc = { .func = { .type_idx = 2 } }
};
static WASMComponentExternDesc extern_desc_value = {
    .type = WASM_COMP_EXTERN_VALUE,
    .extern_desc = { .value = { .value_bound = NULL } }
};
static WASMComponentExternDesc extern_desc_type = {
    .type = WASM_COMP_EXTERN_TYPE,
    .extern_desc = { .type = { .type_bound = &type_bound_eq } }
};
static WASMComponentExternDesc extern_desc_type_sub = {
    .type = WASM_COMP_EXTERN_TYPE,
    .extern_desc = { .type = { .type_bound = &type_bound_type } }
};
static WASMComponentExternDesc extern_desc_component = {
    .type = WASM_COMP_EXTERN_COMPONENT,
    .extern_desc = { .component = { .type_idx = 2 } }
};
static WASMComponentExternDesc extern_desc_instance = {
    .type = WASM_COMP_EXTERN_INSTANCE,
    .extern_desc = { .instance = { .type_idx = 2 } }
};

static WASMComponentExport exports_def_inst[] = {
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_INVALID,
      .extern_desc = &extern_desc_core_INVALID },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_core_module,
      .extern_desc = &extern_desc_core_module },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_func,
      .extern_desc = &extern_desc_func },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_value,
      .extern_desc = &extern_desc_value },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_type,
      .extern_desc = &extern_desc_type },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_type,
      .extern_desc = &extern_desc_type_sub },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_component,
      .extern_desc = &extern_desc_component },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_inst,
      .extern_desc = &extern_desc_instance },
};
static WASMComponentExportSection export_section_instance = {
    .count = sizeof(exports_def_inst) / sizeof(WASMComponentExport),
    .exports = exports_def_inst
};

static WASMComponentExport exports_def_inst_2[] = {
    { .export_name = &export_name_type,
      .sort_idx = &export_sort_idx_type,
      .extern_desc = &extern_desc_type },
    { .export_name = &export_name_func,
      .sort_idx = &export_sort_idx_func,
      .extern_desc = &extern_desc_func },
    { .export_name = &export_name_inst,
      .sort_idx = &export_sort_idx_inst,
      .extern_desc = &extern_desc_instance },
};
static WASMComponentExportSection export_section_instance_2 = {
    .count = sizeof(exports_def_inst_2) / sizeof(WASMComponentExport),
    .exports = exports_def_inst_2
};

// =============================================================================
// Section 12: Values Section
// =============================================================================
static const uint8_t value_data_inst[] = { 2, 2, 2, 2 };
static WASMComponentValueType value_type_def_inst = {
    .type = WASM_COMP_VAL_TYPE_PRIMVAL,
    .type_specific.primval_type = WASM_COMP_PRIMVAL_S32
};
static WASMComponentValue values_def_inst[] = {
    { .val_type = &value_type_def_inst,
      .core_data_len = sizeof(value_data_inst),
      .core_data = value_data_inst }
};
static WASMComponentValueSection value_section_instance = {
    .count = 1,
    .values = values_def_inst
};

// =============================================================================
// Mock Core Modules
// =============================================================================

uint8 dummy_local_types = 8;
uint16 dummy_local_offsets = 16;

WASMFunctionInstance dummy_func_implementation = {};
WASMFunction dummy_func_internal = {};
WASMFunctionImport dummy_func_import = {
    .func_ptr_linked = (void *)&dummy_func_implementation
};
WASMFunctionInstance dummy_core_func = { .is_import_func = true,
                                         .param_count = 4,
                                         .u.func_import = &dummy_func_import };
WASMFunctionInstance dummy_core_func_2 = { .is_import_func = false,
                                           .param_count = 4,
                                           .u.func = &dummy_func_internal,
                                           .local_types = &dummy_local_types,
                                           .local_offsets =
                                               &dummy_local_offsets };
WASMExportFuncInstance core_func_exp[] = { { .name = "test core func",
                                             .function = &dummy_core_func } };

WASMGlobalInstance dummy_core_global = {};
static WASMExportGlobInstance core_global_exp[] = {
    { .name = "test core global", .global = &dummy_core_global }
};

WASMTableInstance dummy_core_table = {};
static WASMExportTabInstance core_table_exp[] = {
    { .name = "test core table", .table = &dummy_core_table }
};

WASMMemoryInstance dummy_core_memory = {};
static WASMExportMemInstance core_memory_exp[] = {
    { .name = "test core memory", .memory = &dummy_core_memory }
};

WASMModuleInstance core_inst = { .export_func_count = 1,
                                 .export_functions = core_func_exp,
                                 .export_global_count = 1,
                                 .export_globals = core_global_exp,
                                 .export_table_count = 1,
                                 .export_tables = core_table_exp,
                                 .export_memory_count = 1,
                                 .export_memories = core_memory_exp };

WASMImport core_module_imports[] = { { .kind = EXPORT_KIND_FUNC,
                                       .u.names.field_name = "test core func",
                                       .u.names.module_name = "test module" },
                                     { .kind = EXPORT_KIND_TABLE,
                                       .u.names.field_name = "test core table",
                                       .u.names.module_name = "test module" },
                                     { .kind = EXPORT_KIND_MEMORY,
                                       .u.names.field_name = "test core memory",
                                       .u.names.module_name = "test module" },
                                     { .kind = EXPORT_KIND_GLOBAL,
                                       .u.names.field_name = "test core global",
                                       .u.names.module_name = "test module" } };

WASMModule test_core_module = { .import_count = sizeof(core_module_imports)
                                                / sizeof(WASMImport),
                                .imports = core_module_imports,
                                .import_function_count = 1,
                                .import_functions = &core_module_imports[0],
                                .import_table_count = 1,
                                .import_tables = &core_module_imports[1],
                                .import_memory_count = 1,
                                .import_memories = &core_module_imports[0],
                                .import_global_count = 1,
                                .import_globals = &core_module_imports[0],
                                .name = "TEST" };

// =============================================================================
// Main Component Structure
// =============================================================================
static WASMComponentSection sections_def[] = {
    { .id = WASM_COMP_SECTION_CORE_CUSTOM,
      .parsed.core_custom = &core_custom_section_instance },
    { .id = WASM_COMP_SECTION_CORE_MODULE,
      .parsed.core_module = &core_module_wrapper_instance },
    { .id = WASM_COMP_SECTION_CORE_MODULE,
      .parsed.core_module = &core_module_wrapper_instance },
    { .id = WASM_COMP_SECTION_CORE_INSTANCE,
      .parsed.core_instance_section = &core_instance_section_instance },
    { .id = WASM_COMP_SECTION_CORE_TYPE,
      .parsed.core_type_section = &core_type_section_instance },
    { .id = WASM_COMP_SECTION_COMPONENT,
      .parsed.component = &nested_component_instance },
    { .id = WASM_COMP_SECTION_INSTANCES,
      .parsed.instance_section = &instance_section_instance },
    { .id = WASM_COMP_SECTION_ALIASES,
      .parsed.alias_section = &alias_section_instance },
    { .id = WASM_COMP_SECTION_TYPE,
      .parsed.type_section = &type_section_instance },
    { .id = WASM_COMP_SECTION_CANONS,
      .parsed.canon_section = &canon_section_instance },
    { .id = WASM_COMP_SECTION_START,
      .parsed.start_section = &start_section_instance },
    { .id = WASM_COMP_SECTION_IMPORTS,
      .parsed.import_section = &import_section_instance },
    { .id = WASM_COMP_SECTION_EXPORTS,
      .parsed.export_section = &export_section_instance },
    { .id = WASM_COMP_SECTION_EXPORTS,
      .parsed.export_section = &export_section_instance_2 },
    { .id = WASM_COMP_SECTION_VALUES,
      .parsed.value_section = &value_section_instance },
    { .id = WASM_COMP_SECTION_ALIASES,
      .parsed.alias_section = &alias_section_instance_2 },
};

static WASMComponentSection sections_def_2[] = {
    { .id = WASM_COMP_SECTION_CORE_CUSTOM,
      .parsed.core_custom = &core_custom_section_instance },
    { .id = WASM_COMP_SECTION_CORE_MODULE,
      .parsed.core_module = &core_module_wrapper_instance },
    { .id = WASM_COMP_SECTION_CORE_INSTANCE,
      .parsed.core_instance_section = &core_instance_section_instance },
    { .id = WASM_COMP_SECTION_COMPONENT,
      .parsed.component = &nested_component_instance },
    { .id = WASM_COMP_SECTION_INSTANCES,
      .parsed.instance_section = &instance_section_instance },
    { .id = WASM_COMP_SECTION_ALIASES,
      .parsed.alias_section = &alias_section_instance },
    { .id = WASM_COMP_SECTION_TYPE,
      .parsed.type_section = &type_section_instance },
    { .id = WASM_COMP_SECTION_IMPORTS,
      .parsed.import_section = &import_section_instance },
    { .id = WASM_COMP_SECTION_EXPORTS,
      .parsed.export_section = &export_section_instance },
};

static WASMComponentSection sections_def_3[] = {
    { .id = WASM_COMP_SECTION_TYPE,
      .parsed.type_section = &type_section_instance_2 },
};

WASMComponent test_component = {
    .header = { .magic = 2, .version = 2, .layer = 2 },
    .sections = sections_def,
    .section_count = sizeof(sections_def) / sizeof(WASMComponentSection)
};

WASMComponent test_component_2 = {
    .header = { .magic = 2, .version = 2, .layer = 2 },
    .sections = sections_def_2,
    .section_count = sizeof(sections_def_2) / sizeof(WASMComponentSection)
};

WASMComponent test_component_3 = {
    .header = { .magic = 2, .version = 2, .layer = 2 },
    .sections = sections_def_3,
    .section_count = sizeof(sections_def_3) / sizeof(WASMComponentSection)
};

WASMComponent test_component_INVALID = {
    .header = { .magic = 2, .version = 2, .layer = 2 },
    .sections = NULL,
    .section_count = 1
};

/// test index count
WASMComponentIndexCount test_index_count = { .components = 2,
                                             .core_functions = 2,
                                             .core_globals = 2,
                                             .core_instances = 2,
                                             .core_memories = 2,
                                             .core_modules = 2,
                                             .core_tables = 2,
                                             .core_types = 2,
                                             .exports = 3,
                                             .functions = 3,
                                             .imports = 3,
                                             .instances = 3,
                                             .types = 3,
                                             .types_total_size = 1000,
                                             .values = 3 };

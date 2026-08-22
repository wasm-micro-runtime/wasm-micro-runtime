/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <gtest/gtest.h>
#include <vector>
#include <memory>
#include <cstdio>
#include <cstring>
#include <string>
#include "helpers.h"
extern "C" {
#include "test_wasm_component.h"
#include "wasm_component_runtime.h"
#include "wasm_component_canonical.h"
}

class ComponentInstantiationTest : public testing::Test
{
  public:
    ComponentInstantiationTest() {}
    ~ComponentInstantiationTest() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;


    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;

    virtual void SetUp() {
      printf("Starting setup\n");
      memset(&init_args, 0, sizeof(RuntimeInitArgs));

      init_args.mem_alloc_type = Alloc_With_Pool;
      init_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
      init_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

      if (!wasm_runtime_full_init(&init_args)) {
          printf("Failed to initialize WAMR runtime.\n");
          runtime_init = false;
      } else {
          runtime_init = true;
      }
      
      printf("Ending setup\n");
    }

    virtual void TearDown() {
      printf("Starting teardown\n");
        if (runtime_init) {
          printf("Starting to destroy runtime\n");
          wasm_runtime_destroy();
          runtime_init = false;
        }
        printf("Ending teardown\n");

    }

    WASMComponent* load_component_from_candidates(const char *file_name) { 
      return load_component_from_candidates_internal(file_name, "component-instantiation");
    }

    virtual void test_values(WASMComponentTypeInstance **val_instance, WASMComponentValueType *val_definition, WASMComponentTypeInstance **types) {
      if (!val_definition) {
        ASSERT_EQ(*val_instance, nullptr);
      }
      else if (val_definition->type == WASM_COMP_VAL_TYPE_PRIMVAL) {
        ASSERT_NE(*val_instance, nullptr);
        ASSERT_EQ((*val_instance)->type, COMPONENT_VAL_TYPE_PRIMVAL);
        ASSERT_EQ((*val_instance)->type_specific.primval, val_definition->type_specific.primval_type);
      }
      else {
        ASSERT_TRUE(*val_instance == types[val_definition->type_specific.type_idx]);
      }
    }

    virtual void test_def_type(WASMComponentTypeInstance *def_type_instance, WASMComponentDefValType *def_type_definition, WASMComponentTypeInstance **types) {

      WASMComponentValueType *val_definition;
      uint32 idx;

      switch (def_type_definition->tag) {
        case WASM_COMP_DEF_VAL_PRIMVAL:    // pvt:<primvaltype>
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_PRIMVAL);
          ASSERT_EQ(def_type_instance->type_specific.primval, def_type_definition->def_val.primval);
          break;
        // Record type (labeled fields)
        case WASM_COMP_DEF_VAL_RECORD:     // 0x72 lt*:vec(<labelvaltype>)
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_RECORD);
          ASSERT_EQ(def_type_instance->type_specific.record->count, def_type_definition->def_val.record->count);
          for (idx = 0; idx < def_type_definition->def_val.record->count; idx++) {
            ASSERT_EQ(def_type_instance->type_specific.record->fields[idx].label, def_type_definition->def_val.record->fields[idx].label);
            val_definition = def_type_definition->def_val.record->fields[idx].value_type;
            test_values(&def_type_instance->type_specific.record->fields[idx].type, val_definition, types);
          }
          break;
        // Variant type (labeled cases)
        case WASM_COMP_DEF_VAL_VARIANT:     // 0x71 case*:vec(<case>)
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_VARIANT);
          ASSERT_EQ(def_type_instance->type_specific.variant->count, def_type_definition->def_val.variant->count);
          for (idx = 0; idx < def_type_definition->def_val.variant->count; idx++) {
            ASSERT_EQ(def_type_instance->type_specific.variant->cases[idx].label, def_type_definition->def_val.variant->cases[idx].label);
            val_definition = def_type_definition->def_val.variant->cases[idx].value_type;
            test_values(&def_type_instance->type_specific.variant->cases[idx].value_type, val_definition, types);
          }
          break;
        // List types
        case WASM_COMP_DEF_VAL_LIST:        // 0x70 t:<valtype>
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_LIST);
          val_definition = def_type_definition->def_val.list->element_type;
          test_values(&def_type_instance->type_specific.list->element_type, val_definition, types);
          break;
        case WASM_COMP_DEF_VAL_LIST_LEN:    // 0x67 t:<valtype> len:<u32>
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_FIXED_SIZE_LIST);
          val_definition = def_type_definition->def_val.list_len->element_type;
          ASSERT_EQ(def_type_instance->type_specific.list_len->len, def_type_definition->def_val.list_len->len);
          test_values(&def_type_instance->type_specific.list_len->element_type, val_definition, types);
          break;

        // Tuple type
        case WASM_COMP_DEF_VAL_TUPLE:       // 0x6f t*:vec(<valtype>)
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_TUPLE);
          ASSERT_EQ(def_type_instance->type_specific.tuple->count, def_type_definition->def_val.tuple->count);
          for (idx = 0; idx < def_type_definition->def_val.tuple->count; idx++) {
            val_definition = &def_type_definition->def_val.tuple->element_types[idx];
            test_values(&def_type_instance->type_specific.tuple->element_types[idx], val_definition, types);
          }
          break;
        // Flags type
        case WASM_COMP_DEF_VAL_FLAGS:       // 0x6e l*:vec(<label'>)
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_FLAGS);
          ASSERT_EQ(def_type_instance->type_specific.flag, def_type_definition->def_val.flag);
          break;
        // Enum type
        case WASM_COMP_DEF_VAL_ENUM:        // 0x6d l*:vec(<label'>)
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_ENUM);
          ASSERT_EQ(def_type_instance->type_specific.enum_type, def_type_definition->def_val.enum_type);
          break;
        // Option type
        case WASM_COMP_DEF_VAL_OPTION:      // 0x6b t:<valtype>
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_OPTION);
          val_definition = def_type_definition->def_val.option->element_type;
          test_values(&def_type_instance->type_specific.option->element_type, val_definition, types);
          break;
        // Result type
        case WASM_COMP_DEF_VAL_RESULT:      // 0x6a t?:<valtype>? u?:<valtype>?
          ASSERT_EQ(def_type_instance->type, COMPONENT_VAL_TYPE_RESULT);
          val_definition = def_type_definition->def_val.result->error_type;
          test_values(&def_type_instance->type_specific.result->error_type, val_definition, types);
          val_definition = def_type_definition->def_val.result->result_type;
          test_values(&def_type_instance->type_specific.result->result_type, val_definition, types);
          break;
        default:
          break;
      }
    }

    virtual void test_func_type(WASMComponentTypeInstance *func_type_instance, WASMComponentFuncType  *func_type_definition, WASMComponentTypeInstance **types) {
      uint32 idx;
      ASSERT_TRUE(func_type_instance);
      ASSERT_EQ(func_type_instance->type, COMPONENT_VAL_TYPE_FUNCTION);
      ASSERT_TRUE(func_type_instance->type_specific.function);
      ASSERT_TRUE(func_type_instance->type_specific.function->params);
      ASSERT_TRUE(func_type_instance->type_specific.function->results);
      ASSERT_EQ(func_type_instance->type_specific.function->results->tag, func_type_definition->results->tag);
      test_values(&func_type_instance->type_specific.function->results->result, func_type_definition->results->results, types);
      ASSERT_EQ(func_type_instance->type_specific.function->params->count, func_type_definition->params->count);
      for (idx = 0; idx < func_type_definition->params->count; idx++) {
        ASSERT_EQ(func_type_instance->type_specific.function->params->params[idx].label, func_type_definition->params->params[idx].label);
        test_values(&func_type_instance->type_specific.function->params->params[idx].type, func_type_definition->params->params[idx].value_type, types);
      }
    }

    virtual void test_instance_type(WASMComponentTypeInstance *type_instance, WASMComponentInstType *instance_type_definition, WASMComponentTypeInstance **types) {
      WASMComponentInstanceDeclTypeSize instance_decl_size;
      memset(&instance_decl_size, 0, sizeof(WASMComponentInstanceDeclTypeSize));
      uint32 size = 0, decl_idx, type_idx = 0, func_idx = 0, export_idx = 0;;
      WASMComponentInstTypeInstance *instance_type_instance = type_instance->type_specific.instance;
      WASMComponentInstDecl instance_decl;
      size += wasm_get_inst_decl_size(instance_type_definition, &instance_decl_size);
      ASSERT_TRUE(type_instance);
      ASSERT_EQ(type_instance->type, COMPONENT_VAL_TYPE_INSTANCE);
      ASSERT_TRUE(type_instance->type_specific.instance->types);
      ASSERT_TRUE(type_instance->type_specific.instance->funcs);
      ASSERT_TRUE(type_instance->type_specific.instance->exports);
      ASSERT_EQ(type_instance->type_specific.instance->types_count, instance_decl_size.types_count);
      ASSERT_EQ(type_instance->type_specific.instance->func_count, instance_decl_size.func_count);
      ASSERT_EQ(type_instance->type_specific.instance->exports_count, instance_decl_size.exports_count);

      for (decl_idx = 0; decl_idx < instance_type_definition->count; decl_idx++) {
        instance_decl = instance_type_definition->instance_decls[decl_idx];
        switch (instance_decl.tag)
        {
          case WASM_COMP_COMPONENT_DECL_INSTANCE_TYPE:
            type_idx++;
            break;
          case WASM_COMP_COMPONENT_DECL_INSTANCE_ALIAS:
            type_idx++;
            break;
          case WASM_COMP_COMPONENT_DECL_INSTANCE_EXPORTDECL:
            switch(instance_decl.decl.export_decl->extern_desc->type) {
              case WASM_COMP_EXTERN_TYPE:
                type_idx++;
                ASSERT_EQ(instance_type_instance->exports[export_idx].export_name, instance_decl.decl.export_decl->export_name);
                ASSERT_EQ(instance_type_instance->exports[export_idx].type, instance_decl.decl.export_decl->extern_desc->type);
                export_idx++;
                break;
              case WASM_COMP_EXTERN_FUNC:
                func_idx++;
                ASSERT_EQ(instance_type_instance->exports[export_idx].export_name, instance_decl.decl.export_decl->export_name);
                ASSERT_EQ(instance_type_instance->exports[export_idx].type, instance_decl.decl.export_decl->extern_desc->type);
                export_idx++;
                break;
              default:
                break;
            }
          default:
            break;
        }
      }
    }

};

TEST_F(ComponentInstantiationTest, TestGetIndexCount)
{
  printf("TEST BEGIN\n");
  WASMComponentIndexCount *index_count;
  index_count =  wasm_component_get_index_count(NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(index_count == NULL);
  index_count =  wasm_component_get_index_count(&test_component_INVALID, error_buf, sizeof(error_buf));
  ASSERT_TRUE(index_count == NULL);
  index_count =  wasm_component_get_index_count(&test_component_2, error_buf, sizeof(error_buf));
  ASSERT_EQ(index_count->core_functions, 1); /*Core func section*/ 
  ASSERT_EQ(index_count->core_types, 1); /*Core type section*/ 
  ASSERT_EQ(index_count->core_modules, 2 + 1 + 1);   /*Core module section*/  /*Alias section*/   /*Import section*/ 
  ASSERT_EQ(index_count->core_instances, 3); /*Core instance section*/ 
  ASSERT_EQ(index_count->functions, 1 + 1 + 1); /*Alias section*/   /*Import section*/  /*Export section*/ 

  ASSERT_EQ(index_count->types, 1 + 1 + 2 + 12);   /*Type section*/   /*Alias section*/   /*Import section*/  /*Export section*/ 
  ASSERT_EQ(index_count->components, 2 + 1 + 1);  /*Component section*/   /*Alias section*/   /*Import section*/ 
  ASSERT_EQ(index_count->instances, 3 + 1 + 1);  /*Instance section*/   /*Alias section*/   /*Import section*/ 
  ASSERT_EQ(index_count->imports, 6);   /*Import section*/ 
  ASSERT_EQ(index_count->exports, 8);  /*Export section*/ 
  ASSERT_EQ(index_count->core_tables, 1);  /*Alias section*/  
  ASSERT_EQ(index_count->core_memories, 1);  /*Alias section*/  
  ASSERT_EQ(index_count->core_globals, 1);  /*Alias section*/  
  ASSERT_EQ(index_count->values, 1 + 1 + 1);   /*Alias section*/   /*Import section*/  /*Export section*/ 
  uint32 def_types_size = sizeof(WASMComponentTypeInstance) * 10
          + sizeof (WASMComponentRecordInstance) + 2 * sizeof(WASMComponentLabelValTypeInstance)
          + sizeof(WASMComponentVariantInstance) + 2 * sizeof (WASMComponentCaseValInstance)
          + sizeof (WASMComponentListInstance)
          + sizeof (WASMComponentListLenInstance)
          + sizeof(WASMComponentTupleInstance) + 3 * sizeof (WASMComponentTypeInstance *)
          + sizeof (WASMComponentOptionInstance)
          + sizeof (WASMComponentResultInstance)
          ;
  printf("Sizeof(WASMComponentLabelValTypeInstance) = %ld\n", sizeof(WASMComponentLabelValTypeInstance));
  uint32 instance_type_size = sizeof(WASMComponentTypeInstance) +
                sizeof(WASMComponentInstTypeInstance) + 4 * sizeof(WASMComponentTypeInstance) +
                2 * sizeof(WASMComponentFunctionInstance) + 2 * sizeof(WASMComponentExportInstance) +
                2 * sizeof(WASMFunctionInstance) + 2 *sizeof(WASMFunctionImport) + sizeof(WASMComponentTypeInstance) +
                sizeof(WASMComponentTypeInstance) +
                sizeof (WASMComponentRecordInstance) + 2 * sizeof (WASMComponentLabelValTypeInstance);
                ;
  uint32 types_total_size = 
          + sizeof(WASMComponentFuncTypeInstance) + sizeof(WASMComponentParamListInstance) + 2 * sizeof (WASMComponentLabelValTypeInstance) + sizeof(WASMComponentResultListInstance) + sizeof(WASMComponentTypeInstance)
          + def_types_size
          + instance_type_size;
  ASSERT_EQ(index_count->types_total_size, types_total_size );


  wasm_runtime_free(index_count);

  printf("TEST END\n");
}

TEST_F(ComponentInstantiationTest, TestMemAllocation)
{
  WASMComponentInstance *comp_instance =  wasm_component_instance_allocate(&test_index_count, NULL, 0);
  ASSERT_TRUE(comp_instance);
  ASSERT_TRUE(comp_instance->defined_types_size == test_index_count.types_total_size);
  ASSERT_TRUE(comp_instance->functions == (WASMComponentFunctionInstance **) ((uint64)comp_instance + sizeof(WASMComponentInstance)));
  ASSERT_TRUE(comp_instance->values == (WASMComponentValue **) ((uint64)comp_instance->functions + test_index_count.functions * sizeof (WASMComponentFunctionInstance *)));
  ASSERT_TRUE(comp_instance->types == (WASMComponentTypeInstance **) ((uint64)comp_instance->values + test_index_count.values * sizeof (WASMComponentValue *)));
  ASSERT_TRUE(comp_instance->component_instances == (WASMComponentInstance **) ((uint64)comp_instance->types + test_index_count.types * sizeof (WASMComponentTypeInstance *)));
  ASSERT_TRUE(comp_instance->components == (WASMComponent **) ((uint64)comp_instance->component_instances + test_index_count.instances * sizeof (WASMComponentInstance *)));
  ASSERT_TRUE(comp_instance->core_functions == (WASMFunctionInstance **) ((uint64)comp_instance->components + test_index_count.components * sizeof (WASMComponent *)));
  ASSERT_TRUE(comp_instance->core_tables == (WASMTableInstance **) ((uint64)comp_instance->core_functions + test_index_count.core_functions * sizeof (WASMFunctionInstance *)));
  ASSERT_TRUE(comp_instance->core_memories == (WASMMemoryInstance **) ((uint64)comp_instance->core_tables + test_index_count.core_tables * sizeof (WASMTableInstance *)));
  ASSERT_TRUE(comp_instance->core_globals == (WASMGlobalInstance **) ((uint64)comp_instance->core_memories + test_index_count.core_memories * sizeof (WASMMemoryInstance *)));
  ASSERT_TRUE(comp_instance->core_types == (WASMType **)  ((uint64)comp_instance->core_globals + test_index_count.core_globals * sizeof (WASMGlobalInstance *)));
  ASSERT_TRUE(comp_instance->core_module_instances == (WASMModuleInstance **) ((uint64)comp_instance->core_types + test_index_count.core_types * sizeof (WASMType *)));
  ASSERT_TRUE(comp_instance->core_modules == (WASMModule **) ((uint64)comp_instance->core_module_instances + test_index_count.core_instances* sizeof (WASMModuleInstance *)));
  ASSERT_TRUE(comp_instance->exports == (WASMComponentExportInstance *) ((uint64)comp_instance->core_modules + test_index_count.core_modules * sizeof (WASMModule *)));
  ASSERT_TRUE(comp_instance->defined_types == (void *) ((uint64)comp_instance->exports + test_index_count.exports * sizeof(WASMComponentExportInstance)));
  wasm_component_deinstantiate(comp_instance);

}

TEST_F(ComponentInstantiationTest, TestResolveTypes)
{
  bool status;
  WASMComponentIndexCount *index_count = wasm_component_get_index_count(&test_component_3, error_buf, sizeof(error_buf));
  ASSERT_TRUE(index_count);

  LOG_VERBOSE("Found %d types, total size = %ld\n", index_count->types, index_count->types_total_size);
  WASMComponentInstance *comp_instance =  wasm_component_instance_allocate(index_count, NULL, 0);
  ASSERT_TRUE(test_component_3.sections[0].id == WASM_COMP_SECTION_TYPE);
  WASMComponentTypeSection *types_section = test_component_3.sections[0].parsed.type_section;

  ASSERT_EQ(wasm_resolve_types(types_section, NULL, NULL, 0), false);
  ASSERT_EQ(wasm_resolve_types(NULL, comp_instance, NULL, 0), false);
  status = wasm_resolve_types(types_section, comp_instance, NULL, 0);
  ASSERT_TRUE(status);
  
  ASSERT_EQ(comp_instance->types[0]->type, COMPONENT_VAL_TYPE_PRIMVAL);
  ASSERT_EQ(comp_instance->types[1]->type, COMPONENT_VAL_TYPE_RECORD);

  test_def_type(comp_instance->types[0], types_section->types[1].type.def_val_type, comp_instance->types); // primval
  test_def_type(comp_instance->types[1], types_section->types[2].type.def_val_type, comp_instance->types); // record
  test_def_type(comp_instance->types[2], types_section->types[3].type.def_val_type, comp_instance->types); // variant
  test_def_type(comp_instance->types[3], types_section->types[4].type.def_val_type, comp_instance->types); // list
  test_def_type(comp_instance->types[4], types_section->types[5].type.def_val_type, comp_instance->types); // list_len
  test_def_type(comp_instance->types[5], types_section->types[6].type.def_val_type, comp_instance->types); // tuple
  test_def_type(comp_instance->types[6], types_section->types[7].type.def_val_type, comp_instance->types); // flags
  test_def_type(comp_instance->types[7], types_section->types[8].type.def_val_type, comp_instance->types); // enums
  test_def_type(comp_instance->types[8], types_section->types[9].type.def_val_type, comp_instance->types); // options
  test_def_type(comp_instance->types[9], types_section->types[10].type.def_val_type, comp_instance->types); // result
  test_func_type(comp_instance->types[10], types_section->types[11].type.func_type, comp_instance->types); // function type
  test_instance_type(comp_instance->types[11], types_section->types[12].type.instance_type, comp_instance->types);

  WASMComponentInstTypeInstance *instance_type = comp_instance->types[11]->type_specific.instance;

  ASSERT_EQ(comp_instance->defined_types_size, 0);
  wasm_component_deinstantiate(comp_instance);
  wasm_runtime_free(index_count);

}

TEST_F(ComponentInstantiationTest, TestResolveExports)
{
  uint32 idx;
  bool status;

  WASMComponentIndexCount index_count;
  memset(&index_count, 0, sizeof(WASMComponentIndexCount));
  index_count.types = 4;
  index_count.functions = 2;
  index_count.instances = 2;
  index_count.exports = 3;

  WASMComponentInstance *comp_instance =  wasm_component_instance_allocate(&index_count, NULL, 0);
  WASMComponentTypeInstance test_types[3];
  test_types[0].type = COMPONENT_VAL_TYPE_PRIMVAL;
  test_types[1].type = COMPONENT_VAL_TYPE_RECORD;
  test_types[2].type = COMPONENT_VAL_TYPE_FUNCTION;

  WASMComponentFunctionInstance test_function;
  WASMComponentInstance test_instance;
  memset(&test_instance, 0, sizeof(WASMComponentInstance));
 
  comp_instance->types[0] = &test_types[0];
  comp_instance->types[1] = &test_types[1];
  comp_instance->types[2] = &test_types[2];
  comp_instance->types_count = 3;
  comp_instance->functions[0] = &test_function;
  comp_instance->functions_count = 1;
  comp_instance->component_instances[0] = &test_instance;
  comp_instance->component_instances_count = 1;

  ASSERT_EQ(test_component.sections[13].id, WASM_COMP_SECTION_EXPORTS);
  WASMComponentExportSection *export_section = test_component.sections[13].parsed.export_section;

  wasm_resolve_exports(export_section, comp_instance, NULL, 0);
  ASSERT_EQ(comp_instance->types_count, 4);
  ASSERT_EQ(comp_instance->functions_count, 2);
  ASSERT_EQ(comp_instance->component_instances_count, 2);
  ASSERT_EQ(comp_instance->exports_count, 3);

  ASSERT_EQ(comp_instance->types[3], comp_instance->types[2]);
  ASSERT_EQ(comp_instance->functions[1], comp_instance->functions[0]);
  ASSERT_EQ(comp_instance->component_instances[1], comp_instance->component_instances[0]);

  ASSERT_EQ((uint64)comp_instance->defined_types - (uint64)comp_instance->exports , 3 * sizeof(WASMComponentExportInstance));
  ASSERT_EQ(comp_instance->exports[0].type, export_section->exports[0].extern_desc->type);
  ASSERT_EQ(comp_instance->exports[1].type, export_section->exports[1].extern_desc->type);
  ASSERT_EQ(comp_instance->exports[2].type, export_section->exports[2].extern_desc->type);

  ASSERT_EQ(comp_instance->exports[0].export_name, export_section->exports[0].export_name);
  ASSERT_EQ(comp_instance->exports[1].export_name, export_section->exports[1].export_name);
  ASSERT_EQ(comp_instance->exports[2].export_name, export_section->exports[2].export_name);

  ASSERT_EQ(comp_instance->exports[0].exp.type, comp_instance->types[3]);
  ASSERT_EQ(comp_instance->exports[1].exp.function, comp_instance->functions[1]);
  ASSERT_EQ(comp_instance->exports[2].exp.instance, comp_instance->component_instances[1]);


  wasm_component_deinstantiate(comp_instance);
}


TEST_F(ComponentInstantiationTest, TestResolveImports)
{
  uint32 idx;
  bool status;

  WASMComponentIndexCount index_count;
  memset(&index_count, 0, sizeof(WASMComponentIndexCount));
  index_count.types = 1;
  index_count.functions = 1;
  index_count.instances = 1;
  index_count.values = 1;
  index_count.components = 1;
  index_count.core_modules = 1;

  WASMComponentInstance *comp_instance =  wasm_component_instance_allocate(&index_count, NULL, 0);
  WASMComponentImportSection *import_section = test_component.sections[11].parsed.import_section;
  status = wasm_resolve_imports(import_section, comp_instance, &inst_args, NULL, 0);

  ASSERT_EQ(comp_instance->functions[0], inst_args.args[0].arg.function);
  ASSERT_EQ(comp_instance->values[0], inst_args.args[1].arg.value);
  ASSERT_EQ(comp_instance->types[0], inst_args.args[2].arg.type);
  ASSERT_EQ(comp_instance->components[0], inst_args.args[3].arg.component);
  ASSERT_EQ(comp_instance->component_instances[0], inst_args.args[4].arg.instance);
  ASSERT_EQ(comp_instance->core_modules[0], inst_args.args[5].arg.core_module);


  wasm_component_deinstantiate(comp_instance);
}

TEST_F(ComponentInstantiationTest, TestResolveAlias)
{
  uint32 idx;
  bool status;

  WASMComponentIndexCount index_count;
  memset(&index_count, 0, sizeof(WASMComponentIndexCount));
  index_count.instances = 1;
  index_count.components = 1;

  WASMComponentInstance *parent_instance = wasm_component_instance_allocate(&index_count, NULL, 0);
  WASMComponent p_comp;
  WASMComponentInstance p_inst;
  memset(&p_inst, 0, sizeof(WASMComponentInstance));
  parent_instance->components[0] = &p_comp;
  parent_instance->components_count = 1;
  parent_instance->component_instances[0] = &p_inst;
  parent_instance->component_instances_count = 1;

  WASMComponentIndexCount index_count_2;
  memset(&index_count_2, 0, sizeof(WASMComponentIndexCount));
  index_count_2.types = 1;
  index_count_2.functions = 1;
  index_count_2.values = 1;
  index_count_2.exports = 2;

  WASMComponentIndexCount index_count_3;
  memset(&index_count_3, 0, sizeof(WASMComponentIndexCount));
  index_count_3.instances = 2;
  index_count_3.components = 1;
  index_count_3.types = 1;
  index_count_3.functions = 1;
  index_count_3.values = 1;
  index_count_3.components = 1;
  index_count_3.core_modules = 1;
  index_count_3.core_functions = 1;
  index_count_3.core_globals = 1;
  index_count_3.core_instances = 1;
  index_count_3.core_tables = 1;
  index_count_3.core_memories = 1;

  WASMComponentInstance *inner_instance =  wasm_component_instance_allocate(&index_count_2, NULL, 0);

  static WASMComponentTypeInstance dummy_type = {};
  dummy_type.type = COMPONENT_VAL_TYPE_RECORD;

  static WASMComponentFunctionInstance dummy_func = {};

  static WASMComponentCoreName name_type = { .name_len = 4, .name = (char *)"test type" };
  static WASMComponentExportName exp_name_type = {};
  exp_name_type.tag = WASM_COMP_IMPORTNAME_SIMPLE;
  exp_name_type.exported.simple.name = &name_type;

  static WASMComponentCoreName name_func = { .name_len = 4, .name = (char *)"test func" };
  static WASMComponentExportName exp_name_func = {};
  exp_name_func.tag = WASM_COMP_IMPORTNAME_SIMPLE;
  exp_name_func.exported.simple.name = &name_func;

  // Mock target instance for alias exports
  inner_instance->exports[0].export_name = &exp_name_type;
  inner_instance->exports[0].type = WASM_COMP_EXTERN_TYPE;
  inner_instance->exports[0].exp.type = &dummy_type;

  inner_instance->exports[1].export_name = &exp_name_func;
  inner_instance->exports[1].type = WASM_COMP_EXTERN_FUNC;
  inner_instance->exports[1].exp.function = &dummy_func;
  inner_instance->exports_count = 2;

  WASMComponentInstance *comp_instance =  wasm_component_instance_allocate(&index_count_3, NULL, 0);
  comp_instance->parent = parent_instance;
  comp_instance->component_instances[0] = inner_instance;
  comp_instance->component_instances_count = 1;

  comp_instance->core_module_instances[0] = &core_inst;
  comp_instance->core_module_instances_count = 1;

  WASMComponentAliasSection *alias_section = test_component.sections[15].parsed.alias_section;
  status = wasm_resolve_alias(alias_section, comp_instance, error_buf, sizeof(error_buf));
  ASSERT_TRUE(status);
  // Test outer aliases
  ASSERT_EQ(comp_instance->components_count, 1);
  ASSERT_EQ(comp_instance->components[0], &p_comp );
  ASSERT_EQ(comp_instance->component_instances_count, 2);
  ASSERT_EQ(comp_instance->component_instances[1], &p_inst );
  // Test export aliases
  ASSERT_EQ(comp_instance->functions_count, 1);
  ASSERT_EQ(comp_instance->functions[0], &dummy_func );
  ASSERT_EQ(comp_instance->types_count, 1);
  ASSERT_EQ(comp_instance->types[0], &dummy_type );
  // Test core export aliases
  ASSERT_EQ(comp_instance->core_functions_count, 1);
  ASSERT_EQ(comp_instance->core_functions[0], &dummy_core_func);
  ASSERT_EQ(comp_instance->core_functions[0]->module_instance, &core_inst);
  ASSERT_EQ(comp_instance->core_globals_count, 1);
  ASSERT_EQ(comp_instance->core_globals[0], &dummy_core_global);
  ASSERT_EQ(comp_instance->core_tables_count, 1);
  ASSERT_EQ(comp_instance->core_tables[0], &dummy_core_table);
  ASSERT_EQ(comp_instance->core_memories_count, 1);
  ASSERT_EQ(comp_instance->core_memories[0], &dummy_core_memory);
  wasm_component_deinstantiate(parent_instance);
  wasm_component_deinstantiate(inner_instance);
  wasm_component_deinstantiate(comp_instance);
}

TEST_F(ComponentInstantiationTest, TestResolveCoreInstance) 
{
  uint32 idx;
  bool status;

  WASMComponentIndexCount index_count;
  memset(&index_count, 0, sizeof(WASMComponentIndexCount));
  index_count.core_instances = 3;
  index_count.core_modules = 1;
  index_count.core_functions = 1;
  index_count.core_tables = 1;
  index_count.core_memories = 1;
  index_count.core_globals = 1;

  WASMComponentInstance *comp_instance = wasm_component_instance_allocate(&index_count, NULL, 0);
  comp_instance->core_modules[0] = &test_core_module;
  comp_instance->core_modules_count = 1;
  comp_instance->core_module_instances[0] = &core_inst;
  comp_instance->core_module_instances_count = 1;
  WASMComponentCoreInstSection *instance_section = test_component.sections[3].parsed.core_instance_section;
  WASMComponentCoreInst *core_instance = &instance_section->instances[0];

  // Test imports resolution
  WASMCoreImports found_imports;
  memset(&found_imports, 0, sizeof(WASMCoreImports));
  found_imports.func_instance = (WASMFunctionInstance **)wasm_runtime_malloc(test_core_module.import_function_count * sizeof(WASMFunctionInstance *));
  found_imports.table_instance = (WASMTableInstance **)wasm_runtime_malloc(test_core_module.import_table_count * sizeof(WASMTableInstance *));
  found_imports.mem_instance = (WASMMemoryInstance **)wasm_runtime_malloc(test_core_module.import_memory_count * sizeof(WASMMemoryInstance *));
  found_imports.global_instance = (WASMGlobalInstance **)wasm_runtime_malloc(test_core_module.import_global_count * sizeof(WASMGlobalInstance *));
  wasm_resolve_core_imports(&core_instance->expression, &test_core_module, comp_instance, &found_imports, error_buf, sizeof(error_buf));
  ASSERT_EQ(found_imports.func_count, 1);
  ASSERT_EQ(found_imports.tables_count, 1);
  ASSERT_EQ(found_imports.mem_count, 1);
  ASSERT_EQ(found_imports.globals_count, 1);
  ASSERT_EQ(found_imports.func_instance[0], core_inst.export_functions[0].function);
  ASSERT_EQ(found_imports.table_instance[0], core_inst.export_tables[0].table);
  ASSERT_EQ(found_imports.mem_instance[0], core_inst.export_memories[0].memory);
  ASSERT_EQ(found_imports.global_instance[0], core_inst.export_globals[0].global);

// Test core instanciation without arguments

  comp_instance->core_functions[0] = &dummy_core_func;
  comp_instance->core_functions_count = 1;
  comp_instance->core_tables[0] = &dummy_core_table;
  comp_instance->core_tables_count = 1;
  comp_instance->core_memories[0] = &dummy_core_memory;
  comp_instance->core_memories_count = 1;
  comp_instance->core_globals[0] = &dummy_core_global;
  comp_instance->core_globals_count= 1;
  core_instance = &instance_section->instances[1];
  ASSERT_EQ(core_instance->instance_expression_tag, WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS);
  status = wasm_create_core_inst_from_expression(core_instance, comp_instance, error_buf, sizeof(error_buf));
  ASSERT_TRUE(status);
  ASSERT_EQ(comp_instance->core_module_instances_count, 2);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_func_count, 1);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_table_count, 1);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_memory_count, 1);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_global_count, 1);

  ASSERT_EQ(comp_instance->core_module_instances[1]->export_functions[0].name, core_instance->expression.without_args.inline_expr[0].name->name);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_tables[0].name, core_instance->expression.without_args.inline_expr[1].name->name);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_memories[0].name, core_instance->expression.without_args.inline_expr[2].name->name);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_globals[0].name, core_instance->expression.without_args.inline_expr[3].name->name);

  ASSERT_EQ(comp_instance->core_module_instances[1]->export_functions[0].function, &dummy_core_func);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_tables[0].table, &dummy_core_table);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_memories[0].memory, &dummy_core_memory);
  ASSERT_EQ(comp_instance->core_module_instances[1]->export_globals[0].global, &dummy_core_global);

  // Test core instantiation on a real wasm binary
  char file_name[] = "surface_and_geometry.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_EQ(component->sections[3].id, 4);
  WASMComponent *comp_1 = component->sections[3].parsed.component;
  ASSERT_EQ(comp_1->sections[2].id, 1);
  WASMModule *core_module_0 = (WASMModule *)component->sections[3].parsed.component->sections[2].parsed.core_module->module_handle;

  ASSERT_EQ(comp_1->sections[5].id, 2);
  instance_section = comp_1->sections[5].parsed.core_instance_section;
  ASSERT_EQ(instance_section->count, 2  );
  ASSERT_EQ(instance_section->instances[0].instance_expression_tag, WASM_COMP_INSTANCE_EXPRESSION_WITHOUT_ARGS);
  ASSERT_EQ(instance_section->instances[0].expression.without_args.inline_expr_len , 1);
  ASSERT_EQ(instance_section->instances[0].expression.without_args.inline_expr[0].sort_idx->idx , 0);
  ASSERT_EQ(instance_section->instances[0].expression.without_args.inline_expr[0].sort_idx->sort->sort , 0);
  ASSERT_EQ(instance_section->instances[1].expression.with_args.arg_len, 1);

  WASMComponentInlineExport *inline_expr = &instance_section->instances[0].expression.without_args.inline_expr[0];
  WASMInstExpr  *instance_expression = &instance_section->instances[0].expression;
  printf("\nCore instance 0 expression (without args):\n");
  printf("Sort: %d, Core sort: %d\n", inline_expr->sort_idx->sort->sort, inline_expr->sort_idx->sort->core_sort);
  printf("Name: %s\n",inline_expr->name->name );
  printf("Index: %d\n",inline_expr->sort_idx->idx );

  WASMComponentInstArg *instance_args = &instance_section->instances[0].expression.with_args.args[0];
  printf("\nCore instance 1 expression (with args):\n");
  printf("name: %s\n",instance_args->name->name);
  printf("Sort; %d, Core Sort: %d\n",instance_args->idx.sort_idx->sort->sort, instance_args->idx.sort_idx->sort->core_sort);
  printf("Core module index: %d, target core instance index: %d\n\n", instance_section->instances[0].expression.with_args.idx , instance_args->idx.sort_idx->idx);

  ASSERT_EQ(instance_section->instances[1].instance_expression_tag, WASM_COMP_INSTANCE_EXPRESSION_WITH_ARGS);
  ASSERT_EQ(instance_section->instances[1].expression.with_args.idx, 0);

  WASMComponentInstance *comp_instance_2 = wasm_component_instance_allocate(&index_count, NULL, 0);
  comp_instance_2->core_functions[0] = &dummy_core_func;
  comp_instance_2->core_functions_count = 1;
  comp_instance_2->core_modules[0] = core_module_0;
  comp_instance_2->core_modules_count = 1;
  comp_instance_2->component = component;
  
  // found function is import func
  status = wasm_resolve_core_instance(instance_section, comp_instance_2, NULL, 0);
  ASSERT_TRUE(status);
  ASSERT_EQ(comp_instance_2->core_module_instances_count, 2);
  ASSERT_EQ(comp_instance_2->core_module_instances[0]->export_func_count , 1);
  ASSERT_EQ(comp_instance_2->core_module_instances[0]->export_functions[0].function , &dummy_core_func);
  ASSERT_TRUE(comp_instance_2->core_module_instances[1]->e->functions[0].is_import_func);
  ASSERT_EQ(comp_instance_2->core_module_instances[1]->e->functions[0].u.func_import->func_ptr_linked, dummy_core_func.u.func_import->func_ptr_linked );
  ASSERT_EQ(comp_instance_2->core_module_instances[1]->import_func_ptrs[0], dummy_core_func.u.func_import->func_ptr_linked );

  wasm_component_deinstantiate(comp_instance_2);

  // found function is not import
  comp_instance_2 = wasm_component_instance_allocate(&index_count, NULL, 0);
  comp_instance_2->core_functions[0] = &dummy_core_func_2;
  comp_instance_2->core_functions_count = 1;
  comp_instance_2->core_modules[0] = core_module_0;
  comp_instance_2->core_modules_count = 1;
  comp_instance_2->component = component;
  
  status = wasm_resolve_core_instance(instance_section, comp_instance_2, NULL, 0);
  ASSERT_TRUE(status);
  ASSERT_EQ(comp_instance_2->core_module_instances_count, 2);
  ASSERT_EQ(comp_instance_2->core_module_instances[0]->export_func_count , 1);
  ASSERT_EQ(comp_instance_2->core_module_instances[0]->export_functions[0].function , &dummy_core_func_2);
  ASSERT_TRUE(comp_instance_2->core_module_instances[1]->e->functions[0].is_import_func);
  ASSERT_EQ(*(comp_instance_2->core_module_instances[1]->e->functions[0].local_types), 8);
  ASSERT_EQ(*(comp_instance_2->core_module_instances[1]->e->functions[0].local_offsets), 16);

  wasm_component_deinstantiate(comp_instance);

}

TEST_F(ComponentInstantiationTest, TestResolveCanons)
{
  bool status;

  // Build a minimal canon section: one LIFT (core_func=0, type=0, no opts) and one LOWER (func=0, no opts).
  WASMComponentCanon canons[2];
  memset(canons, 0, sizeof(canons));
  canons[0].tag = WASM_COMP_CANON_LIFT;
  canons[0].canon_data.lift.core_func_idx = 0;
  canons[0].canon_data.lift.type_idx      = 0;
  canons[0].canon_data.lift.canon_opts    = NULL;
  canons[1].tag = WASM_COMP_CANON_LOWER;
  canons[1].canon_data.lower.func_idx   = 0;
  canons[1].canon_data.lower.canon_opts = NULL;

  WASMComponentCanonSection canon_section_local;
  memset(&canon_section_local, 0, sizeof(canon_section_local));
  canon_section_local.count  = 2;
  canon_section_local.canons = canons;

  // Component func type: () -> ()
  WASMComponentParamListInstance   dummy_params;
  WASMComponentResultListInstance  dummy_results;
  memset(&dummy_params,  0, sizeof(dummy_params));   // count = 0
  memset(&dummy_results, 0, sizeof(dummy_results));
  dummy_results.tag = WASM_COMP_RESULT_LIST_EMPTY;

  WASMComponentFuncTypeInstance dummy_func_type;
  memset(&dummy_func_type, 0, sizeof(dummy_func_type));
  dummy_func_type.params  = &dummy_params;
  dummy_func_type.results = &dummy_results;

  WASMComponentTypeInstance dummy_type;
  memset(&dummy_type, 0, sizeof(dummy_type));
  dummy_type.type = COMPONENT_VAL_TYPE_FUNCTION;
  dummy_type.type_specific.function = &dummy_func_type;

  // Core func: () -> () - needs a WASMFuncType accessible via u.func->func_type
  WASMFuncType dummy_wasm_func_type;
  memset(&dummy_wasm_func_type, 0, sizeof(dummy_wasm_func_type));  // param_count=0, result_count=0

  WASMFunction dummy_wasm_func;
  memset(&dummy_wasm_func, 0, sizeof(dummy_wasm_func));
  dummy_wasm_func.func_type = &dummy_wasm_func_type;

  WASMFunctionInstance dummy_core_func;
  memset(&dummy_core_func, 0, sizeof(dummy_core_func));
  dummy_core_func.u.func = &dummy_wasm_func;  // is_import_func=false

  WASMComponentFunctionInstance dummy_comp_func;
  memset(&dummy_comp_func, 0, sizeof(dummy_comp_func));
  dummy_comp_func.core_func = &dummy_core_func;
  dummy_comp_func.func_type = &dummy_func_type;

  WASMComponentIndexCount index_count;
  memset(&index_count, 0, sizeof(index_count));
  index_count.types = 1;
  index_count.functions = 2;
  index_count.core_functions = 2;

  WASMComponentInstance *comp_instance = wasm_component_instance_allocate(&index_count, NULL, 0);
  comp_instance->functions[0]      = &dummy_comp_func;
  comp_instance->functions_count   = 1;
  comp_instance->core_functions[0] = &dummy_core_func;
  comp_instance->core_functions_count = 1;
  comp_instance->types[0]          = &dummy_type;
  comp_instance->types_count       = 1;

  status = wasm_resolve_canon(&canon_section_local, comp_instance, error_buf, sizeof(error_buf));
  ASSERT_TRUE(status);
  ASSERT_EQ(comp_instance->core_functions_count, 2);
  ASSERT_EQ(comp_instance->functions_count, 2);
  wasm_component_deinstantiate(comp_instance);
}


TEST_F(ComponentInstantiationTest, TestInstantiateInternal)
{
  uint32 idx;
  bool status;
  bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

  const char *path = nullptr;
  unsigned char *buf = nullptr;
  char file_name[] = "surface_and_geometry_0_2_0.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component);

  // Test component is instantiated
  WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  char file_name_2[] = "complex.wasm";
  WASMComponent *component_2 = load_component_from_candidates(file_name_2);
  ASSERT_NE(component_2, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component_2);

  // Test component is instantiated
  WASMComponentInstance *comp_instance_2 = wasm_component_instantiate_internal(component_2, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance_2);

  // Test all index spaces are populated correctly
  ASSERT_EQ(comp_instance->component_instances_count, 5);
  ASSERT_EQ(comp_instance->components_count, 2);
  ASSERT_EQ(comp_instance->exports_count, 1);
  ASSERT_EQ(comp_instance->component_instances[0]->core_functions_count, 3);
  ASSERT_EQ(comp_instance->component_instances[0]->functions_count , 2);
  ASSERT_EQ(comp_instance->component_instances[0]->types_count, 4);
  ASSERT_EQ(comp_instance->component_instances[0]->core_modules_count, 1);
  ASSERT_EQ(comp_instance->component_instances[0]->core_module_instances_count, 1);
  ASSERT_EQ(comp_instance->component_instances[2]->core_memories_count, 1);
  ASSERT_EQ(comp_instance->component_instances[2]->core_tables_count, 1);

  // Test canonical sections are resolved correctly
  ASSERT_TRUE(comp_instance->component_instances[0]->functions[1]->canon_options);
  ASSERT_EQ(comp_instance->component_instances[0]->functions[1]->func_type, comp_instance->component_instances[0]->types[3]->type_specific.function);
  ASSERT_EQ(comp_instance->component_instances[0]->functions[1]->canon_options->lift_lower_opts->lift_opts->memory, comp_instance->component_instances[0]->core_memories[0]);
  ASSERT_EQ(comp_instance->component_instances[0]->functions[1]->canon_options->lift_lower_opts->realloc_func, comp_instance->component_instances[0]->core_functions[1]);

  ASSERT_EQ(comp_instance->component_instances[2]->core_functions[3]->canon_options->lift_lower_opts->lift_opts->memory, comp_instance->component_instances[2]->core_memories[0]);

  // Test Alias sections are resolved correctly
  ASSERT_EQ(comp_instance->component_instances[1], comp_instance->component_instances[0]->exports[0].exp.instance); /*Alias export*/
  ASSERT_EQ(comp_instance->component_instances[2]->core_functions[1] , comp_instance->component_instances[2]->core_module_instances[0]->export_functions[0].function); /*Alias core export*/
  ASSERT_EQ(comp_instance_2->types[2]->type_specific.instance->types[0], comp_instance_2->types[1]); /*Alias outer*/

  // Test core export tables are instantiated correctly
  ASSERT_EQ(comp_instance->component_instances[2]->core_module_instances[0]->export_table_count, 1);

  // Test core module instantiation (test imports are resolved properly)
  ASSERT_EQ(comp_instance->component_instances[2]->core_module_instances[2]->e->functions[0].u.func, comp_instance->component_instances[2]->core_functions[0]->u.func );
  ASSERT_EQ(comp_instance->component_instances[2]->core_module_instances[2]->e->functions[0].import_func_inst, comp_instance->component_instances[2]->core_functions[0]);
  ASSERT_EQ(comp_instance->component_instances[2]->core_module_instances[2]->e->functions[0].import_module_inst, comp_instance->component_instances[2]->core_functions[0]->module_instance);

  WASMModuleInstance **defined_core_instances = comp_instance->component_instances[2]->defined_core_instances;
  uint32 defined_core_instances_count = comp_instance->component_instances[2]->defined_core_instances_count;
  WASMComponentInstance **defined_instances = comp_instance->defined_instances;
  uint32 defined_instances_count = comp_instance->defined_instances_count;

  // Test deinstantiation
  wasm_component_deinstantiate(comp_instance);
  ASSERT_EQ(defined_core_instances_count, 5);
  for (idx = 0; idx < defined_core_instances_count; idx++) {
    ASSERT_FALSE(defined_core_instances[idx]);
  }
  ASSERT_EQ(defined_instances_count, 2);
  for (idx = 0; idx < defined_instances_count; idx++) {
    ASSERT_FALSE(defined_instances[idx]);
  }
  wasm_runtime_free(component);

}

TEST_F(ComponentInstantiationTest, TestInstantiateWASIImports)
{
  bool status;
  bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

  const char *path = nullptr;
  unsigned char *buf = nullptr;
  char file_name[] = "hello_wasi.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component);

  // Test component is instantiated
  WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  // Test canonical options information is filled corectly
  ASSERT_EQ(comp_instance->core_functions_count, 42);
  ASSERT_TRUE(comp_instance->core_functions[40]->canon_options);
  ASSERT_TRUE(comp_instance->core_functions[40]->canon_options->lift_lower_opts->lift_opts->memory);

  // Test core instance link to parent instance is filled
  ASSERT_EQ(comp_instance->core_module_instances[2]->comp_instance, comp_instance );

  // Test resource count is filled correctly
  ASSERT_EQ(comp_instance->resources_count, 4);

  wasm_component_deinstantiate(comp_instance);

  char file_name_2[] = "wasip2_tcp_server.wasm";
  component = load_component_from_candidates(file_name_2);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";
  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));

  ASSERT_TRUE(comp_instance);
  wasm_component_deinstantiate(comp_instance);

  char file_name_3[] = "wasi_server.wasm";
  component = load_component_from_candidates(file_name_3);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";
  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));

  // West wasi functions are found correctly

  ASSERT_EQ(comp_instance->component_instances[1]->types[0]->type, COMPONENT_VAL_TYPE_RESOURCE_SYNC);
  ASSERT_FALSE(strcmp(comp_instance->component_instances[1]->types[0]->type_specific.resource->name, "output-stream"));
  ASSERT_FALSE(strcmp(comp_instance->component_instances[1]->types[0]->type_specific.resource->interface_name, "wasi:io/streams@0.2.4"));
  // Test resource drop method is found correctly
  ASSERT_TRUE(comp_instance->component_instances[1]->types[0]->type_specific.resource->drop_method);
  // Test wasi function is found correctly
  ASSERT_TRUE(comp_instance->component_instances[1]->functions[0]->core_func->u.func_import->func_ptr_linked );

  ASSERT_TRUE(comp_instance);
  wasm_component_deinstantiate(comp_instance);
}

TEST_F(ComponentInstantiationTest, TestModuleSwitchExecution)
{
  bool status;
  bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

  const char *path = nullptr;
  unsigned char *buf = nullptr;
  char file_name[] = "surface_and_geometry_0_2_0.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component);

  // Test component is instantiated
  WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  uint32 argc1 = 0;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 10);
  ASSERT_TRUE(argv1 != NULL);
  status = wasm_component_application_execute_func_ex(comp_instance, (char *)"circle-area({x:0.0,y:0.0},{x:2.0,y:0.0})", &argc1, &argv1);
  ASSERT_TRUE(status);

}

TEST_F(ComponentInstantiationTest, TestTypesInstantiation)
{
  bool status;
  bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

  const char *path = nullptr;
  unsigned char *buf = nullptr;

  char file_name[] = "complex.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component);

  // Test component is instantiated
  WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  char file_name_2[] = "processor_and_logging_merged_wac_plug.wasm";
  WASMComponent *component_2 = load_component_from_candidates(file_name_2);
  ASSERT_NE(component_2, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component_2);

  // Test component is instantiated
  WASMComponentInstance *comp_instance_2 = wasm_component_instantiate_internal(component_2, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance_2);

  // Test record type instantiation
  ASSERT_EQ(comp_instance->types[16]->type, COMPONENT_VAL_TYPE_RECORD);
  ASSERT_EQ(comp_instance->types[16]->type_specific.record->count, 2);
  ASSERT_EQ(comp_instance->types[16]->type_specific.record->fields[0].type->type_specific.primval, WASM_COMP_PRIMVAL_STRING);
  ASSERT_FALSE(strcmp(comp_instance->types[16]->type_specific.record->fields[0].label->name, "name"));
  ASSERT_EQ(comp_instance->types[16]->type_specific.record->fields[1].type->type_specific.primval, WASM_COMP_PRIMVAL_U32);
  ASSERT_FALSE(strcmp(comp_instance->types[16]->type_specific.record->fields[1].label->name, "age"));

  // Test variant type instantiation
  ASSERT_EQ(comp_instance->types[18]->type, COMPONENT_VAL_TYPE_VARIANT);
  ASSERT_EQ(comp_instance->types[18]->type_specific.record->count, 2);
  ASSERT_EQ(comp_instance->types[18]->type_specific.record->fields[0].type->type_specific.primval, WASM_COMP_PRIMVAL_F32);
  ASSERT_FALSE(strcmp(comp_instance->types[18]->type_specific.record->fields[0].label->name, "circle"));
  ASSERT_EQ(comp_instance->types[18]->type_specific.record->fields[1].type->type, COMPONENT_VAL_TYPE_TUPLE);
  ASSERT_FALSE(strcmp(comp_instance->types[18]->type_specific.record->fields[1].label->name, "rectangle"));

  // Test list type instantiation
  ASSERT_EQ(comp_instance->types[26]->type, COMPONENT_VAL_TYPE_LIST);
  ASSERT_EQ(comp_instance->types[26]->type_specific.list->element_type->type_specific.primval, WASM_COMP_PRIMVAL_S32);

 // Test tuple type instantiation
  ASSERT_EQ(comp_instance->types[17]->type, COMPONENT_VAL_TYPE_TUPLE);
  ASSERT_EQ(comp_instance->types[17]->type_specific.tuple->element_types[0]->type_specific.primval, WASM_COMP_PRIMVAL_F32);
  ASSERT_EQ(comp_instance->types[17]->type_specific.tuple->element_types[1]->type_specific.primval, WASM_COMP_PRIMVAL_F32);

  // Test flags type instantiation
  ASSERT_EQ(comp_instance_2->types[13]->type_specific.instance->types[31]->type , COMPONENT_VAL_TYPE_FLAGS);
  ASSERT_EQ(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->count, 6);
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[0].name, "read"));
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[1].name, "write"));
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[2].name, "file-integrity-sync"));
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[3].name, "data-integrity-sync"));
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[4].name, "requested-write-sync"));
  ASSERT_FALSE(strcmp(comp_instance_2->types[13]->type_specific.instance->types[31]->type_specific.flag->flags[5].name, "mutate-directory"));

  // Test enum type instantiation
  ASSERT_EQ(comp_instance_2->component_instances[13]->types[24]->type, COMPONENT_VAL_TYPE_ENUM);
  ASSERT_EQ(comp_instance_2->component_instances[13]->types[24]->type_specific.enum_type->count, 3);
  ASSERT_FALSE(strcmp(comp_instance_2->component_instances[13]->types[24]->type_specific.enum_type->labels[0].name, "info"));
  ASSERT_FALSE(strcmp(comp_instance_2->component_instances[13]->types[24]->type_specific.enum_type->labels[1].name, "warning"));
  ASSERT_FALSE(strcmp(comp_instance_2->component_instances[13]->types[24]->type_specific.enum_type->labels[2].name, "error"));

  // Test option type instantiation
  ASSERT_EQ(comp_instance->types[34]->type, COMPONENT_VAL_TYPE_OPTION);
  ASSERT_EQ(comp_instance->types[34]->type_specific.option->element_type->type_specific.primval, WASM_COMP_PRIMVAL_STRING);

  // Test result type instantiation
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[8]->type, COMPONENT_VAL_TYPE_RESULT);
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[8]->type_specific.result->error_type->type, COMPONENT_VAL_TYPE_VARIANT);
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[8]->type_specific.result->error_type->type_specific.variant->count, 2);
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[8]->type_specific.result->result_type->type_specific.primval, WASM_COMP_PRIMVAL_U64);

  // Test own type instantiation
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[4]->type, COMPONENT_VAL_TYPE_OWN);
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[4]->type_specific.resource_handle->is_borrow, false);
  ASSERT_FALSE(strcmp(comp_instance->types[2]->type_specific.instance->types[4]->type_specific.resource_handle->resource->name, "error"));

  // Test borrow type instantiation
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[7]->type, COMPONENT_VAL_TYPE_BORROW);
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[7]->type_specific.resource_handle->is_borrow, true);
  ASSERT_FALSE(strcmp(comp_instance->types[2]->type_specific.instance->types[7]->type_specific.resource_handle->resource->name, "output-stream"));

  // Test resource type instantiation
  ASSERT_EQ(comp_instance->types[2]->type_specific.instance->types[2]->type, COMPONENT_VAL_TYPE_RESOURCE_SYNC);
  ASSERT_FALSE(strcmp(comp_instance->types[2]->type_specific.instance->types[2]->type_specific.resource->name , "output-stream"));
  ASSERT_FALSE(strcmp(comp_instance->types[2]->type_specific.instance->types[2]->type_specific.resource->interface_name, "wasi:io/streams@0.2.3"));
  ASSERT_TRUE(comp_instance->types[2]->type_specific.instance->types[2]->type_specific.resource->drop_method);


}

TEST_F(ComponentInstantiationTest, TestInstantiateSmokeTest)
{
  std::vector<std::string> component_files = {
    "complex.wasm",
    "hello_wasi.wasm",
    "hello_wasip1_hacked.component.wasm",
    "logging_service.component.wasm",
    "processor_and_logging_merged_wac_plug.wasm",
    "surface_and_geometry_0_2_0.wasm",
    "surface_and_geometry.wasm",
    "wasi_server.wasm",
    "wasip2_tcp_server.wasm",
    "add.wasm",
    "sampletypes.wasm"
  };

  WASMComponent *component;
  WASMComponentInstance *comp_instance;

  for (uint32 i = 0; i < component_files.size(); i++)
  {
    const char *file_name = component_files[i].c_str();
    component = load_component_from_candidates(file_name);
    ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates: "<< file_name;

    // Test component is instantiated
    printf("Instantiate %s\n", file_name);
    comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
    ASSERT_NE(comp_instance, nullptr) << "Failed to instantiate " << file_name;
    wasm_component_deinstantiate(comp_instance);

  }
}

TEST_F(ComponentInstantiationTest, TestInstantiateCanonFunctions)
{
  char file_name[] = "surface_and_geometry_0_4_0.wasm";
  WASMComponent *component = load_component_from_candidates(file_name);
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  ASSERT_TRUE(component);
  bh_log_set_verbose_level(WASM_LOG_LEVEL_DEBUG);

  // Test component is instantiated
  WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  //Canon function from component 0 core module 0

  ASSERT_TRUE(comp_instance->component_instances[5]->core_functions[1] );
  ASSERT_TRUE(comp_instance->component_instances[5]->core_functions[1]->is_canon_func );
  ASSERT_EQ(comp_instance->component_instances[5]->core_functions[1]->canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[5]->core_functions[1]->resource, comp_instance->component_instances[5]->types[8]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[0].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[0].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[0].resource, comp_instance->component_instances[5]->types[8]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[1].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[1].canon_type, WASM_COMP_CANON_RESOURCE_NEW);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[1].resource, comp_instance->component_instances[5]->types[8]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[2].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[2].canon_type, WASM_COMP_CANON_RESOURCE_REP);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[2].resource, comp_instance->component_instances[5]->types[8]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[3].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[3].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[3].resource, comp_instance->component_instances[5]->types[1]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[4].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[4].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[5]->core_module_instances[7]->e->functions[4].resource, comp_instance->component_instances[5]->types[5]->type_specific.resource);

  //Canon function from component 1 core module 0

  ASSERT_TRUE(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[3].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[3].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[3].resource, comp_instance->component_instances[7]->types[9]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[7].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[7].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[7].resource, comp_instance->component_instances[7]->types[10]->type_specific.resource);

  ASSERT_TRUE(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[8].is_canon_func);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[8].canon_type, WASM_COMP_CANON_RESOURCE_DROP);
  ASSERT_EQ(comp_instance->component_instances[7]->core_module_instances[7]->e->functions[8].resource, comp_instance->component_instances[7]->types[11]->type_specific.resource);

}
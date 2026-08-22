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
#include "../component-instantiation/helpers.h"
extern "C" {
#include "wasm_component_runtime.h"
#include "wasm_component_host_resource.h"
#include "wasi_p2_sockets.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include "wasm_component_canonical.h"

}

class WasiP2ClocksWrapperTest : public testing::Test
{
  public:
    WasiP2ClocksWrapperTest() {}
    ~WasiP2ClocksWrapperTest() {}
    RuntimeInitArgs init_args;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    WASMComponentInstance *comp_instance;
    libc_wasi_parse_context_t parse_ctx;

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
      bool success = instantiate_host_resource_table();
      ASSERT_TRUE(success);

      bh_log_set_verbose_level(WASM_LOG_LEVEL_WARNING);

      WASMComponent *component = LoadfromCandidates("test_clocks_comp.wasm");
      ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

      libc_wasi_set_default_options(&parse_ctx);
      libc_component_wasi_init(component, 0, NULL, &parse_ctx);
      
      comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
      ASSERT_TRUE(comp_instance);

      bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

      destroy_resolve_streams_map();

      printf("Ending setup\n");
    }

    virtual void TearDown() {
      printf("Starting teardown\n");
      destroy_host_resource_table();
        if (runtime_init) {
          printf("Starting to destroy runtime\n");
          wasm_runtime_destroy();
          runtime_init = false;
        }
        
        printf("Ending teardown\n");
    }

    WASMComponent* LoadfromCandidates(const char *file_name) { 
      return load_component_from_candidates_internal(file_name, "libc-wasi-p2");
    }

};

// Clocks

TEST_F(WasiP2ClocksWrapperTest, test_call_monotonic_clock_now)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-monotonic-clock-now()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2ClocksWrapperTest, test_call_monotonic_clock_resolution)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-monotonic-clock-resolution()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2ClocksWrapperTest, test_call_monotonic_clock_subscribe_instant)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-monotonic-clock-subscribe-instant()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2ClocksWrapperTest, test_call_monotonic_clock_subscribe_duration)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-monotonic-clock-subscribe-duration()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2ClocksWrapperTest, test_call_wall_clock_now)
{

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char*)"call-wall-clock-now()"));
  WASMComponentTypeInstance *ret_type = comp_instance->functions[4]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[7]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value; 
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_TRUE(loaded_value->type == COMPONENT_VAL_TYPE_RECORD);
  ASSERT_TRUE(loaded_value->value.record_value.size == 2);
  ASSERT_STREQ(loaded_value->value.record_value.fields[0].key, "seconds");
  ASSERT_TRUE(loaded_value->value.record_value.fields[0].value);
  ASSERT_STREQ(loaded_value->value.record_value.fields[1].key, "nanoseconds");
  ASSERT_TRUE(loaded_value->value.record_value.fields[1].value);

}

TEST_F(WasiP2ClocksWrapperTest, test_call_wall_clock_resolution)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-wall-clock-resolution()"));
  WASMComponentTypeInstance *ret_type = comp_instance->functions[5]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[8]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value; 
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_TRUE(loaded_value->type == COMPONENT_VAL_TYPE_RECORD);
  ASSERT_TRUE(loaded_value->value.record_value.size == 2);
  ASSERT_STREQ(loaded_value->value.record_value.fields[0].key, "seconds");
  ASSERT_TRUE(loaded_value->value.record_value.fields[0].value);
  ASSERT_STREQ(loaded_value->value.record_value.fields[1].key, "nanoseconds");
  ASSERT_TRUE(loaded_value->value.record_value.fields[1].value);
}
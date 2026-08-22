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
#include "wasm_component_canonical.h"
#include "wasi_p2_types.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
}

class WasiP2IoWrapperTest : public testing::Test
{
  public:
    WasiP2IoWrapperTest() {}
    ~WasiP2IoWrapperTest() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;
    libc_wasi_parse_context_t parse_ctx;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    WASMComponentInstance *comp_instance;

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

      char cwd[PATH_MAX];
      getcwd(cwd, sizeof(cwd));
      const char *substr = "wasm-micro-runtime";
      /* Last occurrence: hosted CI nests the checkout as
         .../wasm-micro-runtime/wasm-micro-runtime, and the first match
         truncates the path prefix one directory short. */
      char *pos = NULL;
      for (char *p = strstr(cwd, substr); p; p = strstr(p + 1, substr))
          pos = p;
      ASSERT_TRUE(pos != NULL) << "Could not find 'wasm-micro-runtime' in cwd";
      size_t prefix_len = (pos - cwd) + strlen(substr);
      char path[PATH_MAX] = "";
      strncat(path, cwd, prefix_len);
      strcat(path, "/tests/unit/libc-wasi-p2/test_dir/test_input.txt");
      
      FILE *input_file = freopen(path, "r", stdin);
      ASSERT_TRUE(input_file);

      bh_log_set_verbose_level(WASM_LOG_LEVEL_WARNING);

      WASMComponent *component = LoadfromCandidates("test_io_comp.wasm");
      ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

      libc_wasi_set_default_options(&parse_ctx);
      libc_component_wasi_init(component, 0, NULL, &parse_ctx);
      
      comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
      ASSERT_TRUE(comp_instance);

      bh_log_set_verbose_level(WASM_LOG_LEVEL_VERBOSE);

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

    void test_function_execution(const char *binary_name, const char *func_name) {
      WASMComponent *component = LoadfromCandidates(binary_name);
      ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";
      
      WASMComponentInstance *comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
      ASSERT_TRUE(comp_instance);

      std::string func_name_args = std::string(func_name) + "()";
      bool status = wasm_component_application_execute_func(comp_instance, (char *)func_name_args.c_str());
      ASSERT_TRUE(status);
    }
};

// Input Stream

TEST_F(WasiP2IoWrapperTest, test_call_read)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-read()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[7]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[23]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_blocking_read)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-read()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[8]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[24]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_call_skip)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-skip()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[9]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[25]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_call_blocking_skip)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-skip()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[10]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[26]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_call_subscribe_in)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-subscribe-in()"));
}

// Output stream

TEST_F(WasiP2IoWrapperTest, test_check_write)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-check-write()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[11]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[27]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_write)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-write()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[12]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[28]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_blocking_write_and_flush)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-write-and-flush()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[13]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[29]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_flush)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-flush()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[14]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[30]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_blocking_flush)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-flush()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[15]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[31]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_subscribe_out)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-subscribe-out()"));
}

TEST_F(WasiP2IoWrapperTest, test_write_zeroes)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-write-zeroes()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[16]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[32]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_blockin_write_zeroes_and_flush)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-write-zeroes-and-flush()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[17]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[33]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_call_splice)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-splice()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[18]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[34]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2IoWrapperTest, test_blocking_splice)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-blocking-splice()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[19]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[35]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

// Error

TEST_F(WasiP2IoWrapperTest, test_to_debug_string)
{
  // redirect stdin to write only file in order to cause input-stream.read to return last-operation-failed and generate an error resource
  char cwd[PATH_MAX];
  getcwd(cwd, sizeof(cwd));
  const char *substr = "wasm-micro-runtime";
  /* Last occurrence: hosted CI nests the checkout as
     .../wasm-micro-runtime/wasm-micro-runtime. */
  char *pos = NULL;
  for (char *p = strstr(cwd, substr); p; p = strstr(p + 1, substr))
      pos = p;
  ASSERT_TRUE(pos != NULL) << "Could not find 'wasm-micro-runtime' in cwd";
  size_t prefix_len = (pos - cwd) + strlen(substr);
  char path[PATH_MAX] = "";
  strncat(path, cwd, prefix_len);
  strcat(path, "/tests/unit/libc-wasi-p2/test_dir/test_output.txt");
  
  FILE *input_file = freopen(path, "w", stdin);
  ASSERT_TRUE(input_file);
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-to-debug-string()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[20]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[36]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);

}

// Poll

TEST_F(WasiP2IoWrapperTest, test_block)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-block()"));
}

TEST_F(WasiP2IoWrapperTest, test_ready)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-ready()"));
}

TEST_F(WasiP2IoWrapperTest, test_poll)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-poll()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[21]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[37]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}
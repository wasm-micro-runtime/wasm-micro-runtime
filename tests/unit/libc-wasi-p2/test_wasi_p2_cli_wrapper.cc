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
#include "wasi_p2_sockets.h"
#include "../../../product-mini/platforms/common/libc_wasi.h"
}

class WasiP2CliWrapperTest : public testing::Test
{
  public:
    WasiP2CliWrapperTest() {}
    ~WasiP2CliWrapperTest() {}
    RuntimeInitArgs init_args;

    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    WASMComponentInstance *comp_instance;
    libc_wasi_parse_context_t parse_ctx;

    char *argv[2] = {(char *)"arg1", (char *)"arg2"};

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
      libc_wasi_set_default_options(&parse_ctx);

      parse_ctx.env_list[0] = "MY_VAR=hello";
      parse_ctx.env_list[1] = "ANOTHER=world";
      parse_ctx.env_list_size = 2;

      WASMComponent *component = LoadfromCandidates("test_cli_comp.wasm");
      ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";
      libc_component_wasi_init(component, 2, argv, &parse_ctx);
      
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

// Cli

TEST_F(WasiP2CliWrapperTest, test_call_get_environment)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-environment()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[4]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[11]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_TRUE(loaded_value->value.list_value.size);
  ASSERT_TRUE(loaded_value->value.list_value.elems[0]->value.tuple_value.elems[0]);

}

TEST_F(WasiP2CliWrapperTest, test_call_get_arguments)
{ 
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-arguments()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[5]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[12]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_EQ(loaded_value->value.list_value.size, 2);
}

TEST_F(WasiP2CliWrapperTest, test_call_initial_cwd)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance,  (char *)"call-initial-cwd()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[6]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[13]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_TRUE(loaded_value->value.option_value.optional_elem);
}

TEST_F(WasiP2CliWrapperTest, test_call_get_stdin)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char *)"call-get-stdin()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);  // index rep
}

TEST_F(WasiP2CliWrapperTest, test_call_get_stdout)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char *)"call-get-stdout()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);  // index rep
}

TEST_F(WasiP2CliWrapperTest, test_call_get_stderr)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char *)"call-get-stderr()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);  // index rep
}

TEST_F(WasiP2CliWrapperTest, test_call_get_terminal_stdin)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-terminal-stdin()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[7]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[14]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);

  // For normal test stdin is redirected to file, causing isatty() to fail
  // For debug execution, this will pass
  if (isatty(fileno(stdin))) {
    ASSERT_TRUE(loaded_value->value.option_value.optional_elem);
  }
  else {
    ASSERT_FALSE(loaded_value->value.option_value.optional_elem);
  }
}

TEST_F(WasiP2CliWrapperTest, test_call_get_terminal_stdout)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-terminal-stdout()"));
  WASMComponentTypeInstance *ret_type = comp_instance->functions[8]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[15]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  
  // For normal test stdout is redirected to file, causing isatty() to fail
  // For debug execution, this will pass
  if (isatty(fileno(stdout))) {
    ASSERT_TRUE(loaded_value->value.option_value.optional_elem);
  }
  else {
    ASSERT_FALSE(loaded_value->value.option_value.optional_elem);
  }
}

TEST_F(WasiP2CliWrapperTest, test_call_get_terminal_stderr)
{

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-terminal-stdout()"));
  WASMComponentTypeInstance *ret_type = comp_instance->functions[9]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[16]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);

  // For normal test stderr is redirected to file, causing isatty() to fail
  // For debug execution, this will pass
  if (isatty(fileno(stderr))) {
    ASSERT_TRUE(loaded_value->value.option_value.optional_elem);
  }
  else {
    ASSERT_FALSE(loaded_value->value.option_value.optional_elem);
  }
}

TEST_F(WasiP2CliWrapperTest, test_call_exit)
{
  pid_t pid = fork();
  ASSERT_NE(pid, -1);

  if (pid == 0) { // child process
      ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-exit()"));
      // This should not be reached
      exit(127);
  }
  else { // parent process
      int status;
      waitpid(pid, &status, 0);
      ASSERT_TRUE(WIFEXITED(status));
      ASSERT_EQ(WEXITSTATUS(status), 0);
  }
}
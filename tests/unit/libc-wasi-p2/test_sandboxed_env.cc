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
#include "wasm_component_canonical.h"
#include "wasm_component_host_resource.h"
#include "wasi_p2_sockets.h"
}

extern "C" {
#include "../../../product-mini/platforms/common/libc_wasi.h"
#include "../../libraries/libc-wasi-p2/wasi_p2_cli.h"
}

class CanonSandboxedEnv : public testing::Test
{
  public:
    CanonSandboxedEnv() {}
    ~CanonSandboxedEnv() {}
    RuntimeInitArgs init_args;
    unsigned char *component_raw = NULL;


    char error_buf[128];
    char global_heap_buf[HEAP_SIZE]; // 100 MB

    bool runtime_init = false;
    WASMComponentInstance *comp_instance;
    libc_wasi_parse_context_t parse_ctx;

    // WASIContext *wasi_ctx;
    char test_dir[128];

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

  //  Sandbox environment flags (as of wasmtime implementation, -X == not yet supprted):
  //                              cli[=y|n] -- Enable support for WASI CLI APIs, including filesystems, sockets, clocks, and random.
  // X             cli-exit-with-code[=y|n] -- Enable WASI APIs marked as: @unstable(feature = cli-exit-with-code)
  //                          common[=y|n] -- Deprecated alias for `cli`
  // X                             nn[=y|n] -- Enable support for WASI neural network imports (experimental)
  // X                        threads[=y|n] -- Enable support for WASI threading imports (experimental). Implies preview2=false.
  // X                           http[=y|n] -- Enable support for WASI HTTP imports
  // X   http-outgoing-body-buffer-chunks=N -- Number of distinct write calls to the outgoing body's output-stream that the implementation will buffer. Default: 1.
  // X      http-outgoing-body-chunk-size=N -- Maximum size allowed in a write call to the outgoing body's output-stream. Default: 1024 * 1024.
  // X                         config[=y|n] -- Enable support for WASI config imports (experimental)
  // X                       keyvalue[=y|n] -- Enable support for WASI key-value imports (experimental)
  // X                       listenfd[=y|n] -- Inherit environment variables and file descriptors following the systemd listen fd specification (UNIX only) (legacy wasip1 implementation only)
  // X                        tcplisten=val -- Grant access to the given TCP listen socket (experimental, legacy wasip1 implementation only)
  // X                            tls[=y|n] -- Enable support for WASI TLS (Transport Layer Security) imports (experimental)
  // X                       preview2[=y|n] -- Implement WASI Preview1 using new Preview2 implementation (true, default) or legacy implementation (false)
  // X             nn-graph=<format>::<dir> -- Pre-load machine learning graphs (i.e., models) for use by wasi-nn.
  //                  inherit-network[=y|n] -- Flag for WASI preview2 to inherit the host's network within the guest so it has full access to all addresses/ports/etc.
  //             allow-ip-name-lookup[=y|n] -- Indicates whether `wasi:sockets/ip-name-lookup` is enabled or not.
  //                              tcp[=y|n] -- Indicates whether `wasi:sockets` TCP support is enabled or not.
  //                              udp[=y|n] -- Indicates whether `wasi:sockets` UDP support is enabled or not.
  // X             network-error-code[=y|n] -- Enable WASI APIs marked as: @unstable(feature = network-error-code)
  // X                       preview0[=y|n] -- Allows imports from the `wasi_unstable` core wasm module.
  //                      inherit-env[=y|n] -- Inherit all environment variables from the parent process.
  // X              config-var=<name>=<val> -- Pass a wasi config variable to the program.
  // X keyvalue-in-memory-data=<name>=<val> -- Preset data for the In-Memory provider of WASI key-value API.
  // X                             p3[=y|n] -- Enable support for WASIp3 APIs.

TEST_F(CanonSandboxedEnv, test_option_cli_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("cli=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_cli_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->cli, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-terminal-stdin()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  ASSERT_EQ(ret[0] , 0);     // none
}

TEST_F(CanonSandboxedEnv, test_option_common_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("common=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_cli_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->common, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-terminal-stdin()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  ASSERT_EQ(ret[0] , 0);     // none
}

TEST_F(CanonSandboxedEnv, test_option_ip_name_lookup_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("allow-ip-name-lookup=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_sockets_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->allow_ip_name_lookup, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-ip-lookup-resolve-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  ASSERT_EQ(ret[0] , 1);     // error
  ASSERT_EQ(ret[1], WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
}

TEST_F(CanonSandboxedEnv, test_option_inherit_network_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("inherit-network=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_sockets_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->inherit_network, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-ip-lookup-resolve-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  ASSERT_EQ(ret[0] , 1);     // error
  ASSERT_EQ(ret[1], WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
}

TEST_F(CanonSandboxedEnv, test_option_tcp_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("tcp=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_sockets_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->tcp, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-shutdown()"));
    WASMComponentTypeInstance *ret_type = comp_instance->functions[49]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[93]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_TRUE(loaded_value->value.result_value.is_err);
}

TEST_F(CanonSandboxedEnv, test_option_udp_off)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("udp=n", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_sockets_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->udp, 0);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-create-udp-socket()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  ASSERT_EQ(ret[0] , 1);     // error
  ASSERT_EQ(ret[1], WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
}

TEST_F(CanonSandboxedEnv, test_option_inherit_env)
{
  libc_wasi_set_default_options(&parse_ctx);

  bool status = libc_wasi_parse_options("inherit-env=y", &parse_ctx);
  ASSERT_TRUE(status == LIBC_WASI_PARSE_RESULT_OK);

  WASMComponent *component = LoadfromCandidates("test_cli_comp.wasm");
  ASSERT_NE(component, nullptr) << "Failed to load/parse component from candidates.";

  libc_component_wasi_init(component, 0, NULL, &parse_ctx);

  comp_instance = wasm_component_instantiate_internal(component, NULL, error_buf, sizeof(error_buf));
  ASSERT_TRUE(comp_instance);

  WASIContext *wasi_ctx = wasm_runtime_get_wasi_ctx((WASMModuleInstanceCommon *)comp_instance->core_module_instances[9]);
  ASSERT_TRUE(wasi_ctx->wasi_options);
  ASSERT_EQ(wasi_ctx->wasi_options->inherit_env, 1);

  uint32_t host_env_count;
  wasi_tuple_string_string_t *host_env = wasi_cli_get_environment(&host_env_count);

  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-get-environment()"));


  WASMComponentTypeInstance *ret_type = comp_instance->functions[4]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[11]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);
  ASSERT_TRUE(load_result);
  ASSERT_EQ(loaded_value->value.list_value.size, host_env_count);

}

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

class WasiP2SocketsWrapperTest : public testing::Test
{
  public:
    WasiP2SocketsWrapperTest() {}
    ~WasiP2SocketsWrapperTest() {}
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

      bh_log_set_verbose_level(WASM_LOG_LEVEL_WARNING);

      WASMComponent *component = LoadfromCandidates("test_sockets_comp.wasm");
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

// Instance network
TEST_F(WasiP2SocketsWrapperTest, test_call_instance_network)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-instance-network()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0]);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_create_udp_socket)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-create-udp-socket()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[10]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[54]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_create_tcp_socket)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-create-tcp-socket()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[10]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[54]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

// UDP

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_start_bind)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-start-bind()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[11]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[55]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_finish_bind)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-finish-bind()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[12]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[56]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_stream)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-stream()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[13]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[57]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_local_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-local-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);

  // Test ip-socket-address result store correctly
  WASMComponentTypeInstance *ret_type = comp_instance->functions[14]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[58]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  ASSERT_FALSE(strcmp(loaded_value->value.result_value.result.ok->value.variant_value.discriminator , "ipv4"));
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_remote_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-remote-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);

  // Test ip-socket-address result store correctly
  WASMComponentTypeInstance *ret_type = comp_instance->functions[15]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[59]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  ASSERT_FALSE(strcmp(loaded_value->value.result_value.result.ok->value.variant_value.discriminator , "ipv4"));
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_address_family)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-udp-address-family()", &argc1, &argv1));
  ASSERT_EQ(argv1[0], 0);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_unicast_hop_limit)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-unicast-hop-limit()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[17]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[61]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_set_unicast_hop_limit)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-set-unicast-hop-limit()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[17]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[61]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_receive_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-receive-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[18]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[62]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_set_receive_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-set-receive-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[19]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[63]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_send_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-send-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[20]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[64]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_subscribe)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-udp-subscribe()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0]);
}

// UDP Incoming datagram tests

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_incoming_datagram_stream_subscribe)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-udp-incoming-stream-subscribe()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0]);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_incoming_datagram_stream_receive)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-incoming-stream-receive()"));
  WASMComponentTypeInstance *ret_type = comp_instance->functions[22]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[66]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

// UDP Outgoing datagram tests

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_outgoing_datagram_stream_subscribe)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-udp-outgoing-datagram-stream-subscribe()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_outgoing_datagram_stream_check_send)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-outgoing-datagram-stream-check-send()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[24]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[68]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_outgoing_datagram_stream_send)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-udp-outgoing-datagram-stream-send()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[23]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[67]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

// TCP tests

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_start_bind)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-start-bind()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[25]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[69]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_finish_bind)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-finish-bind()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[26]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[70]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);

  // The method does nothing, is it ok to just return success? needs to be analysed
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_start_connect)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-start-connect()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[27]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[71]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_finish_connect)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-finish-connect()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[28]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[72]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_start_listen)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-start-listen()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[29]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[73]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_finish_listen)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-finish-listen()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[30]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[74]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_accept)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-accept()"));
  int32 *ret = (int32 *)((char*)comp_instance->core_memories[0]->memory_data + 128);
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[31]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[75]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 128, ret_type, &loaded_value);

  uint8 *ret_8 = (uint8 *)ret;

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  ASSERT_EQ(loaded_value->value.result_value.result.ok->value.tuple_value.size, 3);

}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_local_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-local-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);
  // Test ip-socket-address result store correctly
  WASMComponentTypeInstance *ret_type = comp_instance->functions[32]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[76]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  ASSERT_FALSE(strcmp(loaded_value->value.result_value.result.ok->value.variant_value.discriminator , "ipv4"));
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_remote_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-local-address()"));
  int32 *ret = (int32 *)(comp_instance->core_memories[0]->memory_data);

  // Test ip-socket-address result store correctly
  WASMComponentTypeInstance *ret_type = comp_instance->functions[33]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[77]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
  ASSERT_FALSE(strcmp(loaded_value->value.result_value.result.ok->value.variant_value.discriminator , "ipv4"));
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_is_listening)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-tcp-is-listening()", &argc1, &argv1));
  ASSERT_FALSE(argv1[0]); // socket is not listening
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_address_family)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-tcp-address-family()", &argc1, &argv1));
  ASSERT_EQ(argv1[0], 0); // IPV4 family
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_listen_backlog_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-listen-backlog-size()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[34]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[78]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_keep_alive_enabled)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-keep-alive-enabled()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[35]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[79]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_keep_alive_enabled)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-keep-alive-enabled()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[36]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[80]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_keep_alive_idle_time)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-keep-alive-idle-time()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[37]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[81]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_keep_alive_idle_time)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-keep-alive-idle-time()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[38]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[82]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_keep_alive_interval)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-keep-alive-interval()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[39]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[83]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_keep_alive_interval)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-keep-alive-interval()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[40]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[85]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_keep_alive_count)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-keep-alive-count()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[41]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[85]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_keep_alive_count)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-keep-alive-count()"));
 
  WASMComponentTypeInstance *ret_type = comp_instance->functions[42]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[86]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_hop_limit)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-hop-limit()"));

  WASMComponentTypeInstance *ret_type = comp_instance->functions[43]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[87]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_hop_limit)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-hop-limit()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[44]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[88]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_receive_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-receive-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[45]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[89]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_receive_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-receive-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[46]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[90]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_send_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-send-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[47]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[91]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_set_send_buffer_size)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-set-send-buffer-size()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[48]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[92]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_subscribe)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-tcp-subscribe()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_tcp_shutdown)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-tcp-shutdown()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[49]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[93]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

// IP name lookup

TEST_F(WasiP2SocketsWrapperTest, test_call_ip_lookup_subscribe)
{
  uint32 argc1 = 1;
  uint32 *argv1 = (uint32 *)wasm_runtime_malloc(sizeof(uint32) * 1);
  ASSERT_TRUE(wasm_component_application_execute_func_ex(comp_instance, (char*)"call-ip-lookup-subscribe()", &argc1, &argv1));
  ASSERT_TRUE(argv1[0] > 0);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_ip_lookup_resolve_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-ip-lookup-resolve-address()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[50]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[94]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}

TEST_F(WasiP2SocketsWrapperTest, test_call_udp_lookup_resolve_next_address)
{
  ASSERT_TRUE(wasm_component_application_execute_func(comp_instance, (char *)"call-ip-lookup-resolve-next-address()"));
  
  WASMComponentTypeInstance *ret_type = comp_instance->functions[51]->func_type->results->result;
  LiftLowerContext cx;
  cx.canonical_opts = comp_instance->core_functions[95]->canon_options;
  cx.inst = comp_instance;

  wit_value_t loaded_value;
  bool load_result = load(&cx, 0, ret_type, &loaded_value);

  ASSERT_TRUE(load_result);
  ASSERT_NE(loaded_value, nullptr);
  ASSERT_FALSE(loaded_value->value.result_value.is_err);
}


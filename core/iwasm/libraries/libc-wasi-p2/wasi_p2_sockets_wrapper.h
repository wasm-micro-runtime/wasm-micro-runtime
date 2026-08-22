/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_SOCKETS_WRAPPER_H
#define WASI_P2_SOCKETS_WRAPPER_H

#include "wasm_export.h"
#include "wasi_p2_sockets.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct wasi_socket_context_t {
    uint32_t fd;
    wasi_ip_address_family_t family;
    uint64_t tcp_listen_backlog;
} wasi_socket_context_t;

/* wasi:sockets/instance-network */
uint32_t
wasi_sockets_instance_network_instance_network_wrapper(
    wasm_exec_env_t exec_env);

/* wasi:sockets/ip-name-lookup */
void
wasi_sockets_ip_name_lookup_resolve_addresses_wrapper(wasm_exec_env_t exec_env,
                                                      uint32_t network_handle,
                                                      uint32_t name_ptr,
                                                      uint32_t name_len,
                                                      uint32_t offset_addr);
void
wasi_sockets_ip_name_lookup_resolve_address_stream_resolve_next_address_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, uint32_t offset_addr);
uint32_t
wasi_sockets_ip_name_lookup_resolve_address_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle);

/* wasi:sockets/tcp-create-socket */
void
wasi_sockets_tcp_create_socket_create_tcp_socket_wrapper(
    wasm_exec_env_t exec_env, uint32_t address_family, uint32_t offset_addr);

/* wasi:sockets/tcp */
void
wasi_sockets_tcp_tcp_socket_start_bind_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_finish_bind_wrapper(wasm_exec_env_t exec_env,
                                                uint32_t socket_handle,
                                                uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_start_connect_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_finish_connect_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_start_listen_wrapper(wasm_exec_env_t exec_env,
                                                 uint32_t socket_handle,
                                                 uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_finish_listen_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_accept_wrapper(wasm_exec_env_t exec_env,
                                           uint32_t socket_handle,
                                           uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_local_address_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_remote_address_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr);
uint32_t
wasi_sockets_tcp_tcp_socket_is_listening_wrapper(wasm_exec_env_t exec_env,
                                                 uint32_t socket_handle);
uint32_t
wasi_sockets_tcp_tcp_socket_address_family_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle);
void
wasi_sockets_tcp_tcp_socket_set_listen_backlog_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_keep_alive_enabled_wrapper(wasm_exec_env_t exec_env,
                                                       uint32_t socket_handle,
                                                       uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_enabled_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_keep_alive_idle_time_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_idle_time_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_keep_alive_interval_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_interval_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_keep_alive_count_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_keep_alive_count_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle,
                                              uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t value,
                                                  uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_send_buffer_size_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr);
void
wasi_sockets_tcp_tcp_socket_set_send_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
uint32_t
wasi_sockets_tcp_tcp_socket_subscribe_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle);
void
wasi_sockets_tcp_tcp_socket_shutdown_wrapper(wasm_exec_env_t exec_env,
                                             uint32_t socket_handle,
                                             uint32_t shutdown_type,
                                             uint32_t offset_addr);

/* wasi:sockets/udp-create-socket */
void
wasi_sockets_udp_create_socket_create_udp_socket_wrapper(
    wasm_exec_env_t exec_env, uint32_t address_family, uint32_t offset_addr);

/* wasi:sockets/udp */
void
wasi_sockets_udp_udp_socket_start_bind_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t network_handle,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_finish_bind_wrapper(wasm_exec_env_t exec_env,
                                                uint32_t socket_handle,
                                                uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_stream_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t remote_addr_tag,
    uint32_t addr_tag, uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3,
    uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7, uint32_t p8,
    uint32_t p9, uint32_t p10, uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_local_address_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t socket_handle,
                                                  uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_remote_address_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle,
                                                   uint32_t offset_addr);
uint32_t
wasi_sockets_udp_udp_socket_address_family_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t socket_handle);
void
wasi_sockets_udp_udp_socket_unicast_hop_limit_wrapper(wasm_exec_env_t exec_env,
                                                      uint32_t socket_handle,
                                                      uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_set_unicast_hop_limit_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t value,
    uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_set_receive_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_send_buffer_size_wrapper(wasm_exec_env_t exec_env,
                                                     uint32_t socket_handle,
                                                     uint32_t offset_addr);
void
wasi_sockets_udp_udp_socket_set_send_buffer_size_wrapper(
    wasm_exec_env_t exec_env, uint32_t socket_handle, int64_t value,
    uint32_t offset_addr);
uint32_t
wasi_sockets_udp_udp_socket_subscribe_wrapper(wasm_exec_env_t exec_env,
                                              uint32_t socket_handle);
void
wasi_sockets_udp_incoming_datagram_stream_receive_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, int64_t max_results,
    uint32_t offset_addr);
uint32_t
wasi_sockets_udp_incoming_datagram_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle);
void
wasi_sockets_udp_outgoing_datagram_stream_check_send_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle, uint32_t offset_addr);
void
wasi_sockets_udp_outgoing_datagram_stream_send_wrapper(wasm_exec_env_t exec_env,
                                                       uint32_t stream_handle,
                                                       uint32_t datagrams_ptr,
                                                       uint32_t datagrams_len,
                                                       uint32_t offset_addr);
uint32_t
wasi_sockets_udp_outgoing_datagram_stream_subscribe_wrapper(
    wasm_exec_env_t exec_env, uint32_t stream_handle);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_SOCKETS_WRAPPER_H */
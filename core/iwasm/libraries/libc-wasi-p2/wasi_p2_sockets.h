/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_SOCKETS_H
#define WASI_P2_SOCKETS_H

#include <stdint.h>
#include <stdbool.h>

#include "wasi_p2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void
wasi_p2_sockets_cleanup(void);
typedef int32_t wasi_network_t;
typedef int32_t wasi_tcp_socket_t;
typedef int32_t wasi_udp_socket_t;
typedef int32_t wasi_resolve_address_stream_t;
typedef int32_t wasi_incoming_datagram_stream_t;
typedef int32_t wasi_outgoing_datagram_stream_t;
typedef enum wasi_ip_address_family_t {
    WASI_IP_ADDRESS_FAMILY_IPV4,
    WASI_IP_ADDRESS_FAMILY_IPV6,
} wasi_ip_address_family_t;
typedef struct wasi_ipv4_address_t {
    uint8_t f0;
    uint8_t f1;
    uint8_t f2;
    uint8_t f3;
} wasi_ipv4_address_t;
typedef struct wasi_ipv6_address_t {
    uint16_t f0;
    uint16_t f1;
    uint16_t f2;
    uint16_t f3;
    uint16_t f4;
    uint16_t f5;
    uint16_t f6;
    uint16_t f7;
} wasi_ipv6_address_t;
typedef enum wasi_ip_address_tag_t {
    WASI_IP_ADDRESS_TAG_IPV4,
    WASI_IP_ADDRESS_TAG_IPV6,
} wasi_ip_address_tag_t;
typedef struct wasi_ipv4_socket_address_t {
    uint16_t port;
    wasi_ipv4_address_t address;
} wasi_ipv4_socket_address_t;
typedef struct wasi_ipv6_socket_address_t {
    uint16_t port;
    uint32_t flow_info;
    wasi_ipv6_address_t address;
    uint32_t scope_id;
} wasi_ipv6_socket_address_t;
typedef enum wasi_ip_socket_address_tag_t {
    WASI_IP_SOCKET_ADDRESS_TAG_IPV4,
    WASI_IP_SOCKET_ADDRESS_TAG_IPV6,
} wasi_ip_socket_address_tag_t;
typedef struct wasi_ip_socket_address_t {
    wasi_ip_socket_address_tag_t tag;
    union {
        wasi_ipv4_socket_address_t ipv4;
        wasi_ipv6_socket_address_t ipv6;
    } val;
} wasi_ip_socket_address_t;
typedef enum wasi_shutdown_type_t {
    WASI_SHUTDOWN_TYPE_RECEIVE,
    WASI_SHUTDOWN_TYPE_SEND,
    WASI_SHUTDOWN_TYPE_BOTH,
} wasi_shutdown_type_t;
typedef struct wasi_incoming_datagram_t {
    uint8_t *data;
    uint64_t data_len;
    wasi_ip_socket_address_t remote_address;
} wasi_incoming_datagram_t;
typedef struct wasi_outgoing_datagram_t {
    uint8_t *data;
    uint64_t data_len;
    wasi_ip_socket_address_t *remote_address;
} wasi_outgoing_datagram_t;
wasi_network_t
wasi_sockets_instance_network(void);
void
wasi_sockets_resolve_addresses(wasi_network_t network, const char *name,
                               wasi_resolve_address_stream_t *ret, int *err);
void
wasi_sockets_resolve_next_address(wasi_resolve_address_stream_t stream,
                                  wasi_ip_address_t *ret, bool *is_some,
                                  int *err);
wasi_pollable_context_t
wasi_sockets_resolve_address_stream_subscribe(
    wasi_resolve_address_stream_t stream);
void
wasi_sockets_create_tcp_socket(wasi_ip_address_family_t address_family,
                               wasi_tcp_socket_t *ret, int *err);
int
wasi_sockets_tcp_start_bind(wasi_tcp_socket_t socket, wasi_network_t network,
                            const wasi_ip_socket_address_t *local_address);
int
wasi_sockets_tcp_finish_bind(wasi_tcp_socket_t socket);
int
wasi_sockets_tcp_start_connect(wasi_tcp_socket_t socket, wasi_network_t network,
                               const wasi_ip_socket_address_t *remote_address);
void
wasi_sockets_tcp_finish_connect(wasi_tcp_socket_t socket,
                                wasi_input_stream_t *input_stream,
                                wasi_output_stream_t *output_stream, int *err);
int
wasi_sockets_tcp_start_listen(wasi_tcp_socket_t socket, uint64_t backlog);
int
wasi_sockets_tcp_finish_listen(wasi_tcp_socket_t socket);
void
wasi_sockets_tcp_accept(wasi_tcp_socket_t socket, wasi_tcp_socket_t *ret,
                        wasi_input_stream_t *input_stream,
                        wasi_output_stream_t *output_stream, int *err);
void
wasi_sockets_tcp_local_address(wasi_tcp_socket_t socket,
                               wasi_ip_socket_address_t *ret, int *err);
void
wasi_sockets_tcp_remote_address(wasi_tcp_socket_t socket,
                                wasi_ip_socket_address_t *ret, int *err);
bool
wasi_sockets_tcp_is_listening(wasi_tcp_socket_t socket);
void
wasi_sockets_tcp_keep_alive_enabled(wasi_tcp_socket_t socket, bool *ret,
                                    int *err);
int
wasi_sockets_tcp_set_keep_alive_enabled(wasi_tcp_socket_t socket, bool value);
void
wasi_sockets_tcp_keep_alive_idle_time(wasi_tcp_socket_t socket,
                                      wasi_duration_t *ret, int *err);
int
wasi_sockets_tcp_set_keep_alive_idle_time(wasi_tcp_socket_t socket,
                                          wasi_duration_t value);
void
wasi_sockets_tcp_keep_alive_interval(wasi_tcp_socket_t socket,
                                     wasi_duration_t *ret, int *err);
int
wasi_sockets_tcp_set_keep_alive_interval(wasi_tcp_socket_t socket,
                                         wasi_duration_t value);
void
wasi_sockets_tcp_keep_alive_count(wasi_tcp_socket_t socket, uint32_t *ret,
                                  int *err);
int
wasi_sockets_tcp_set_keep_alive_count(wasi_tcp_socket_t socket, uint32_t value);
void
wasi_sockets_tcp_hop_limit(wasi_tcp_socket_t socket, uint8_t *ret, int *err);
int
wasi_sockets_tcp_set_hop_limit(wasi_tcp_socket_t socket, uint8_t value);
void
wasi_sockets_tcp_receive_buffer_size(wasi_tcp_socket_t socket, uint64_t *ret,
                                     int *err);
int
wasi_sockets_tcp_set_receive_buffer_size(wasi_tcp_socket_t socket,
                                         uint64_t value);
void
wasi_sockets_tcp_send_buffer_size(wasi_tcp_socket_t socket, uint64_t *ret,
                                  int *err);
int
wasi_sockets_tcp_set_send_buffer_size(wasi_tcp_socket_t socket, uint64_t value);

int
wasi_sockets_tcp_shutdown(wasi_tcp_socket_t socket,
                          wasi_shutdown_type_t shutdown_type);
void
wasi_sockets_create_udp_socket(wasi_ip_address_family_t address_family,
                               wasi_udp_socket_t *ret, int *err);
int
wasi_sockets_udp_start_bind(wasi_udp_socket_t socket, wasi_network_t network,
                            const wasi_ip_socket_address_t *local_address);
int
wasi_sockets_udp_finish_bind(wasi_udp_socket_t socket);
void
wasi_sockets_udp_stream(wasi_udp_socket_t socket,
                        const wasi_ip_socket_address_t *remote_address,
                        wasi_incoming_datagram_stream_t *input_stream,
                        wasi_outgoing_datagram_stream_t *output_stream,
                        int *err);
void
wasi_sockets_udp_local_address(wasi_udp_socket_t socket,
                               wasi_ip_socket_address_t *ret, int *err);
void
wasi_sockets_udp_remote_address(wasi_udp_socket_t socket,
                                wasi_ip_socket_address_t *ret, int *err);
void
wasi_sockets_udp_unicast_hop_limit(wasi_udp_socket_t socket, uint8_t *ret,
                                   int *err);
int
wasi_sockets_udp_set_unicast_hop_limit(wasi_udp_socket_t socket, uint8_t value);
void
wasi_sockets_udp_receive_buffer_size(wasi_udp_socket_t socket, uint64_t *ret,
                                     int *err);
int
wasi_sockets_udp_set_receive_buffer_size(wasi_udp_socket_t socket,
                                         uint64_t value);
void
wasi_sockets_udp_send_buffer_size(wasi_udp_socket_t socket, uint64_t *ret,
                                  int *err);
int
wasi_sockets_udp_set_send_buffer_size(wasi_udp_socket_t socket, uint64_t value);

void
wasi_sockets_udp_receive(wasi_incoming_datagram_stream_t stream,
                         uint64_t max_results, wasi_incoming_datagram_t **ret,
                         uint64_t *ret_len, int *err);
void
wasi_sockets_udp_check_send(wasi_outgoing_datagram_stream_t stream,
                            uint64_t *ret, int *err);
void
wasi_sockets_udp_send(wasi_outgoing_datagram_stream_t stream,
                      const wasi_outgoing_datagram_t *datagrams,
                      uint64_t datagrams_len, uint64_t *ret, int *err);

void
destroy_resolve_streams_map();

void
tcp_socket_dtor(void *data);

void
udp_socket_dtor(void *data);

void
tcp_owned_stream_dtor(void *data);

void
udp_datagram_stream_dtor(void *data);

void
wasi_sockets_drop_resolve_stream(uint32_t stream_id);

void
resolve_stream_dtor(void *data);

#ifdef __cplusplus
}
#endif

#endif /* end of _WASI_SOCKETS_H */
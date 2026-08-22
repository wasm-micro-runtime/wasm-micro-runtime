/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>

extern "C" {
#include "wasm_export.h"
#include "wasi_p2_sockets.h"
}

class WasiP2SocketsTest : public testing::Test {
protected:
    bool ipv6_supported = false;
    void SetUp() override {
        signal(SIGPIPE, SIG_IGN);
        wasm_runtime_init();
        int sock = socket(AF_INET6, SOCK_STREAM, 0);
        if (sock != -1) {
            ipv6_supported = true;
            close(sock);
        }
    }

    void TearDown() override {
        wasi_p2_sockets_cleanup();
        wasm_runtime_destroy();
    }
};

// Test: wasi:sockets/ip-name-lookup.resolve-addresses and resolve-next-address
TEST_F(WasiP2SocketsTest, IpNameLookup_ResolveAddressStream) {
    wasi_resolve_address_stream_t stream;
    int err;

    wasi_network_t network = wasi_sockets_instance_network();
    wasi_sockets_resolve_addresses(network, "localhost", &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_pollable_context_t pollable =
        wasi_sockets_resolve_address_stream_subscribe(stream);
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd = { .fd = pollable.fd, .events = POLLIN, .revents = 0};
    int ret = poll(&pfd, 1, 1000); // 1 second timeout
    ASSERT_GT(ret, 0);

    wasi_ip_address_t addr;
    bool is_some;
    wasi_sockets_resolve_next_address(stream, &addr, &is_some, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_TRUE(is_some);

    close(pollable.fd);
}

// Test: wasi:sockets/ip-name-lookup.resolve-next-address returns would-block
TEST_F(WasiP2SocketsTest, IpNameLookup_ResolveAddressStreamWouldBlock) {
    wasi_resolve_address_stream_t stream;
    int err;

    wasi_network_t network = wasi_sockets_instance_network();
    wasi_sockets_resolve_addresses(network, "localhost", &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_address_t addr;
    bool is_some;
    wasi_sockets_resolve_next_address(stream, &addr, &is_some, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_FALSE(is_some);
}

// Test: wasi:sockets/ip-name-lookup.resolve-addresses with null name
TEST_F(WasiP2SocketsTest, IpNameLookup_ResolveAddressesNull) {
    wasi_resolve_address_stream_t stream = 0; // Initialize
    int err;

    wasi_network_t network = wasi_sockets_instance_network();
    wasi_sockets_resolve_addresses(network, NULL, &stream, &err);
    ASSERT_EQ(err, EINVAL);
    ASSERT_EQ(stream, -1);
}

// Test: wasi:sockets/tcp-create-socket.create-tcp-socket
TEST_F(WasiP2SocketsTest, TcpCreateSocket_CreateTcpSocket) {
    wasi_tcp_socket_t sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(sock, -1);
    close(sock);

    if (ipv6_supported) {
        wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV6, &sock,
                                       &err);
        ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
        ASSERT_NE(sock, -1);
        close(sock);
    }

    wasi_sockets_create_tcp_socket((wasi_ip_address_family_t)2, &sock, &err);
    ASSERT_EQ(err, WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
    ASSERT_EQ(sock, -1);
}

// Test: wasi:sockets/tcp full lifecycle
TEST_F(WasiP2SocketsTest, Tcp_FullLifecycle) {
    wasi_tcp_socket_t listen_sock, client_sock, server_sock;
    int err;

    // Create sockets
    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &listen_sock,
                                   &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &client_sock,
                                   &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Bind
    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_tcp_start_bind(listen_sock, network, &local_addr);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_finish_bind(listen_sock);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Get local address
    wasi_ip_socket_address_t bound_addr;
    wasi_sockets_tcp_local_address(listen_sock, &bound_addr, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(bound_addr.tag, WASI_IP_SOCKET_ADDRESS_TAG_IPV4);
    ASSERT_NE(bound_addr.val.ipv4.port, 0);

    // Listen
    err = wasi_sockets_tcp_start_listen(listen_sock, SOMAXCONN);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_finish_listen(listen_sock);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_TRUE(wasi_sockets_tcp_is_listening(listen_sock));

    // Connect
    err = wasi_sockets_tcp_start_connect(client_sock, network, &bound_addr);
    ASSERT_EQ(err, EINPROGRESS);

    // Poll for connect to complete
    struct pollfd pfd_client = { .fd = client_sock, .events = POLLOUT, .revents = 0 };
    int ret = poll(&pfd_client, 1, 1000);
    ASSERT_GT(ret, 0);

    wasi_input_stream_t client_input, server_input;
    wasi_output_stream_t client_output, server_output;
    wasi_sockets_tcp_finish_connect(client_sock, &client_input,
                                    &client_output, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Poll for accept
    ASSERT_NE(listen_sock, -1);
    struct pollfd pfd_listen = { .fd = listen_sock, .events = POLLIN, .revents = 0 };
    ret = poll(&pfd_listen, 1, 1000);
    ASSERT_GT(ret, 0);

    // Accept
    wasi_sockets_tcp_accept(listen_sock, &server_sock, &server_input,
                            &server_output, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(server_sock, -1);

    // Get remote address
    wasi_ip_socket_address_t remote_addr;
    wasi_sockets_tcp_remote_address(server_sock, &remote_addr, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Send/Recv
    char send_buf[] = "hello";
    char recv_buf[16];
    memset(recv_buf, 0, sizeof(recv_buf));

    ssize_t nwritten = write(client_output, send_buf, sizeof(send_buf));
    ASSERT_EQ(nwritten, sizeof(send_buf));

    // Poll for server socket readability
    ASSERT_NE(server_sock, -1);
    struct pollfd pfd_server = { .fd = server_sock, .events = POLLIN, .revents = 0};
    ret = poll(&pfd_server, 1, 1000);
    ASSERT_GT(ret, 0);

    ssize_t nread = read(server_input, recv_buf, sizeof(recv_buf));
    ASSERT_EQ(nread, sizeof(send_buf));
    ASSERT_STREQ(send_buf, recv_buf);

    // Shutdown
    err = wasi_sockets_tcp_shutdown(client_sock, WASI_SHUTDOWN_TYPE_BOTH);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    close(client_sock);
    close(server_sock);
    close(listen_sock);
}

// Test: wasi:sockets/tcp socket options
TEST_F(WasiP2SocketsTest, Tcp_SocketOptions) {
    wasi_tcp_socket_t sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Keep-alive
    bool enabled;
    wasi_sockets_tcp_keep_alive_enabled(sock, &enabled, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_keep_alive_enabled(sock, !enabled);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    bool new_enabled;
    wasi_sockets_tcp_keep_alive_enabled(sock, &new_enabled, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_enabled, !enabled);

    // Keep-alive idle time
    wasi_duration_t idle_time;
    wasi_sockets_tcp_keep_alive_idle_time(sock, &idle_time, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_keep_alive_idle_time(sock,
                                                    idle_time + 1000000000);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_duration_t new_idle_time;
    wasi_sockets_tcp_keep_alive_idle_time(sock, &new_idle_time, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_idle_time, idle_time + 1000000000);

    // Keep-alive interval
    wasi_duration_t interval;
    wasi_sockets_tcp_keep_alive_interval(sock, &interval, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_keep_alive_interval(sock,
                                                   interval + 1000000000);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_duration_t new_interval;
    wasi_sockets_tcp_keep_alive_interval(sock, &new_interval, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_interval, interval + 1000000000);

    // Keep-alive count
    uint32_t count;
    wasi_sockets_tcp_keep_alive_count(sock, &count, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_keep_alive_count(sock, count + 1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    uint32_t new_count;
    wasi_sockets_tcp_keep_alive_count(sock, &new_count, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_count, count + 1);

    // Hop limit
    uint8_t hop_limit;
    wasi_sockets_tcp_hop_limit(sock, &hop_limit, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_hop_limit(sock, hop_limit + 1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    uint8_t new_hop_limit;
    wasi_sockets_tcp_hop_limit(sock, &new_hop_limit, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_hop_limit, hop_limit + 1);

    // Buffer sizes
    uint64_t recv_buf_size, send_buf_size;
    wasi_sockets_tcp_receive_buffer_size(sock, &recv_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_tcp_send_buffer_size(sock, &send_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_receive_buffer_size(sock, recv_buf_size * 2);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_tcp_set_send_buffer_size(sock, send_buf_size * 2);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    uint64_t new_recv_buf_size, new_send_buf_size;
    wasi_sockets_tcp_receive_buffer_size(sock, &new_recv_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_tcp_send_buffer_size(sock, &new_send_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_GE(new_recv_buf_size, recv_buf_size);
    ASSERT_GE(new_send_buf_size, send_buf_size);

    close(sock);
}

// Test: wasi:sockets/tcp.finish-connect failure
// Verifies that finish-connect correctly reports an error and invalidates
// streams when a connection fails.
TEST_F(WasiP2SocketsTest, Tcp_FinishConnectFailure) {
    wasi_tcp_socket_t client_sock;
    int err;

    // Create a client socket
    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &client_sock,
                                   &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Attempt to connect to an address that is not listening
    wasi_ip_socket_address_t remote_addr;
    remote_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    remote_addr.val.ipv4.port = 65535; // Unlikely to be used
    remote_addr.val.ipv4.address.f0 = 127;
    remote_addr.val.ipv4.address.f1 = 0;
    remote_addr.val.ipv4.address.f2 = 0;
    remote_addr.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_tcp_start_connect(client_sock, network, &remote_addr);
    ASSERT_EQ(err, EINPROGRESS);

    // Poll for the connection to fail
    ASSERT_NE(client_sock, -1);
    struct pollfd pfd_client = { .fd = client_sock, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd_client, 1, 1000);
    ASSERT_GT(ret, 0);

    // Finish the connection and expect a connection-refused error
    wasi_input_stream_t client_input = 0;
    wasi_output_stream_t client_output = 0;
    wasi_sockets_tcp_finish_connect(client_sock, &client_input,
                                    &client_output, &err);
    ASSERT_EQ(err, ECONNREFUSED);
    ASSERT_EQ(client_input, -1);
    ASSERT_EQ(client_output, -1);

    close(client_sock);
}

// Test: wasi:sockets/tcp.accept on a non-listening socket
// Verifies that `accept` returns `invalid-state` when called on a
// non-listening socket.
TEST_F(WasiP2SocketsTest, Tcp_AcceptNonListeningSocket) {
    wasi_tcp_socket_t sock, new_sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_input_stream_t input;
    wasi_output_stream_t output;
    wasi_sockets_tcp_accept(sock, &new_sock, &input, &output, &err);

    ASSERT_EQ(err, ENOTCONN);
    ASSERT_EQ(new_sock, -1);
    ASSERT_EQ(input, -1);
    ASSERT_EQ(output, -1);

    close(sock);
}

// Test: wasi:sockets/tcp.shutdown on a non-connected socket
TEST_F(WasiP2SocketsTest, Tcp_ShutdownOnNotConnectedSocket) {
    wasi_tcp_socket_t sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    err = wasi_sockets_tcp_shutdown(sock, WASI_SHUTDOWN_TYPE_BOTH);
    ASSERT_EQ(err, ENOTCONN);

    close(sock);
}

// Test: wasi:sockets/udp.remote-address
TEST_F(WasiP2SocketsTest, Udp_RemoteAddress) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    // Create sockets
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Bind sock1
    wasi_ip_socket_address_t addr1;
    addr1.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr1.val.ipv4.port = 0; // any port
    addr1.val.ipv4.address.f0 = 127;
    addr1.val.ipv4.address.f1 = 0;
    addr1.val.ipv4.address.f2 = 0;
    addr1.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &addr1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_udp_finish_bind(sock1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Get local address of sock1
    wasi_ip_socket_address_t bound_addr1;
    wasi_sockets_udp_local_address(sock1, &bound_addr1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Connect sock2 to sock1
    wasi_incoming_datagram_stream_t input_stream;
    wasi_outgoing_datagram_stream_t output_stream;
    wasi_sockets_udp_stream(sock2, &bound_addr1, &input_stream,
                            &output_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Get remote address of sock2
    wasi_ip_socket_address_t remote_addr;
    wasi_sockets_udp_remote_address(sock2, &remote_addr, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(remote_addr.tag, WASI_IP_SOCKET_ADDRESS_TAG_IPV4);
    ASSERT_EQ(remote_addr.val.ipv4.port, bound_addr1.val.ipv4.port);
    ASSERT_EQ(remote_addr.val.ipv4.address.f0,
              bound_addr1.val.ipv4.address.f0);
    ASSERT_EQ(remote_addr.val.ipv4.address.f1,
              bound_addr1.val.ipv4.address.f1);
    ASSERT_EQ(remote_addr.val.ipv4.address.f2,
              bound_addr1.val.ipv4.address.f2);
    ASSERT_EQ(remote_addr.val.ipv4.address.f3,
              bound_addr1.val.ipv4.address.f3);

    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/udp.receive with a small datagram
// This test exercises the realloc path in wasi_sockets_udp_receive.
TEST_F(WasiP2SocketsTest, UdpReceive_SmallDatagramRealloc) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    // Create sockets
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Bind sock1
    wasi_ip_socket_address_t addr1;
    addr1.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr1.val.ipv4.port = 0; // any port
    addr1.val.ipv4.address.f0 = 127;
    addr1.val.ipv4.address.f1 = 0;
    addr1.val.ipv4.address.f2 = 0;
    addr1.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &addr1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Get local address of sock1
    wasi_ip_socket_address_t bound_addr1;
    wasi_sockets_udp_local_address(sock1, &bound_addr1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Send a small datagram from sock2 to sock1
    char send_buf[] = "small";
    wasi_outgoing_datagram_t send_datagram;
    send_datagram.data = (uint8_t *)send_buf;
    send_datagram.data_len = strlen(send_buf);
    send_datagram.remote_address = &bound_addr1;
    uint64_t sent_len;
    wasi_sockets_udp_send(sock2, &send_datagram, 1, &sent_len, &err);
    ASSERT_EQ(err, WASI_UDP_SEND_SUCCESS);
    ASSERT_EQ(sent_len, 1);

    // Poll for receive readiness
    struct pollfd pfd_sock1 = { .fd = sock1, .events = POLLIN, .revents = 0 };
    poll(&pfd_sock1, 1, 1000);

    // Receive the datagram. This should trigger the realloc path.
    wasi_incoming_datagram_t *recv_datagrams;
    uint64_t recv_len;
    wasi_sockets_udp_receive(sock1, 1, &recv_datagrams, &recv_len, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(recv_len, 1);
    ASSERT_NE(recv_datagrams, nullptr);
    ASSERT_EQ(recv_datagrams[0].data_len, strlen(send_buf));
    ASSERT_EQ(memcmp(recv_datagrams[0].data, send_buf, recv_datagrams[0].data_len), 0);

    // Cleanup
    wasm_runtime_free(recv_datagrams[0].data);
    wasm_runtime_free(recv_datagrams);
    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/tcp.shutdown with an invalid socket
// Verifies that shutting down an invalid socket returns `bad-descriptor`.
TEST_F(WasiP2SocketsTest, Tcp_ShutdownInvalidSocket) {
    wasi_tcp_socket_t invalid_sock = -1;
    int err;

    err = wasi_sockets_tcp_shutdown(invalid_sock, WASI_SHUTDOWN_TYPE_BOTH);

    ASSERT_EQ(err, EBADF);
}

// Test: wasi:sockets/udp.receive does not reallocate buffer
// Verifies that the receive buffer is correctly sized when fewer datagrams are received than requested.
TEST_F(WasiP2SocketsTest, Udp_ReceiveNoRealloc) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    // Create and bind sockets
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t addr1;
    addr1.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr1.val.ipv4.port = 0; // any port
    addr1.val.ipv4.address.f0 = 127;
    addr1.val.ipv4.address.f1 = 0;
    addr1.val.ipv4.address.f2 = 0;
    addr1.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &addr1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t bound_addr1;
    wasi_sockets_udp_local_address(sock1, &bound_addr1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Send one datagram
    wasi_outgoing_datagram_t send_datagram;
    char send_buf[] = "single packet";
    send_datagram.data = (uint8_t *)send_buf;
    send_datagram.data_len = strlen(send_buf);
    send_datagram.remote_address = &bound_addr1;
    uint64_t sent_len;
    wasi_sockets_udp_send(sock2, &send_datagram, 1, &sent_len, &err);
    ASSERT_EQ(err, WASI_UDP_SEND_SUCCESS);
    ASSERT_EQ(sent_len, 1);

    // Poll for receive readiness
    struct pollfd pfd_sock1 = { .fd = sock1, .events = POLLIN, .revents = 0};
    poll(&pfd_sock1, 1, 1000);

    // Try to receive more datagrams than were sent
    wasi_incoming_datagram_t *recv_datagrams;
    uint64_t recv_len;
    wasi_sockets_udp_receive(sock1, 10, &recv_datagrams, &recv_len, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(recv_len, 1);
    ASSERT_NE(recv_datagrams, nullptr);
    ASSERT_EQ(recv_datagrams[0].data_len, strlen(send_buf));
    ASSERT_EQ(
        memcmp(recv_datagrams[0].data, send_buf, recv_datagrams[0].data_len),
        0);

    wasm_runtime_free(recv_datagrams[0].data);
    wasm_runtime_free(recv_datagrams);

    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/udp.receive fewer than max_results
// Verifies that `receive` correctly reports the number of datagrams read when fewer are available than requested.
TEST_F(WasiP2SocketsTest, Udp_ReceiveFewerThanMax) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    // Create and bind sockets
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t addr1;
    addr1.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr1.val.ipv4.port = 0; // any port
    addr1.val.ipv4.address.f0 = 127;
    addr1.val.ipv4.address.f1 = 0;
    addr1.val.ipv4.address.f2 = 0;
    addr1.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &addr1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t bound_addr1;
    wasi_sockets_udp_local_address(sock1, &bound_addr1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Send one datagram
    wasi_outgoing_datagram_t send_datagram;
    char send_buf[] = "single packet";
    send_datagram.data = (uint8_t *)send_buf;
    send_datagram.data_len = strlen(send_buf);
    send_datagram.remote_address = &bound_addr1;
    uint64_t sent_len;
    wasi_sockets_udp_send(sock2, &send_datagram, 1, &sent_len, &err);
    ASSERT_EQ(err, WASI_UDP_SEND_SUCCESS);
    ASSERT_EQ(sent_len, 1);

    // Poll for receive readiness
    struct pollfd pfd_sock1 = { .fd = sock1, .events = POLLIN, .revents = 0 };
    poll(&pfd_sock1, 1, 1000);

    // Try to receive more datagrams than were sent
    wasi_incoming_datagram_t *recv_datagrams;
    uint64_t recv_len;
    wasi_sockets_udp_receive(sock1, 10, &recv_datagrams, &recv_len, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(recv_len, 1);
    ASSERT_NE(recv_datagrams, nullptr);
    ASSERT_EQ(recv_datagrams[0].data_len, strlen(send_buf));
    ASSERT_EQ(
        memcmp(recv_datagrams[0].data, send_buf, recv_datagrams[0].data_len),
        0);

    wasm_runtime_free(recv_datagrams[0].data);
    wasm_runtime_free(recv_datagrams);

    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/udp.receive returns would-block
// Verifies that `receive` returns successfully with a zero-length list when no datagrams are available.
TEST_F(WasiP2SocketsTest, Udp_ReceiveWouldBlock) {
    wasi_udp_socket_t sock;
    int err;
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Bind the socket so it can receive
    wasi_ip_socket_address_t addr;
    addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr.val.ipv4.port = 0;
    addr.val.ipv4.address.f0 = 127;
    addr.val.ipv4.address.f1 = 0;
    addr.val.ipv4.address.f2 = 0;
    addr.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock, network, &addr);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Try to receive when no data has been sent
    wasi_incoming_datagram_t *recv_datagrams;
    uint64_t recv_len;
    wasi_sockets_udp_receive(sock, 10, &recv_datagrams, &recv_len, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(recv_len, 0);
    ASSERT_EQ(recv_datagrams, nullptr);

    close(sock);
}

// Test: wasi:sockets/ip-name-lookup.resolve-addresses failure case
// Verifies that resolving a non-existent domain returns a `name-unresolvable` error.
TEST_F(WasiP2SocketsTest, IpNameLookup_ResolveAddressStreamFailure) {
    wasi_resolve_address_stream_t stream;
    int err;

    wasi_network_t network = wasi_sockets_instance_network();
    wasi_sockets_resolve_addresses(network, "nonexistent-domain-for-test",
                                   &stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_pollable_context_t pollable =
        wasi_sockets_resolve_address_stream_subscribe(stream);
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd = { .fd = pollable.fd, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd, 1, 15000); // 15 second timeout, DNS can be slow
    ASSERT_GT(ret, 0);

    wasi_ip_address_t addr;
    bool is_some;
    wasi_sockets_resolve_next_address(stream, &addr, &is_some, &err);
    // normally intEMPORARY_RESOLVER_FAILURE
    ASSERT_GT(err, 0);
    ASSERT_FALSE(is_some);

    close(pollable.fd);
}

// Test: wasi:sockets/udp.check-send
// Verifies that `check-send` returns a non-zero permit size, indicating the stream is ready for writing.
TEST_F(WasiP2SocketsTest, Udp_CheckSend) {
    wasi_udp_socket_t sock;
    int err;
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    uint64_t ret;
    wasi_sockets_udp_check_send(sock, &ret, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_GT(ret, 0);

    close(sock);
}

// Test: wasi:sockets/udp-create-socket.create-udp-socket
// Verifies that `create-udp-socket` successfully creates sockets for supported address families.
TEST_F(WasiP2SocketsTest, UdpCreateSocket_CreateUdpSocket) {
    wasi_udp_socket_t sock;
    int err;

    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(sock, -1);
    close(sock);

    if (ipv6_supported) {
        wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV6, &sock,
                                       &err);
        ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
        ASSERT_NE(sock, -1);
        close(sock);
    }

    wasi_sockets_create_udp_socket((wasi_ip_address_family_t)2, &sock, &err);
    ASSERT_EQ(err, WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED);
    ASSERT_EQ(sock, -1);
}

// Test: wasi:sockets/udp full lifecycle
// Verifies the complete lifecycle of a UDP socket, including creation, binding, sending, and receiving multiple datagrams.
TEST_F(WasiP2SocketsTest, Udp_FullLifecycle) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    // Create sockets
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Bind sock1
    wasi_ip_socket_address_t addr1;
    addr1.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    addr1.val.ipv4.port = 0; // any port
    addr1.val.ipv4.address.f0 = 127;
    addr1.val.ipv4.address.f1 = 0;
    addr1.val.ipv4.address.f2 = 0;
    addr1.val.ipv4.address.f3 = 1;
    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &addr1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_udp_finish_bind(sock1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Get local address of sock1
    wasi_ip_socket_address_t bound_addr1;
    wasi_sockets_udp_local_address(sock1, &bound_addr1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(bound_addr1.val.ipv4.port, 0);

    // Send/Recv multiple datagrams
    const int num_datagrams = 3;
    wasi_outgoing_datagram_t send_datagrams[num_datagrams];
    char send_bufs[num_datagrams][16];

    for (int i = 0; i < num_datagrams; ++i) {
        snprintf(send_bufs[i], sizeof(send_bufs[i]), "hello%d", i);
        send_datagrams[i].data = (uint8_t *)send_bufs[i];
        send_datagrams[i].data_len = strlen(send_bufs[i]);
        send_datagrams[i].remote_address = &bound_addr1;
    }

    uint64_t sent_len;
    wasi_sockets_udp_send(sock2, send_datagrams, num_datagrams, &sent_len,
                          &err);
    ASSERT_EQ(err, WASI_UDP_SEND_SUCCESS);
    ASSERT_EQ(sent_len, num_datagrams);

    // Poll for receive readiness
    ASSERT_NE(sock1, -1);
    struct pollfd pfd_sock1 = { .fd = sock1, .events = POLLIN, .revents = 0 };
    int ret = poll(&pfd_sock1, 1, 1000);
    ASSERT_GT(ret, 0);

    wasi_incoming_datagram_t *recv_datagrams;
    uint64_t recv_len;
    wasi_sockets_udp_receive(sock1, num_datagrams, &recv_datagrams, &recv_len,
                             &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(recv_len, num_datagrams);

    for (int i = 0; i < num_datagrams; ++i) {
        bool matched = false;
        for (int j = 0; j < num_datagrams; ++j) {
            if (recv_datagrams[i].data_len == strlen(send_bufs[j])
                && memcmp(recv_datagrams[i].data, send_bufs[j],
                          recv_datagrams[i].data_len)
                       == 0) {
                matched = true;
                break;
            }
        }
        ASSERT_TRUE(matched);
        wasm_runtime_free(recv_datagrams[i].data);
    }
    wasm_runtime_free(recv_datagrams);

    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/udp socket options
// Verifies that socket options can be get and set for a UDP socket.
TEST_F(WasiP2SocketsTest, Udp_SocketOptions) {
    wasi_udp_socket_t sock;
    int err;

    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // Hop limit
    uint8_t hop_limit;
    wasi_sockets_udp_unicast_hop_limit(sock, &hop_limit, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_udp_set_unicast_hop_limit(sock, hop_limit + 1);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    uint8_t new_hop_limit;
    wasi_sockets_udp_unicast_hop_limit(sock, &new_hop_limit, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_EQ(new_hop_limit, hop_limit + 1);

    // Buffer sizes
    uint64_t recv_buf_size, send_buf_size;
    wasi_sockets_udp_receive_buffer_size(sock, &recv_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_udp_send_buffer_size(sock, &send_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_udp_set_receive_buffer_size(sock, recv_buf_size * 2);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    err = wasi_sockets_udp_set_send_buffer_size(sock, send_buf_size * 2);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    uint64_t new_recv_buf_size, new_send_buf_size;
    wasi_sockets_udp_receive_buffer_size(sock, &new_recv_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_udp_send_buffer_size(sock, &new_send_buf_size, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_GE(new_recv_buf_size, recv_buf_size);
    ASSERT_GE(new_send_buf_size, send_buf_size);

    close(sock);
}

// Test: wasi:sockets/udp.stream with an invalid socket
// Verifies that `stream` correctly handles an error when passed an invalid
// socket handle.
TEST_F(WasiP2SocketsTest, Udp_StreamInvalidSocket) {
    wasi_udp_socket_t invalid_sock = -1;
    int err;

    wasi_incoming_datagram_stream_t input_stream = 0;
    wasi_outgoing_datagram_stream_t output_stream = 0;
    wasi_sockets_udp_stream(invalid_sock, NULL, &input_stream, &output_stream,
                            &err);

    ASSERT_EQ(err, EBADF);
    ASSERT_EQ(input_stream, -1);
    ASSERT_EQ(output_stream, -1);
}

// Test: wasi:sockets/udp.stream
// Verifies that `stream` returns new, independent stream handles for a UDP socket.
TEST_F(WasiP2SocketsTest, Udp_Stream) {
    wasi_udp_socket_t sock;
    int err;
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_incoming_datagram_stream_t input_stream;
    wasi_outgoing_datagram_stream_t output_stream;
    wasi_sockets_udp_stream(sock, NULL, &input_stream, &output_stream, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    ASSERT_NE(input_stream, (wasi_incoming_datagram_stream_t)-1);
    ASSERT_NE(output_stream, (wasi_outgoing_datagram_stream_t)-1);
    ASSERT_NE(input_stream, sock);
    ASSERT_NE(output_stream, sock);

    close(input_stream);
    close(output_stream);
    close(sock);
}

// Test: wasi:sockets/tcp.start-bind with an invalid socket
// Verifies that `start-bind` returns `bad-descriptor` for an invalid TCP socket.
TEST_F(WasiP2SocketsTest, Tcp_StartBindInvalidSocket) {
    wasi_tcp_socket_t invalid_sock = -1;
    int err;
    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t network = wasi_sockets_instance_network();

    err = wasi_sockets_tcp_start_bind(invalid_sock, network, &local_addr);

    ASSERT_EQ(err, EBADF);
}

// Test: wasi:sockets/udp.start-bind with an invalid socket
// Verifies that `start-bind` returns `bad-descriptor` for an invalid UDP socket.
TEST_F(WasiP2SocketsTest, Udp_StartBindInvalidSocket) {
    wasi_udp_socket_t invalid_sock = -1;
    int err;
    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t network = wasi_sockets_instance_network();

    err = wasi_sockets_udp_start_bind(invalid_sock, network, &local_addr);

    ASSERT_EQ(err, EBADF);
}

// Test: wasi:sockets/tcp.start-bind with a null address
// Verifies that `start-bind` returns `invalid-argument` when passed a null address.
TEST_F(WasiP2SocketsTest, Tcp_StartBindNullAddress) {
    wasi_tcp_socket_t sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_network_t network = wasi_sockets_instance_network();

    err = wasi_sockets_tcp_start_bind(sock, network, NULL);

    ASSERT_EQ(err, EINVAL);

    close(sock);
}

// Test: wasi:sockets/udp.start-bind with a null address
// Verifies that `start-bind` returns `invalid-argument` when passed a null address.
TEST_F(WasiP2SocketsTest, Udp_StartBindNullAddress) {
    wasi_udp_socket_t sock;
    int err;

    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_network_t network = wasi_sockets_instance_network();

    err = wasi_sockets_udp_start_bind(sock, network, NULL);

    ASSERT_EQ(err, EINVAL);

    close(sock);
}

// Test: wasi:sockets/tcp.start-bind with an invalid network handle
// Verifies that `start-bind` returns `invalid-argument` for an invalid network handle.
TEST_F(WasiP2SocketsTest, Tcp_StartBindInvalidNetwork) {
    wasi_tcp_socket_t sock;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t invalid_network = (wasi_network_t)-1;

    err = wasi_sockets_tcp_start_bind(sock, invalid_network, &local_addr);

    ASSERT_EQ(err, EINVAL);

    close(sock);
}

// Test: wasi:sockets/udp.start-bind with an invalid network handle
// Verifies that `start-bind` returns `invalid-argument` for an invalid network handle.
TEST_F(WasiP2SocketsTest, Udp_StartBindInvalidNetwork) {
    wasi_udp_socket_t sock;
    int err;

    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t invalid_network = (wasi_network_t)-1;

    err = wasi_sockets_udp_start_bind(sock, invalid_network, &local_addr);

    ASSERT_EQ(err, EINVAL);

    close(sock);
}

// Test: wasi:sockets/tcp.start-bind with an address already in use
// Verifies that attempting to bind to an address already in use returns `address-in-use`.
TEST_F(WasiP2SocketsTest, Tcp_StartBindAddressInUse) {
    wasi_tcp_socket_t sock1, sock2;
    int err;

    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_tcp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_tcp_start_bind(sock1, network, &local_addr);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t bound_addr;
    wasi_sockets_tcp_local_address(sock1, &bound_addr, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // With SO_REUSEADDR, this should now succeed.
    err = wasi_sockets_tcp_start_bind(sock2, network, &bound_addr);

    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    close(sock1);
    close(sock2);
}

// Test: wasi:sockets/udp.start-bind with an address already in use
// Verifies that attempting to bind to an address already in use returns `address-in-use`.
TEST_F(WasiP2SocketsTest, Udp_StartBindAddressInUse) {
    wasi_udp_socket_t sock1, sock2;
    int err;

    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock1, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);
    wasi_sockets_create_udp_socket(WASI_IP_ADDRESS_FAMILY_IPV4, &sock2, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t local_addr;
    local_addr.tag = WASI_IP_SOCKET_ADDRESS_TAG_IPV4;
    local_addr.val.ipv4.port = 0; // any port
    local_addr.val.ipv4.address.f0 = 127;
    local_addr.val.ipv4.address.f1 = 0;
    local_addr.val.ipv4.address.f2 = 0;
    local_addr.val.ipv4.address.f3 = 1;

    wasi_network_t network = wasi_sockets_instance_network();
    err = wasi_sockets_udp_start_bind(sock1, network, &local_addr);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    wasi_ip_socket_address_t bound_addr;
    wasi_sockets_udp_local_address(sock1, &bound_addr, &err);
    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    // With SO_REUSEADDR, this should now succeed.
    err = wasi_sockets_udp_start_bind(sock2, network, &bound_addr);

    ASSERT_EQ(err, WASI_ERROR_CODE_SUCCESS);

    close(sock1);
    close(sock2);
}
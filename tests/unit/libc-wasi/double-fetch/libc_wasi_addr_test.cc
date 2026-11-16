/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * Tests for the address arguments of libc-wasi socket APIs.
 *
 * sock_bind, sock_connect and sock_send_to check the address against the
 * address pool and then use it. Another wasm thread can change the guest
 * address in between. The race case checks that no datagram reaches an
 * address outside the pool.
 */

#include "libc_wasi_test_fixture.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *ADDR_OPS = "addr_ops.wasm";
static const char *ADDR_THREADS = "addr_threads.wasm";

/* guest layout used by the addr_ops and addr_threads modules */
static const uint32_t ADDR_OFF = 0x400;   /* __wasi_addr_t argument */
static const uint32_t FD_OUT_OFF = 0x500; /* sock_open output */

/* port of the datagram target, the same as in addr_threads.wat */
static const uint16_t TARGET_PORT = 47110;

static const char *const ADDR_POOL[] = { "127.0.0.1/32" };

/* WASI errno values */
static const uint32_t WASI_EACCES = 2;

class LibcWasiAddrTest : public LibcWasiTest
{
  protected:
    /*
     * Write a __wasi_addr_t of kind IPv4 at ADDR_OFF. The layout matches
     * __wasi_addr_t in platform_wasi_types.h: kind (4 bytes), the four
     * address bytes, then the port in host byte order.
     */
    void write_ipv4_addr(uint8_t a, uint8_t b, uint8_t c, uint8_t d,
                         uint16_t port)
    {
        uint8_t addr[24] = { 0 };
        addr[4] = a;
        addr[5] = b;
        addr[6] = c;
        addr[7] = d;
        memcpy(addr + 8, &port, sizeof(port));
        memcpy(guest(ADDR_OFF), addr, sizeof(addr));
    }

    /* Open a UDP socket in the module. Returns its fd. */
    uint32_t open_udp_socket(const char *open_func)
    {
        std::vector<uint32_t> argv = { 0, 0, 0, FD_OUT_OFF };
        EXPECT_TRUE(call(open_func, argv));
        EXPECT_EQ(argv[0], 0u);
        return read_u32(FD_OUT_OFF);
    }
};

/* Bind a non-blocking UDP socket on ip:port. Returns -1 on failure. */
static int
open_udp_receiver(const char *ip, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in sa;
    int one = 1;

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

/* Count the datagrams queued on a non-blocking socket. */
static int
drain_datagrams(int fd)
{
    char buf[64];
    int count = 0;

    while (recv(fd, buf, sizeof(buf), 0) > 0)
        count++;
    return count;
}

TEST_F(LibcWasiAddrTest, SockBindOutsideAddrPoolDenied)
{
    load_module(ADDR_OPS, ADDR_POOL, 1);

    write_ipv4_addr(10, 0, 0, 1, TARGET_PORT);

    std::vector<uint32_t> argv = { 99, ADDR_OFF };
    ASSERT_TRUE(call("call_sock_bind", argv));
    EXPECT_EQ(argv[0], WASI_EACCES);
}

TEST_F(LibcWasiAddrTest, SockConnectOutsideAddrPoolDenied)
{
    load_module(ADDR_OPS, ADDR_POOL, 1);

    write_ipv4_addr(10, 0, 0, 1, TARGET_PORT);

    std::vector<uint32_t> argv = { 99, ADDR_OFF };
    ASSERT_TRUE(call("call_sock_connect", argv));
    EXPECT_EQ(argv[0], WASI_EACCES);
}

TEST_F(LibcWasiAddrTest, SockSendToOutsideAddrPoolDenied)
{
    load_module(ADDR_OPS, ADDR_POOL, 1);

    set_single_iovec("ping", IOVS_OFF, DATA_OFF, 4);
    write_ipv4_addr(10, 0, 0, 1, TARGET_PORT);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, ADDR_OFF, OUT_OFF };
    ASSERT_TRUE(call("call_sock_send_to", argv));
    EXPECT_EQ(argv[0], WASI_EACCES);
    EXPECT_EQ(read_u32(OUT_OFF), 0u);
}

/* An address inside the pool is sent to and the byte count is reported. */
TEST_F(LibcWasiAddrTest, SockSendToAllowedAddress)
{
    load_module(ADDR_OPS, ADDR_POOL, 1);

    uint32_t fd = open_udp_socket("call_sock_open");

    set_single_iovec("ping", IOVS_OFF, DATA_OFF, 4);
    write_ipv4_addr(127, 0, 0, 1, TARGET_PORT);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { fd, IOVS_OFF, 1, 0, ADDR_OFF, OUT_OFF };
    ASSERT_TRUE(call("call_sock_send_to", argv));
    EXPECT_EQ(argv[0], 0u);
    EXPECT_EQ(read_u32(OUT_OFF), 4u);
}

/*
 * sock_send_to checks the destination against the pool, then sends to it. A
 * second thread rewrites the destination between 127.0.0.1 (in the pool) and
 * 127.0.0.2 (not in the pool). No datagram may reach 127.0.0.2.
 */
TEST_F(LibcWasiAddrTest, SockSendToConcurrentDestinationRewrite)
{
    int receiver = open_udp_receiver("127.0.0.2", TARGET_PORT);
    ASSERT_GE(receiver, 0) << "cannot bind 127.0.0.2:" << TARGET_PORT;

    load_module(ADDR_THREADS, ADDR_POOL, 1);

    uint32_t fd = open_udp_socket("call_sock_open");

    std::vector<uint32_t> argv = { fd, 20000 };
    ASSERT_TRUE(call("run", argv)) << "call raised an exception";

    /* give the loopback datagrams a moment to be queued */
    usleep(100 * 1000);
    EXPECT_EQ(drain_datagrams(receiver), 0)
        << "datagram reached an address outside the pool";
    close(receiver);
}

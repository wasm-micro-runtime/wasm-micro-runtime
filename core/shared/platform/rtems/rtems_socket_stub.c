/*
 * Copyright (C) 2026 KylinSoft.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * os_socket_* stubs for libc-wasi and the interpreter debugger. Real RTEMS
 * networking needs libbsd and is out of scope for this no-networking port.
 */

#include "platform_api_extension.h"
#include "platform_wasi_types.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>

#if WASM_ENABLE_LIBC_WASI != 0 || WASM_ENABLE_DEBUG_INTERP != 0

int
os_socket_create(bh_socket_t *sock, bool is_ipv4, bool is_tcp)
{
    (void)sock;
    (void)is_ipv4;
    (void)is_tcp;
    return BHT_ERROR;
}

int
os_socket_bind(bh_socket_t socket, const char *addr, int *port)
{
    (void)socket;
    (void)addr;
    (void)port;
    return BHT_ERROR;
}

int
os_socket_settimeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return BHT_ERROR;
}

int
os_socket_listen(bh_socket_t socket, int max_client)
{
    (void)socket;
    (void)max_client;
    return BHT_ERROR;
}

int
os_socket_accept(bh_socket_t server_sock, bh_socket_t *sock, void *addr,
                 unsigned int *addrlen)
{
    (void)server_sock;
    (void)sock;
    (void)addr;
    (void)addrlen;
    return BHT_ERROR;
}

int
os_socket_connect(bh_socket_t socket, const char *addr, int port)
{
    (void)socket;
    (void)addr;
    (void)port;
    return BHT_ERROR;
}

int
os_socket_recv(bh_socket_t socket, void *buf, unsigned int len)
{
    (void)socket;
    (void)buf;
    (void)len;
    return BHT_ERROR;
}

int
os_socket_recv_from(bh_socket_t socket, void *buf, unsigned int len,
                      int flags, bh_sockaddr_t *src_addr)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)flags;
    (void)src_addr;
    return BHT_ERROR;
}

int
os_socket_send(bh_socket_t socket, const void *buf, unsigned int len)
{
    (void)socket;
    (void)buf;
    (void)len;
    return BHT_ERROR;
}

int
os_socket_send_to(bh_socket_t socket, const void *buf, unsigned int len,
                    int flags, const bh_sockaddr_t *dest_addr)
{
    (void)socket;
    (void)buf;
    (void)len;
    (void)flags;
    (void)dest_addr;
    return BHT_ERROR;
}

int
os_socket_close(bh_socket_t socket)
{
    (void)socket;
    return BHT_ERROR;
}

__wasi_errno_t
os_socket_shutdown(bh_socket_t socket)
{
    (void)socket;
    return __WASI_ENOTSUP;
}

int
os_socket_inet_network(bool is_ipv4, const char *cp, bh_ip_addr_buffer_t *out)
{
    (void)is_ipv4;
    (void)cp;
    (void)out;
    return BHT_ERROR;
}

int
os_socket_addr_resolve(const char *host, const char *service,
                       uint8_t *hint_is_tcp, uint8_t *hint_is_ipv4,
                       bh_addr_info_t *addr_info, size_t addr_info_size,
                       size_t *max_info_size)
{
    (void)host;
    (void)service;
    (void)hint_is_tcp;
    (void)hint_is_ipv4;
    (void)addr_info;
    (void)addr_info_size;
    (void)max_info_size;
    return BHT_ERROR;
}

int
os_socket_addr_local(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
    (void)socket;
    (void)sockaddr;
    return BHT_ERROR;
}

int
os_socket_addr_remote(bh_socket_t socket, bh_sockaddr_t *sockaddr)
{
    (void)socket;
    (void)sockaddr;
    return BHT_ERROR;
}

int
os_socket_set_send_buf_size(bh_socket_t socket, size_t bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return BHT_ERROR;
}

int
os_socket_get_send_buf_size(bh_socket_t socket, size_t *bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return BHT_ERROR;
}

int
os_socket_set_recv_buf_size(bh_socket_t socket, size_t bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return BHT_ERROR;
}

int
os_socket_get_recv_buf_size(bh_socket_t socket, size_t *bufsiz)
{
    (void)socket;
    (void)bufsiz;
    return BHT_ERROR;
}

int
os_socket_set_keep_alive(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_keep_alive(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_send_timeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return BHT_ERROR;
}

int
os_socket_get_send_timeout(bh_socket_t socket, uint64 *timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return BHT_ERROR;
}

int
os_socket_set_recv_timeout(bh_socket_t socket, uint64 timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return BHT_ERROR;
}

int
os_socket_get_recv_timeout(bh_socket_t socket, uint64 *timeout_us)
{
    (void)socket;
    (void)timeout_us;
    return BHT_ERROR;
}

int
os_socket_set_reuse_addr(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_reuse_addr(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_reuse_port(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_reuse_port(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_linger(bh_socket_t socket, bool is_enabled, int linger_s)
{
    (void)socket;
    (void)is_enabled;
    (void)linger_s;
    return BHT_ERROR;
}

int
os_socket_get_linger(bh_socket_t socket, bool *is_enabled, int *linger_s)
{
    (void)socket;
    (void)is_enabled;
    (void)linger_s;
    return BHT_ERROR;
}

int
os_socket_set_tcp_no_delay(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_tcp_no_delay(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_tcp_quick_ack(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_tcp_quick_ack(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_tcp_keep_idle(bh_socket_t socket, uint32_t time_s)
{
    (void)socket;
    (void)time_s;
    return BHT_ERROR;
}

int
os_socket_get_tcp_keep_idle(bh_socket_t socket, uint32_t *time_s)
{
    (void)socket;
    (void)time_s;
    return BHT_ERROR;
}

int
os_socket_set_tcp_keep_intvl(bh_socket_t socket, uint32_t time_s)
{
    (void)socket;
    (void)time_s;
    return BHT_ERROR;
}

int
os_socket_get_tcp_keep_intvl(bh_socket_t socket, uint32_t *time_s)
{
    (void)socket;
    (void)time_s;
    return BHT_ERROR;
}

int
os_socket_set_tcp_fastopen_connect(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_tcp_fastopen_connect(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_ip_multicast_loop(bh_socket_t socket, bool ipv6, bool is_enabled)
{
    (void)socket;
    (void)ipv6;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_ip_multicast_loop(bh_socket_t socket, bool ipv6,
                                bool *is_enabled)
{
    (void)socket;
    (void)ipv6;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_ip_add_membership(bh_socket_t socket,
                                bh_ip_addr_buffer_t *imr_multiaddr,
                                uint32_t imr_interface, bool is_ipv6)
{
    (void)socket;
    (void)imr_multiaddr;
    (void)imr_interface;
    (void)is_ipv6;
    return BHT_ERROR;
}

int
os_socket_set_ip_drop_membership(bh_socket_t socket,
                                 bh_ip_addr_buffer_t *imr_multiaddr,
                                 uint32_t imr_interface, bool is_ipv6)
{
    (void)socket;
    (void)imr_multiaddr;
    (void)imr_interface;
    (void)is_ipv6;
    return BHT_ERROR;
}

int
os_socket_set_ip_ttl(bh_socket_t socket, uint8_t ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return BHT_ERROR;
}

int
os_socket_get_ip_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return BHT_ERROR;
}

int
os_socket_set_ip_multicast_ttl(bh_socket_t socket, uint8_t ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return BHT_ERROR;
}

int
os_socket_get_ip_multicast_ttl(bh_socket_t socket, uint8_t *ttl_s)
{
    (void)socket;
    (void)ttl_s;
    return BHT_ERROR;
}

int
os_socket_set_ipv6_only(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_ipv6_only(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_set_broadcast(bh_socket_t socket, bool is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

int
os_socket_get_broadcast(bh_socket_t socket, bool *is_enabled)
{
    (void)socket;
    (void)is_enabled;
    return BHT_ERROR;
}

uint32_t
htonl(uint32_t hostlong)
{
    return __builtin_bswap32(hostlong);
}

uint16_t
htons(uint16_t hostshort)
{
    return __builtin_bswap16(hostshort);
}

int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    (void)fds;
    (void)nfds;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

int
getsockopt(int socket, int level, int optname, void *optval,
           socklen_t *optlen)
{
    (void)socket;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    errno = ENOSYS;
    return -1;
}

#endif

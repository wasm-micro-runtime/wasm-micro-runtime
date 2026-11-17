/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif
#include "h2.h"
#include "standard_handler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_loop.h"

static h2_server server;
#ifndef _WIN32
static volatile sig_atomic_t stopping;
static void
stop(int sig)
{
    (void)sig;
    stopping = 1;
}
#endif

static void
disconnect(unsigned slot, socket_t *fd)
{
    CLOSE(*fd);
    *fd = BAD_SOCKET;
    h2_close(&server.connections[slot]);
}

int
main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 0;
    double seconds = argc > 2 ? atof(argv[2]) : 8;
    if (port < 0 || port > 65535 || seconds < 1 || seconds > 300)
        return 2;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return 2;
#else
    struct sigaction action = { 0 };
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
#endif
    if (!p3_vm_open(&server.vm, handler_wasm, sizeof(handler_wasm)))
        goto fail;
    wasi_http_init(&server.http, &server.vm);
    socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == BAD_SOCKET || !nonblock(listener))
        goto fail;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))
        || listen(listener, H2_CONNECTIONS))
        goto fail;
#ifdef _WIN32
    int addrlen = sizeof(addr);
#else
    socklen_t addrlen = sizeof(addr);
#endif
    if (getsockname(listener, (struct sockaddr *)&addr, &addrlen))
        goto fail;
    socket_t sockets[H2_CONNECTIONS];
    for (unsigned i = 0; i < H2_CONNECTIONS; ++i)
        sockets[i] = BAD_SOCKET;
    printf("LISTEN %u connections=%u streams=%u per_connection=%u\n",
           ntohs(addr.sin_port), H2_CONNECTIONS, H2_STREAMS, H2_PER_CONNECTION);
    fflush(stdout);
    double start = wall_time(), cpu = cpu_time();
    unsigned rejects = 0, failures = 0, waits = 0, wakes = 0;
    while (wall_time() - start < seconds) {
#ifndef _WIN32
        if (stopping)
            break;
#endif
        bool ready;
        if (!h2_pump(&server, &ready))
            goto fail;
        pollfd_t fds[H2_CONNECTIONS + 1] = { { listener, POLLIN, 0 } };
        unsigned owners[H2_CONNECTIONS + 1], count = 1;
        for (unsigned i = 0; i < H2_CONNECTIONS; ++i) {
            if (sockets[i] == BAD_SOCKET)
                continue;
            h2_connection *c = &server.connections[i];
            if (!h2_output(c)) {
                ++failures;
                disconnect(i, &sockets[i]);
                ready = true;
                continue;
            }
            fds[count] =
                (pollfd_t){ sockets[i],
                            (short)(POLLIN | (c->wire_size ? POLLOUT : 0)), 0 };
            owners[count++] = i;
        }
        double left = seconds - (wall_time() - start);
        if (left <= 0)
            break;
        ++waits;
        int n = poll_sockets(fds, count, ready ? 0 : (int)(left * 1000) + 1);
        if (n < 0 && !again())
            goto fail;
        if (n <= 0)
            continue;
        ++wakes;
        if (fds[0].revents & POLLIN) {
            socket_t fd = accept(listener, NULL, NULL);
            if (fd != BAD_SOCKET) {
                unsigned slot = 0;
                if (nonblock(fd)) {
                    while (slot < H2_CONNECTIONS
                           && (sockets[slot] != BAD_SOCKET
                               || !h2_open(&server, slot)))
                        ++slot;
                }
                else
                    slot = H2_CONNECTIONS;
                if (slot == H2_CONNECTIONS) {
                    ++rejects;
                    CLOSE(fd);
                }
                else {
                    int enabled = 1;
                    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                               (const char *)&enabled, sizeof(enabled));
                    sockets[slot] = fd;
                }
            }
            else if (!again())
                goto fail;
        }
        for (unsigned j = 1; j < count; ++j) {
            unsigned i = owners[j];
            h2_connection *c = &server.connections[i];
            bool broken =
                (fds[j].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (!broken && (fds[j].revents & POLLIN)) {
                uint8_t input[4096];
                n = (int)recv(sockets[i], (char *)input, sizeof(input), 0);
                if (n > 0) {
                    broken = !h2_receive(c, input, (size_t)n);
                    failures += broken;
                }
                else
                    broken = !n || !again();
            }
            if (!broken && (fds[j].revents & POLLOUT)) {
                n = (int)send(sockets[i], (const char *)c->wire,
                              (int)c->wire_size, 0);
                if (n > 0) {
                    c->wire += n;
                    c->wire_size -= (size_t)n;
                }
                else
                    broken = n < 0 && !again();
            }
            if (broken)
                disconnect(i, &sockets[i]);
        }
    }
    CLOSE(listener);
    for (unsigned i = 0; i < H2_CONNECTIONS; ++i)
        if (sockets[i] != BAD_SOCKET)
            disconnect(i, &sockets[i]);
    bool ready;
    for (unsigned i = 0; i < 128 && server.live; ++i)
        if (!h2_pump(&server, &ready))
            goto fail;
    printf(
        "H2 completed=%u reset=%u refused=%u connections_rejected=%u "
        "protocol_errors=%u "
        "connection_errors=%u peak_streams=%u peak_handles=%u "
        "peak_resources=%u "
        "queue_peak=%u heap_peak=%zu live=%u handles=%u resources=%u heap=%zu "
        "rss_bytes=%zu cpu_s=%.9f elapsed_s=%.6f waits=%u wakes=%u "
        "callbacks=%u\n",
        server.completed, server.reset, server.refused, rejects,
        server.protocol_errors, failures, server.peak, server.vm.async.peak,
        server.http.peak, server.input_peak, server.heap_peak, server.live,
        server.vm.async.live, server.http.live, server.heap, rss(),
        cpu_time() - cpu, wall_time() - start, waits, wakes, server.callbacks);
    p3_vm_close(&server.vm);
#ifdef _WIN32
    WSACleanup();
#endif
    return server.live || server.vm.async.live || server.http.live
           || server.heap;
fail:
    fprintf(stderr, "HTTP/2 failed: %s %s\n", server.vm.error,
            server.vm.async.error ? server.vm.async.error : "");
    return 1;
}

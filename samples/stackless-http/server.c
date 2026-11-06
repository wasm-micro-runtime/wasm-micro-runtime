/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#endif
#include "http.h"
#ifdef P3_WASI_HTTP
#include "standard_handler.h"
#else
#include "handler.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "socket_loop.h"
static http_server server;
#if defined(P3_WASI_HTTP) && !defined(_WIN32)
static volatile sig_atomic_t interrupted;
static void
interrupt(int sig)
{
    (void)sig;
    interrupted = 1;
}
#endif

int
main(int argc, char **argv)
{
    bool bench = argc > 1 && !strcmp(argv[1], "--native");
    int port = argc > 1 ? atoi(argv[1]) : 0;
    double seconds = argc > 2 ? atof(argv[2]) : 5;
    if (port < 0 || port > 65535 || seconds < 1 || seconds > 300)
        return 2;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return 2;
#else
    signal(SIGPIPE, SIG_IGN);
#ifdef P3_WASI_HTTP
    struct sigaction action = { 0 };
    action.sa_handler = interrupt;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
#endif
#endif
    const uint8_t *module = handler_wasm;
    size_t module_size = sizeof(handler_wasm);
    uint8_t *file_bytes = NULL;
#ifdef P3_WASI_HTTP
    if (argc > 3) {
        FILE *file = fopen(argv[3], "rb");
        if (!file || fseek(file, 0, SEEK_END))
            goto fail;
        long size = ftell(file);
        if (size <= 0 || fseek(file, 0, SEEK_SET))
            goto fail;
        module_size = (size_t)size;
        file_bytes = malloc(module_size);
        if (!file_bytes || fread(file_bytes, 1, module_size, file) != module_size)
            goto fail;
        fclose(file);
        module = file_bytes;
    }
#endif
    bool opened = p3_vm_open(&server.vm, module, module_size);
    free(file_bytes);
    if (!opened)
        goto fail;
#ifdef P3_WASI_HTTP
    wasi_http_init(&server.http, &server.vm);
#endif
    if (bench) {
        const int32_t iterations = 10000000;
        if (!http_native_loop(&server, 10000))
            goto fail;
        double start = cpu_time();
        if (!http_native_loop(&server, iterations))
            goto fail;
        double ns = (cpu_time() - start) * 1e9 / iterations;
        printf("NATIVE imports=%d cpu_ns_per_import=%.3f\n", iterations, ns);
        p3_vm_close(&server.vm);
        return 0;
    }
    socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == BAD_SOCKET || !nonblock(listener))
        goto fail;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr))
        || listen(listener, HTTP_CONNECTIONS))
        goto fail;
#ifdef _WIN32
    int addrlen = sizeof(addr);
#else
    socklen_t addrlen = sizeof(addr);
#endif
    if (getsockname(listener, (struct sockaddr *)&addr, &addrlen))
        goto fail;
    socket_t sockets[HTTP_CONNECTIONS];
    for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i)
        sockets[i] = BAD_SOCKET;
    printf("LISTEN %u\n", ntohs(addr.sin_port));
    fflush(stdout);
    double wall_start = wall_time(), cpu_start = cpu_time();
    unsigned wakes = 0, waits = 0, rejected = 0;
    uint8_t input[HTTP_CHUNK];
    while (wall_time() - wall_start < seconds) {
#if defined(P3_WASI_HTTP) && !defined(_WIN32)
        if (interrupted)
            break;
#endif
        pollfd_t fds[HTTP_CONNECTIONS + 1];
        unsigned owners[HTTP_CONNECTIONS + 1], count = 1;
        fds[0] = (pollfd_t){ listener, POLLIN, 0 };
        bool ready = false;
        for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i) {
            http_connection *c = &server.connections[i];
            if (!c->live)
                continue;
            if (!http_progress(&server, i))
                goto fail;
            if (http_finished(c)) {
                if (sockets[i] != BAD_SOCKET)
                    CLOSE(sockets[i]);
                sockets[i] = BAD_SOCKET;
                if (!http_reap(&server, i))
                    goto fail;
                continue;
            }
            ready |= p3_task_ready(&c->task);
            if (sockets[i] == BAD_SOCKET)
                continue;
            short events = 0;
            if (http_input_capacity(&server, i))
                events |= POLLIN;
            if (c->sent < c->length)
                events |= POLLOUT;
            fds[count] = (pollfd_t){ sockets[i], events, 0 };
            owners[count++] = i;
        }
        double left = seconds - (wall_time() - wall_start);
        if (left <= 0)
            break;
        int timeout = ready ? 0 : (int)(left * 1000) + 1;
        ++waits;
        int events = poll_sockets(fds, count, timeout);
        if (events < 0) {
            if (again())
                continue;
            goto fail;
        }
        if (!events)
            continue;
        ++wakes;
        if (fds[0].revents & POLLIN) {
            socket_t fd = accept(listener, NULL, NULL);
            if (fd != BAD_SOCKET) {
                unsigned i = 0;
                while (i < HTTP_CONNECTIONS && server.connections[i].live)
                    ++i;
                if (i == HTTP_CONNECTIONS || !nonblock(fd)) {
                    ++rejected;
                    CLOSE(fd);
                }
                else {
                    int cap = 4096;
                    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char *)&cap,
                               sizeof(cap));
                    sockets[i] = fd;
                    if (!http_open(&server, i))
                        goto fail;
                }
            }
            else if (!again())
                goto fail;
        }
        for (unsigned j = 1; j < count; ++j) {
            unsigned i = owners[j];
            http_connection *c = &server.connections[i];
            bool broken =
                (fds[j].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            if (!broken && (fds[j].revents & POLLIN)) {
                uint32_t cap = http_input_capacity(&server, i);
                if (cap > sizeof(input))
                    cap = sizeof(input);
                int n = (int)recv(sockets[i], (char *)input, (int)cap, 0);
                if (n > 0) {
                    if (!http_input(&server, i, input, (uint32_t)n))
                        goto fail;
                }
                else if (!n) {
                    /* A closed connection before a complete request cancels
                     * the lifted task and runs its guest cleanup callback. */
                    broken = true;
                }
                else if (!again())
                    broken = true;
            }
            if (!broken && (fds[j].revents & POLLOUT)) {
                int n = (int)send(sockets[i], (const char *)c->output + c->sent,
                                  (int)(c->length - c->sent), 0);
                if (n > 0)
                    c->sent += (size_t)n;
                else if (n < 0 && !again())
                    broken = true;
            }
            if (broken) {
                CLOSE(sockets[i]);
                sockets[i] = BAD_SOCKET;
                if (!http_disconnect(&server, i))
                    goto fail;
            }
        }
    }
    double cpu = cpu_time() - cpu_start, elapsed = wall_time() - wall_start;
    CLOSE(listener);
    for (unsigned i = 0; i < HTTP_CONNECTIONS; ++i) {
        if (sockets[i] != BAD_SOCKET)
            CLOSE(sockets[i]);
        if (!server.connections[i].live)
            continue;
        if (!http_disconnect(&server, i) || !http_progress(&server, i)
            || !http_reap(&server, i))
            goto fail;
    }
#ifdef P3_WASI_HTTP
    printf("HTTP resources_live=%u resources_peak=%u rejected=%u\n",
           server.http.live, server.http.peak, rejected);
#else
    (void)rejected;
#endif
    printf("RESULT completed=%u cancelled=%u disconnected=%u callbacks=%u "
           "transfers=%u bytes=%llu "
           "live=%u peak_handles=%u server_bytes=%zu rss_bytes=%zu cpu_s=%.9f "
           "elapsed_s=%.6f waits=%u wakes=%u\n",
           server.completed, server.cancelled, server.disconnected,
           server.callbacks, server.vm.async.transfers,
           (unsigned long long)server.vm.async.bytes, server.vm.async.live,
           server.vm.async.peak, sizeof(server), rss(), cpu, elapsed, waits,
           wakes);
    p3_vm_close(&server.vm);
#ifdef _WIN32
    WSACleanup();
#endif
    return server.vm.async.live ? 1 : 0;
fail:
    fprintf(stderr, "HTTP failed: %s %s\n", server.vm.error,
            server.vm.async.error ? server.vm.async.error : "");
    return 1;
}

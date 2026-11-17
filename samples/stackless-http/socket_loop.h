/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_SOCKET_LOOP_H
#define STACKLESS_SOCKET_LOOP_H
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
typedef SOCKET socket_t;
typedef WSAPOLLFD pollfd_t;
#define BAD_SOCKET INVALID_SOCKET
#define poll_sockets WSAPoll
#define CLOSE closesocket
static bool
nonblock(socket_t fd)
{
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
}
static bool
again(void)
{
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
}
static double
wall_time(void)
{
    return GetTickCount64() / 1000.0;
}
static double
cpu_time(void)
{
    FILETIME create, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
    ULARGE_INTEGER k, u;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return (double)(k.QuadPart + u.QuadPart) / 10000000.0;
}
static size_t
rss(void)
{
    PROCESS_MEMORY_COUNTERS m;
    return GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof(m))
               ? m.PeakWorkingSetSize
               : 0;
}
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
typedef struct pollfd pollfd_t;
#define BAD_SOCKET (-1)
#define CLOSE close
#define poll_sockets poll
static bool
nonblock(socket_t fd)
{
    return fcntl(fd, F_SETFL, O_NONBLOCK) == 0;
}
static bool
again(void)
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
static double
wall_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static double
cpu_time(void)
{
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static size_t
rss(void)
{
    struct rusage r;
    if (getrusage(RUSAGE_SELF, &r))
        return 0;
#ifdef __APPLE__
    return (size_t)r.ru_maxrss;
#else
    return (size_t)r.ru_maxrss * 1024;
#endif
}
#endif
#endif

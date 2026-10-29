/*
 * Copyright (c) 2021, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef RTTHREAD_PLATFORM_INTERNAL_H
#define RTTHREAD_PLATFORM_INTERNAL_H

#include <rtthread.h>
#include <rthw.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef BH_PLATFORM_RTT
#define BH_PLATFORM_RTT
#endif
#define BH_HAS_DLFCN 0

#if !defined(RT_USING_HEAP) || !defined(RT_USING_MUTEX)
#error "RT-Thread WAMR requires RT_USING_HEAP and RT_USING_MUTEX"
#endif

typedef rt_thread_t korp_tid;
typedef struct rt_thread korp_thread;
typedef struct rt_mutex korp_mutex;
/* Optional synchronization APIs still require a complete RT backend. */
typedef struct {
    int unused;
} korp_cond;
typedef struct {
    int unused;
} korp_rwlock;
typedef struct rt_semaphore korp_sem;
typedef int os_file_handle;
typedef int os_raw_file_handle;
#if WASM_ENABLE_LIBC_WASI != 0
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
typedef DIR *os_dir_stream;
#else
typedef void *os_dir_stream;
struct pollfd;
#endif
typedef struct pollfd os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef struct timespec os_timespec;

int
os_getpagesize(void);

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}
#endif /* RTTHREAD_PLATFORM_INTERNAL_H */

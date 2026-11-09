/*
 * Copyright (C) 2026 KylinSoft.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BH_PLATFORM_RTEMS
#define BH_PLATFORM_RTEMS
#endif

/*
 * RTEMS newlib declares preadv/pwritev but does not provide the symbols;
 * posix_fallocate is absent. Consumed by posix_file.c before its defaults
 * are applied.
 */
#ifndef CONFIG_HAS_PREADV
#define CONFIG_HAS_PREADV 0
#endif
#ifndef CONFIG_HAS_PWRITEV
#define CONFIG_HAS_PWRITEV 0
#endif
#ifndef CONFIG_HAS_POSIX_FALLOCATE
#define CONFIG_HAS_POSIX_FALLOCATE 0
#endif

#ifndef BH_HAS_DLFCN
#define BH_HAS_DLFCN 0
#endif

#ifndef os_thread_local_attribute
#define os_thread_local_attribute __thread
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)
#define BH_THREAD_DEFAULT_PRIORITY 100

typedef pthread_t korp_tid;
typedef pthread_mutex_t korp_mutex;
typedef pthread_cond_t korp_cond;
typedef pthread_t korp_thread;
typedef pthread_rwlock_t korp_rwlock;
typedef sem_t korp_sem;

#define OS_THREAD_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline int
os_getpagesize(void)
{
    return 4096;
}

typedef int os_file_handle;
typedef DIR *os_dir_stream;
typedef int os_raw_file_handle;
typedef struct pollfd os_poll_file_handle;
typedef nfds_t os_nfds_t;
typedef struct timespec os_timespec;

#define bh_socket_t int

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

/*
 * <sys/socket.h> exists on RTEMS even when BSD networking is disabled, but the
 * symbols below are only declared, not implemented.  Declaring them here lets
 * the shared POSIX/WASI sources compile; the real (no-op) implementations
 * live in rtems_socket_stub.c.
 */
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);

#ifdef __cplusplus
}
#endif

#endif

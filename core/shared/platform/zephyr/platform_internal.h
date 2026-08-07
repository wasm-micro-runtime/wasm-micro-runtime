/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-FileCopyrightText: 2024 Siemens AG (For Zephyr usermode changes)
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

/*
 * Modern Zephyr uses zephyr/ namespace.
 *
 * Note: Cannot use KERNEL_VERSION_NUMBER here as it's defined in version.h
 * which we're trying to include. Must use feature detection instead.
 */
#ifdef __has_include
#if __has_include(<zephyr/autoconf.h>)
#include <zephyr/autoconf.h>
#include <zephyr/version.h>
#else
#include <autoconf.h>
#include <version.h>
#endif
#else
#include <autoconf.h>
#include <version.h>
#endif

#if KERNEL_VERSION_NUMBER < 0x030200 /* version 3.2.0 */
#include <zephyr.h>
#include <kernel.h>
#if KERNEL_VERSION_NUMBER >= 0x020200 /* version 2.2.0 */
#include <sys/printk.h>
#else
#include <misc/printk.h>
#endif
#else /* else of KERNEL_VERSION_NUMBER < 0x030200 */
#include <zephyr/sys/printk.h>
#endif /* end of KERNEL_VERSION_NUMBER < 0x030200 */

#include <inttypes.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef CONFIG_NET_BUF_USER_DATA_SIZE
#define CONFIG_NET_BUF_USER_DATA_SIZE 0
#endif

#if KERNEL_VERSION_NUMBER < 0x030200 /* version 3.2.0 */
#include <zephyr.h>
#include <net/net_pkt.h>
#include <net/net_if.h>
#include <net/net_ip.h>
#include <net/net_core.h>
#include <net/net_context.h>
#include <fs/fs.h>
#else /* else of KERNEL_VERSION_NUMBER < 0x030200 */
#include <zephyr/kernel.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>
#endif /* end of KERNEL_VERSION_NUMBER < 0x030200 */

#ifdef CONFIG_USERSPACE
#include <zephyr/sys/mutex.h>
#include <zephyr/sys/sem.h>
#endif /* end of CONFIG_USERSPACE */

#if KERNEL_VERSION_NUMBER >= 0x030300 /* version 3.3.0 */
#include <zephyr/cache.h>
#endif /* end of KERNEL_VERSION_NUMBER > 0x030300 */

#ifdef CONFIG_ARM_MPU
#if KERNEL_VERSION_NUMBER < 0x030200 /* version 3.2.0 */
#include <arch/arm/aarch32/cortex_m/cmsis.h>
#elif KERNEL_VERSION_NUMBER < 0x030400 /* version 3.4.0 */
#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
#else /* > 3.4.0 */
#include <cmsis_core.h>
#endif
#endif

#ifdef signbit /* probably since Zephyr v3.5.0 a new picolib is included */
#define BH_HAS_SIGNBIT 1
#endif

#ifndef BH_PLATFORM_ZEPHYR
#define BH_PLATFORM_ZEPHYR
#endif

#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* Synchronization primitives for usermode.
 * The macros are prefixed with 'z' because when building
 * with WAMR_BUILD_LIBC_WASI the same functions are defined,
 * and used in the sandboxed-system-primitives (see locking.h)
 */
#ifdef CONFIG_USERSPACE
#define zmutex_t struct sys_mutex
#define zmutex_init(mtx) sys_mutex_init(mtx)
#define zmutex_lock(mtx, timeout) sys_mutex_lock(mtx, timeout)
#define zmutex_unlock(mtx) sys_mutex_unlock(mtx)

#define zsem_t struct sys_sem
#define zsem_init(sem, init_count, limit) sys_sem_init(sem, init_count, limit)
#define zsem_give(sem) sys_sem_give(sem)
#define zsem_take(sem, timeout) sys_sem_take(sem, timeout)
#define zsem_count_get(sem) sys_sem_count_get(sem)
#else /* else of CONFIG_USERSPACE */
#define zmutex_t struct k_mutex
#define zmutex_init(mtx) k_mutex_init(mtx)
#define zmutex_lock(mtx, timeout) k_mutex_lock(mtx, timeout)
#define zmutex_unlock(mtx) k_mutex_unlock(mtx)

#define zsem_t struct k_sem
#define zsem_init(sem, init_count, limit) k_sem_init(sem, init_count, limit)
#define zsem_give(sem) k_sem_give(sem)
#define zsem_take(sem, timeout) k_sem_take(sem, timeout)
#define zsem_count_get(sem) k_sem_count_get(sem)
#endif /* end of CONFIG_USERSPACE */

#define BH_APPLET_PRESERVED_STACK_SIZE (2 * BH_KB)

/* Default thread priority */
#define BH_THREAD_DEFAULT_PRIORITY 7

typedef struct k_thread korp_thread;
typedef korp_thread *korp_tid;
typedef zmutex_t korp_mutex;
typedef unsigned int korp_sem;

/* korp_rwlock is used in platform_api_extension.h,
   we just define the type to make the compiler happy */
struct os_thread_wait_node;
typedef struct os_thread_wait_node *os_thread_wait_list;
typedef struct korp_cond {
    zmutex_t wait_list_lock;
    os_thread_wait_list thread_wait_list;
} korp_cond;

typedef struct {
    struct k_mutex mtx; // Mutex for exclusive access
    struct k_sem sem;   // Semaphore for shared access
    int read_count;     // Number of readers
} korp_rwlock;

// TODO: Conform to Zephyr POSIX definition of rwlock:
// struct posix_rwlock {
// 	struct k_sem rd_sem;
// 	struct k_sem wr_sem;
// 	struct k_sem reader_active; /* blocks WR till reader has acquired lock */
// 	k_tid_t wr_owner;
// };

#ifndef Z_TIMEOUT_MS
#define Z_TIMEOUT_MS(ms) ms
#endif

/* clang-format off */
void abort(void);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

/* math functions which are not provided by os with minimal libc */
#if defined(CONFIG_MINIMAL_LIBC)
double atan(double x);
double atan2(double y, double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double fmin(double x, double y);
double fmax(double x, double y);
double rint(double x);
double fabs(double x);
double trunc(double x);
float sqrtf(float x);
float floorf(float x);
float ceilf(float x);
float fminf(float x, float y);
float fmaxf(float x, float y);
float rintf(float x);
float fabsf(float x);
float truncf(float x);
int isnan_double(double x);
int isnan_float(float x);
#define isnan(x) (sizeof(x) == sizeof(double) ? isnan_double((double)x) : isnan_float(x))
double pow(double x, double y);
double scalbn(double x, int n);

#ifndef BH_HAS_SIGNBIT
int signbit_double(double x);
int signbit_float(float x);
#define signbit(x) (sizeof(x) == sizeof(double) ? signbit_double((double)x) : signbit_float(x))
#endif

unsigned long long int strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
#else
#include <math.h>
#endif /* CONFIG_MINIMAL_LIBC */

/* clang-format on */

#if KERNEL_VERSION_NUMBER >= 0x030100 /* version 3.1.0 */
#define BH_HAS_SQRT
#define BH_HAS_SQRTF
#endif

/**
 * @brief Allocate executable memory
 *
 * @param size size of the memory to be allocated
 *
 * @return the address of the allocated memory if not NULL
 */
typedef void *(*exec_mem_alloc_func_t)(unsigned int size);

/**
 * @brief Release executable memory
 *
 * @param the address of the executable memory to be released
 */
typedef void (*exec_mem_free_func_t)(void *addr);

/* Below function are called by external project to set related function
 * pointers that will be used to malloc/free executable memory. Otherwise
 * default mechanise will be used.
 */
void
set_exec_mem_alloc_func(exec_mem_alloc_func_t alloc_func,
                        exec_mem_free_func_t free_func);

/* The below types are used in platform_api_extension.h,
   we just define them to make the compiler happy */
typedef int os_dir_stream;
typedef int os_raw_file_handle;

#define OS_DIR_STREAM_INVALID 0

// handle for file system descriptor
typedef struct zephyr_fs_desc {
    char *path;
    union {
        struct fs_file_t file;
        struct fs_dir_t dir;
    };
    bool is_dir;
    bool used;
    uint32_t dir_index; // DSK: supprt for rewind and seek
} zephyr_fs_desc;

// definition of zephyr_handle
typedef struct zephyr_handle {
    int fd;
    bool is_sock;
} zephyr_handle;

typedef struct zephyr_handle *os_file_handle;
#define bh_socket_t zephyr_handle *

typedef struct zsock_pollfd os_poll_file_handle;
typedef unsigned int os_nfds_t;

// Some of these definitions will throw warning for macros
// redefinition if CONFIG_POSIX_API=y, but it's fine.
// Warning: the CONFIG_POSIX_API will surely be deprecated and
// split into more macros, so we may use some ifdefs to avoid
// the warning in the future.
#define POLLIN ZSOCK_POLLIN
#define POLLPRI ZSOCK_POLLPRI
#define POLLOUT ZSOCK_POLLOUT
#define POLLERR ZSOCK_POLLERR
#define POLLHUP ZSOCK_POLLHUP
#define POLLNVAL ZSOCK_POLLNVAL

#define FIONREAD ZFD_IOCTL_FIONREAD

/* 
 * The previous `os_timespec` was a forward declaration: 
 *
 *     typedef struct timespec os_timespec;
 *
 * It was not bad as is, but seemed to not be included from anywhere. 
 * As of Zephyr v3.7.0 (LTS) the `timespec` struct is only declared with the
 * configuration `CONFIG_POSIX_API` enabled which is not a prefered
 * configuration for the Zephyr port.
 *
 * NOTE: If Zephyr later exposes `struct timespec` without requiring
 * CONFIG_POSIX_API, this definition should be replaced by an alias.
 */
typedef struct {
    int64_t tv_sec;
    long    tv_nsec;
} os_timespec;


#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 4
#endif

static inline int
os_sched_yield(void)
{
    k_yield();
    return 0;
}

static inline os_file_handle
os_get_invalid_handle(void)
{
    return NULL;
}

static inline int
os_getpagesize()
{
#ifdef CONFIG_MMU
    return CONFIG_MMU_PAGE_SIZE;
#else
    /* Return a default page size if the MMU is not enabled */
    return 4096; /* 4KB */
#endif
}

#endif

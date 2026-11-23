/*
 * Copyright (C) 2026 KylinSoft.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_extension.h"
#include "platform_api_vmcore.h"

#include <rtems/rtems/cache.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{
}

int
os_printf(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vprintf(fmt, ap);
    va_end(ap);
    return r;
}

int
os_vprintf(const char *fmt, va_list ap)
{
    return vprintf(fmt, ap);
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    void *p;

    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;

    if ((uint64)size >= UINT32_MAX)
        return NULL;

    if (posix_memalign(&p, 32, size) != 0)
        return NULL;

    memset(p, 0, size);
    return p;
}

void
os_munmap(void *addr, size_t size)
{
    (void)size;
    free(addr);
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

/*
 * The Cache Manager is always provided by RTEMS: real operations on
 * cache-enabled BSPs and empty no-ops on cache-less configurations.
 */
void
os_dcache_flush(void)
{
    rtems_cache_flush_entire_data();
}

void
os_icache_flush(void *start, size_t len)
{
    rtems_cache_instruction_sync_after_code_change(start, len);
}

uint64
os_time_get_boot_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return ((uint64)ts.tv_sec) * 1000000ULL
           + ((uint64)ts.tv_nsec) / 1000ULL;
}

uint64
os_time_thread_cputime_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0)
        return os_time_get_boot_us();

    return ((uint64)ts.tv_sec) * 1000000ULL
           + ((uint64)ts.tv_nsec) / 1000ULL;
}

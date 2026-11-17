/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_PICO_PLATFORM_H
#define STACKLESS_PICO_PLATFORM_H
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* Only the foreground owner executes WAMR; IRQs only service lwIP. */
typedef unsigned korp_tid;
typedef unsigned korp_mutex;
typedef unsigned korp_cond;
typedef unsigned korp_sem;
typedef unsigned korp_rwlock;
typedef int os_file_handle;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned os_nfds_t;
typedef void *os_dir_stream;
typedef struct timespec os_timespec;
os_file_handle
os_get_invalid_handle(void);
#define os_printf printf
#define os_vprintf vprintf
static inline unsigned
os_getpagesize(void)
{
    return 4096;
}
#endif

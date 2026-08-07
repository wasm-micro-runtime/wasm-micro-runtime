/*
 * Copyright (C) 2024 Grenoble INP - ESISAR.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_extension.h"
#include "platform_api_vmcore.h"
#include "libc_errno.h"

#include <zephyr/kernel.h>

/* Notes:
 * We are using the same implementation for __WASI_CLOCK_REALTIME and
 * __WASI_CLOCK_MONOTONIC, because it is a practical solution when there
 * is no RTC or external time source available.
 * The implementation is based on the Zephyr `k_cycle_get_32()` function or
 * the 64bits variant if available.
 * We could have used `k_uptime_get()` instead, but it is not as precise,
 * it has a millisecond resolution or depend on CONFIG_SYS_CLOCK_TICKS_PER_SEC.
 * Feel free to change the implementation if you have a better solution.
 * May look at
 * https://github.com/zephyrproject-rtos/zephyr/blob/main/lib/posix/options/clock.c
 * for reference.
 */

#define NANOSECONDS_PER_SECOND 1000000000ULL

__wasi_errno_t
os_clock_res_get(__wasi_clockid_t clock_id, __wasi_timestamp_t *resolution)
{
    switch (clock_id) {
        case __WASI_CLOCK_PROCESS_CPUTIME_ID:
        case __WASI_CLOCK_THREAD_CPUTIME_ID:
            return __WASI_ENOTSUP;
        case __WASI_CLOCK_REALTIME:
        case __WASI_CLOCK_MONOTONIC:
            *resolution =
                NANOSECONDS_PER_SECOND / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
            return __WASI_ESUCCESS;
        default:
            return __WASI_EINVAL;
    }
}

__wasi_errno_t
os_clock_time_get(__wasi_clockid_t clock_id, __wasi_timestamp_t precision,
                  __wasi_timestamp_t *time)
{
    (void)precision;

    switch (clock_id) {
        case __WASI_CLOCK_PROCESS_CPUTIME_ID:
        case __WASI_CLOCK_THREAD_CPUTIME_ID:
            return __WASI_ENOTSUP;
        case __WASI_CLOCK_REALTIME:
        case __WASI_CLOCK_MONOTONIC:
#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
            *time = k_cyc_to_ns_near64(k_cycle_get_64());
#else
            *time = k_cyc_to_ns_near64(k_cycle_get_32());
#endif
            return __WASI_ESUCCESS;
        default:
            return __WASI_EINVAL;
    }
}
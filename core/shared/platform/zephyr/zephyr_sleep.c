/*
 * Copyright (C) 2024 Grenoble INP - ESISAR.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "platform_api_extension.h"

/*
 * In Zephyr v3.7, there is no simple way to get a `nanosleep` implementation.
 * But in the later version the Zephyr community introduced some clock APIs
 * and their POSIX compatibility layer. 
 *
 * Relevant Zephyr sources:
 *     - zephyr/include/zephyr/sys/clock.h 
 *     - Zephyr/lib/os/clock.c
 * POSIX layer:
 *     - zephyr/lib/posix/options/clock.c
 *
 * Instead of re-implementing the full Clock APIs, this file provides a naive
 * `nanosleep` implementation based on the Zephyr thread API (`k_sleep`).
 * 
 * Limitations:
 *     Maximum sleep duration is limited by UINT32_MAX or UINT64_MAX ticks
 *     (≈ 4,294,967,295 and 18,446,744,073,709,551,615 respectively).
 *
 * Example at a "slow" clock rate of 50 kHz:
 *     - UINT32_MAX: ~85 899s (~23 hours)
 *     - UINT64_MAX: ~368 934 881 474 191s (~11.7 millions years)
 * Clearly, `nanosleep` should not be used for such long durations.
 *
 * Note: this assumes `CONFIG_POSIX_API=n` in the Zephyr application.
 */

static k_ticks_t timespec_to_ticks(const os_timespec *ts);
static void ticks_to_timespec(k_ticks_t ticks, os_timespec *ts);

__wasi_errno_t
os_nanosleep(const os_timespec *req, os_timespec *rem)
{
    k_timeout_t timeout;
    k_ticks_t rem_ticks;

    if (req == NULL){
        return __WASI_EINVAL;
    }

    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= NSEC_PER_SEC) {
        return __WASI_EINVAL;
    }

    if (req->tv_sec == 0 && req->tv_nsec == 0) {
        if (rem != NULL) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        return __WASI_ESUCCESS;
    }

    timeout.ticks = timespec_to_ticks(req);

    /*
     * The function `int32_t k_sleep(k_timeout_t timeout)` return either:
     *     * 0     requested time elaspsed.
     *     * >0    remaining time in ms (due to k_wakeup).
     */
    int32_t rc = k_sleep(timeout);
    if (rem != NULL) {
        if (rc > 0) {

#ifdef CONFIG_TIMEOUT_64BIT
            rem_ticks = (k_ticks_t)((uint64_t)rc * CONFIG_SYS_CLOCK_TICKS_PER_SEC / MSEC_PER_SEC);
#else  /* CONFIG_TIMEOUT_32BIT */
            uint64_t temp_ticks = (uint64_t)rc * CONFIG_SYS_CLOCK_TICKS_PER_SEC / MSEC_PER_SEC;
            rem_ticks = (k_ticks_t)(temp_ticks > UINT32_MAX ? UINT32_MAX : temp_ticks);
#endif
            ticks_to_timespec(rem_ticks, rem);
        } else {
            rem->tv_sec  = 0;
            rem->tv_nsec = 0;
        }
    }

    return __WASI_ESUCCESS;
}


static k_ticks_t timespec_to_ticks(const os_timespec *ts)
{
    const uint64_t ticks_per_sec = CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    uint64_t total_ns, ticks;

    total_ns = (uint64_t)ts->tv_sec * NSEC_PER_SEC + (uint64_t)ts->tv_nsec;
    ticks    = total_ns * ticks_per_sec / NSEC_PER_SEC;

#ifdef CONFIG_TIMEOUT_64BIT
    if (ticks > INT64_MAX) {
        return INT64_MAX;
    }
#else  /* CONFIG_TIMEOUT_32BIT */
    if (ticks > UINT32_MAX) {
        return UINT32_MAX;
    }
#endif

    return (k_ticks_t)ticks;
}

static void ticks_to_timespec(k_ticks_t ticks, os_timespec *ts)
{
    const uint64_t ticks_per_sec = CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    uint64_t total_ns;

    if (ts == NULL) {
        return;
    }

    total_ns = ((uint64_t)ticks * NSEC_PER_SEC) / ticks_per_sec;

    ts->tv_sec  = (long)(total_ns / NSEC_PER_SEC);
    ts->tv_nsec = (long)(total_ns % NSEC_PER_SEC);
}
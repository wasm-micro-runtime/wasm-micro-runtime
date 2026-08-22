/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_clocks.h"

#include <time.h>
#include <unistd.h>
#include <sys/timerfd.h>

/**
 * @brief Read the current value of the wall-clock.
 * @details Implements the `now` function from the `wasi:clocks/wall-clock`
 *          interface. This clock is not monotonic and represents the
 *          time since the Unix Epoch.
 * @return A wasi_datetime_t struct containing the current time.
 */
wasi_datetime_t
wasi_wall_clock_now(void)
{
    struct timespec tp;

    // Get the calendar time using CLOCK_REALTIME.
    clock_gettime(CLOCK_REALTIME, &tp);

    // Map the POSIX timespec to the wasi_datetime_t struct.
    return (wasi_datetime_t){
        .seconds = (uint64_t)tp.tv_sec,
        .nanoseconds = (uint32_t)tp.tv_nsec,
    };
}

/**
 * @brief Query the resolution of the wall-clock.
 * @details Implements the `resolution` function from the
 *          `wasi:clocks/wall-clock` interface. This represents the
 *          smallest measurable unit of time for this clock.
 * @return A wasi_datetime_t struct containing the clock's resolution.
 */
wasi_datetime_t
wasi_wall_clock_resolution(void)
{
    struct timespec tp;

    // Get the resolution of the CLOCK_REALTIME clock.
    clock_getres(CLOCK_REALTIME, &tp);

    // Map the POSIX timespec to the wasi_datetime_t struct.
    return (wasi_datetime_t){
        .seconds = (uint64_t)tp.tv_sec,
        .nanoseconds = (uint32_t)tp.tv_nsec,
    };
}

/**
 * @brief Read the current value of the monotonic clock.
 * @details Implements the `now` function from the `wasi:clocks/monotonic-clock`
 *          interface. This clock is non-decreasing and is intended for
 *          measuring elapsed time.
 * @return A wasi_instant_t value representing the current time in nanoseconds.
 */
wasi_instant_t
wasi_monotonic_clock_now(void)
{
    struct timespec tp;

    // Use CLOCK_MONOTONIC for a steady, non-decreasing time source,
    // suitable for measuring intervals.
    clock_gettime(CLOCK_MONOTONIC, &tp);

    // Convert the timespec struct into a single u64 nanosecond value.
    return (((wasi_instant_t)tp.tv_sec) * 1000000000) + tp.tv_nsec;
}

/**
 * @brief Query the resolution of the monotonic clock.
 * @details Implements the `resolution` function from the
 *          `wasi:clocks/monotonic-clock` interface. This represents the
 *          smallest measurable unit of time for this clock.
 * @return A wasi_duration_t value representing the clock's resolution in
 * nanoseconds.
 */
wasi_duration_t
wasi_monotonic_clock_resolution(void)
{
    struct timespec tp;

    // Get the resolution of the CLOCK_MONOTONIC clock.
    clock_getres(CLOCK_MONOTONIC, &tp);

    // Convert the timespec struct into a single u64 nanosecond value.
    return (((wasi_duration_t)tp.tv_sec) * 1000000000) + tp.tv_nsec;
}

/**
 * @brief Static helper to create a pollable timer using a timerfd.
 * @details This is the core Linux implementation for both `subscribe`
 *          functions in the `wasi:clocks/monotonic-clock` interface.
 * @param when The time value in nanoseconds (can be absolute or relative).
 * @param flags Flags for `timerfd_settime` (0 for relative, TFD_TIMER_ABSTIME
 *              for absolute).
 * @return A pollable context wrapping the timerfd. On error, `fd` is -1.
 */
static wasi_pollable_context_t
wasi_monotonic_clock_subscribe(wasi_duration_t when, int flags)
{
    wasi_pollable_context_t pollable = { .fd = -1,
                                         .own_fd = false,
                                         .type = WASI_POLLABLE_IN };

    // Create a timer file descriptor based on the monotonic clock.
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd < 0) {
        return pollable;
    }

    // Convert the u64 nanosecond value into the required itimerspec struct.
    struct itimerspec ts;
    ts.it_value.tv_sec = when / 1000000000;
    ts.it_value.tv_nsec = when % 1000000000;

    // A zero duration would disarm the timer. To ensure it fires for a
    // duration of 0, we set it to the smallest possible non-zero value.
    if (ts.it_value.tv_sec == 0 && ts.it_value.tv_nsec == 0) {
        ts.it_value.tv_nsec = 1;
    }

    // Set the interval to zero to create a one-shot (non-repeating) timer.
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = 0;

    // Arm the timer with the specified time value and flags.
    if (timerfd_settime(tfd, flags, &ts, NULL) < 0) {
        close(tfd);
        return pollable;
    }

    SET_INPUT_POLLABLE(&pollable, tfd, true);
    return pollable;
}

/**
 * @brief Create a pollable that resolves at a specific moment in time.
 * @details Implements the `subscribe-instant` function from the
 *          `wasi:clocks/monotonic-clock` interface.
 * @param when An absolute point in time (`instant`) when the timer should fire.
 * @return A pollable handle (file descriptor) for the timer.
 */
wasi_pollable_context_t
wasi_monotonic_clock_subscribe_instant(wasi_instant_t when)
{
    // Call the helper with the TFD_TIMER_ABSTIME flag to treat `when` as an
    // absolute timestamp.
    return wasi_monotonic_clock_subscribe(when, TFD_TIMER_ABSTIME);
}

/**
 * @brief Create a pollable that resolves after a duration of time.
 * @details Implements the `subscribe-duration` function from the
 *          `wasi:clocks/monotonic-clock` interface.
 * @param when A relative duration in nanoseconds to wait before the timer
 * fires.
 * @return A pollable handle (file descriptor) for the timer.
 */
wasi_pollable_context_t
wasi_monotonic_clock_subscribe_duration(wasi_duration_t when)
{
    // Call the helper with a flag of 0 to treat `when` as a relative duration.
    return wasi_monotonic_clock_subscribe(when, 0);
}
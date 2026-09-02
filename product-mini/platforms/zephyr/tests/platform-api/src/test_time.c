/*
 * Copyright (C) 2026 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <zephyr/kernel.h>

#include "test_common.h"

#include "platform_api_vmcore.h"

#define MIN_SLEEP_ELAPSED_US 10000ULL
#define MAX_ELAPSED_US 500000ULL
#define MAX_SLEEP_CPU_US 10000ULL
#define BUSY_WORK_ITERATIONS 20000000U

struct platform_time_fixture {
    uint64 first;
    uint64 second;
};

ZTEST_DMEM static struct platform_time_fixture time_results = { 0 };

static uint64
one_tick_us(void)
{
    return k_ticks_to_us_floor64(1) + 1U;
}

static void
busy_work(void)
{
    volatile uint32_t value = 0;

    for (uint32_t i = 0; i < BUSY_WORK_ITERATIONS; ++i) {
        value += i;
    }
    ARG_UNUSED(value);
}

static void
assert_cpu_time_available(void)
{
#ifndef CONFIG_THREAD_RUNTIME_STATS
    ztest_test_skip();
#endif
#if defined(CONFIG_WAMR_TEST_USER_MODE) && defined(CONFIG_ARC)
    /* FIXME: Zephyr 3.7 k_thread_runtime_stats_get() calls privileged
     * arch_irq_lock() from qemu_arc user threads. */
    ztest_test_skip();
#endif
}

static void *
time_setup(void)
{
    return &time_results;
}

static void
time_before(void *fixture)
{
    struct platform_time_fixture *results = fixture;

    results->first = 0U;
    results->second = 0U;
    pool_before(fixture);
}

ZTEST_SUITE(platform_time, NULL, time_setup, time_before, pool_after, NULL);

WAMR_CONTEXT_TEST_F(platform_time, test_boot_time_is_nondecreasing)
{
    fixture->first = os_time_get_boot_us();
    fixture->second = os_time_get_boot_us();

    zassert_true(fixture->second >= fixture->first, "boot time regressed");
}

WAMR_CONTEXT_TEST_F(platform_time, test_boot_time_advances_with_kernel_sleep)
{
    fixture->first = os_time_get_boot_us();

    k_sleep(K_MSEC(20));
    fixture->second = os_time_get_boot_us() - fixture->first;
    zassert_true(fixture->second >= MIN_SLEEP_ELAPSED_US,
                 "kernel sleep advanced boot time too little: %llu us",
                 fixture->second);
    zassert_true(fixture->second <= MAX_ELAPSED_US,
                 "kernel sleep advanced boot time too much: %llu us",
                 fixture->second);
}

WAMR_CONTEXT_TEST_F(platform_time, test_boot_time_matches_zephyr_uptime_units)
{
    fixture->first = (uint64)k_uptime_get() * 1000ULL;
    fixture->second = os_time_get_boot_us();
    fixture->second = fixture->second >= fixture->first
                          ? fixture->second - fixture->first
                          : fixture->first - fixture->second;

    zassert_true(fixture->second <= one_tick_us(),
                 "boot time differs from Zephyr uptime by %llu us",
                 fixture->second);
}

WAMR_CONTEXT_TEST_F(platform_time, test_repeated_boot_time_reads_do_not_regress)
{
    fixture->first = os_time_get_boot_us();

    for (size_t i = 0; i < 64U; ++i) {
        fixture->second = os_time_get_boot_us();

        zassert_true(fixture->second >= fixture->first,
                     "boot time regressed at read %zu", i);
        fixture->first = fixture->second;
    }
}

WAMR_CONTEXT_TEST_F(platform_time, test_thread_cpu_time_is_nondecreasing)
{
    assert_cpu_time_available();
    fixture->first = os_time_thread_cputime_us();
    for (size_t i = 0; i < 64U; ++i) {
        fixture->second = os_time_thread_cputime_us();

        zassert_true(fixture->second >= fixture->first,
                     "CPU time regressed at read %zu", i);
        fixture->first = fixture->second;
    }
}

WAMR_CONTEXT_TEST_F(platform_time, test_cpu_time_increases_during_busy_work)
{
#if defined(CONFIG_ARCH_POSIX)
    /* FIXME: native_sim's CONFIG_THREAD_RUNTIME_STATS execution cycle counter
     * does not advance for the current thread's busy work. */
    ztest_test_skip();
#endif
    assert_cpu_time_available();
    fixture->first = os_time_thread_cputime_us();
    busy_work();
    fixture->second = os_time_thread_cputime_us();
    zassert_true(fixture->second > fixture->first,
                 "CPU time did not advance during work");
}

WAMR_CONTEXT_TEST_F(platform_time, test_cpu_time_excludes_most_sleep_time)
{
    uint64 boot_before;
    uint64 boot_elapsed;

    assert_cpu_time_available();
    boot_before = os_time_get_boot_us();
    fixture->first = os_time_thread_cputime_us();
    k_sleep(K_MSEC(20));
    boot_elapsed = os_time_get_boot_us() - boot_before;
    fixture->second = os_time_thread_cputime_us();
    zassert_true(boot_elapsed >= MIN_SLEEP_ELAPSED_US,
                 "kernel sleep was shorter than %llu us", MIN_SLEEP_ELAPSED_US);
    zassert_true(boot_elapsed <= MAX_ELAPSED_US,
                 "kernel sleep exceeded %llu us", MAX_ELAPSED_US);
    zassert_true(fixture->second >= fixture->first,
                 "CPU time regressed while sleeping");
    zassert_true(fixture->second - fixture->first <= MAX_SLEEP_CPU_US,
                 "sleep consumed too much CPU time: %llu us",
                 fixture->second - fixture->first);
}

WAMR_CONTEXT_TEST_F(platform_time, test_time_conversion_boundary_is_monotonic)
{
    fixture->first = os_time_get_boot_us();

    k_sleep(K_MSEC(20));
    fixture->second = os_time_get_boot_us();
    zassert_true(fixture->second >= fixture->first,
                 "boot time regressed across a tick");
    zassert_true(fixture->second - fixture->first >= MIN_SLEEP_ELAPSED_US,
                 "conversion did not advance across a tick");
    zassert_true(fixture->second - fixture->first <= MAX_ELAPSED_US,
                 "conversion advanced too far across a tick");
}

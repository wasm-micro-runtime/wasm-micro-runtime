/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

extern "C" {
#include "wasi_p2_clocks.h"
}

class WasiP2ClocksTest : public testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override {}
};

// Test `wasi_wall_clock_now` to ensure it returns a reasonable timestamp.
TEST_F(WasiP2ClocksTest, WallClockNow) {
    wasi_datetime_t now = wasi_wall_clock_now();
    ASSERT_GT(now.seconds, 0);
    ASSERT_LE(now.nanoseconds, 1000000000);

    // Check that the returned time is close to the system's real-time clock.
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    ASSERT_NEAR(now.seconds, tp.tv_sec, 1);
}

// Test `wasi_wall_clock_resolution` to ensure it matches the system's resolution.
TEST_F(WasiP2ClocksTest, WallClockResolution) {
    wasi_datetime_t res = wasi_wall_clock_resolution();
    ASSERT_GT(res.nanoseconds, 0);
    ASSERT_LE(res.seconds, 1);
    ASSERT_LE(res.nanoseconds, 1000000000);

    // Compare with the resolution from the underlying POSIX clock.
    struct timespec tp;
    clock_getres(CLOCK_REALTIME, &tp);
    ASSERT_EQ(res.seconds, tp.tv_sec);
    ASSERT_EQ(res.nanoseconds, tp.tv_nsec);
}

// Test `wasi_monotonic_clock_now` and `wasi_monotonic_clock_resolution`.
TEST_F(WasiP2ClocksTest, MonotonicClockNowAndResolution) {
    wasi_instant_t now1 = wasi_monotonic_clock_now();
    usleep(1000); // sleep for 1ms
    wasi_instant_t now2 = wasi_monotonic_clock_now();
    wasi_duration_t res = wasi_monotonic_clock_resolution();

    ASSERT_GT(now1, 0);
    ASSERT_GT(now2, now1); // Time should advance.
    ASSERT_GT(res, 0);
    ASSERT_LE(res, 1000000000); // Resolution should be <= 1 second.
}

// Test `wasi_monotonic_clock_subscribe_duration` with a short duration.
TEST_F(WasiP2ClocksTest, MonotonicClockSubscribeDuration) {
    wasi_pollable_context_t pollable = wasi_monotonic_clock_subscribe_duration(1000000); // 1ms
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd;
    pfd.fd = pollable.fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 100); // 100ms timeout
    ASSERT_GT(ret, 0); // The timer should fire.
    ASSERT_TRUE(pfd.revents & POLLIN);

    // Check that the returned file descriptor is valid.
    struct stat buf;
    ASSERT_EQ(fstat(pollable.fd, &buf), 0);

    close(pollable.fd);
}

// Test `wasi_monotonic_clock_subscribe_duration` with a large duration to check for overflows.
TEST_F(WasiP2ClocksTest, MonotonicClockSubscribeDurationLarge) {
    wasi_pollable_context_t pollable = wasi_monotonic_clock_subscribe_duration(10000000000); // 10s
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd;
    pfd.fd = pollable.fd;
    pfd.events = POLLIN;

    // The timer should not be ready immediately.
    int ret = poll(&pfd, 1, 0);
    ASSERT_EQ(ret, 0);

    close(pollable.fd);
}

// Test `wasi_monotonic_clock_subscribe_duration` with a zero duration.
TEST_F(WasiP2ClocksTest, MonotonicClockSubscribeDurationZero) {
    wasi_pollable_context_t pollable = wasi_monotonic_clock_subscribe_duration(0);
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd;
    pfd.fd = pollable.fd;
    pfd.events = POLLIN;

    // A zero-duration timer should fire immediately.
    int ret = poll(&pfd, 1, 1);
    ASSERT_GT(ret, 0);
    ASSERT_TRUE(pfd.revents & POLLIN);

    close(pollable.fd);
}

// Test `wasi_monotonic_clock_subscribe_instant` with an instant in the past.
TEST_F(WasiP2ClocksTest, MonotonicClockSubscribeInstantPast) {
    wasi_instant_t now = wasi_monotonic_clock_now();
    wasi_pollable_context_t pollable = wasi_monotonic_clock_subscribe_instant(now - 1000000); // 1ms in the past
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd;
    pfd.fd = pollable.fd;
    pfd.events = POLLIN;

    // A timer for a past instant should fire immediately.
    int ret = poll(&pfd, 1, 1);
    ASSERT_GT(ret, 0);
    ASSERT_TRUE(pfd.revents & POLLIN);

    close(pollable.fd);
}

// Test `wasi_monotonic_clock_subscribe_instant` with an instant in the future.
TEST_F(WasiP2ClocksTest, MonotonicClockSubscribeInstantFuture) {
    wasi_instant_t now = wasi_monotonic_clock_now();
    wasi_pollable_context_t pollable = wasi_monotonic_clock_subscribe_instant(now + 1000000); // 1ms in the future
    ASSERT_NE(pollable.fd, -1);

    struct pollfd pfd;
    pfd.fd = pollable.fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 100); // 100ms timeout
    ASSERT_GT(ret, 0); // The timer should fire.
    ASSERT_TRUE(pfd.revents & POLLIN);

    close(pollable.fd);
}

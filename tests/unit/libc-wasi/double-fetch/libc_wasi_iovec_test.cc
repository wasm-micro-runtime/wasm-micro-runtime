/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * Tests for the iovec handling of libc-wasi fd and socket APIs.
 *
 * The guest iovec array can be modified by another wasm thread while the
 * host reads it. These cases check the normal and out-of-range paths, and a
 * concurrent case where a wasi-threads thread rewrites the array during
 * sock_send.
 */

#include "libc_wasi_test_fixture.h"

static const char *IOVEC_OPS = "iovec_ops.wasm";
static const char *IOVEC_THREADS = "iovec_threads.wasm";

/* fd_write to stdout: the payload must be counted in nwritten. */
TEST_F(LibcWasiTest, FdWriteSingleIovec)
{
    load_module(IOVEC_OPS);

    const char *payload = "libc-wasi iovec ok\n";
    uint32_t len = (uint32_t)strlen(payload);

    set_single_iovec(payload, IOVS_OFF, DATA_OFF, len);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 1, IOVS_OFF, 1, OUT_OFF };
    ASSERT_TRUE(call("call_fd_write", argv));

    EXPECT_EQ(argv[0], 0u);
    EXPECT_EQ(read_u32(OUT_OFF), len);
}

/* Two iovec entries are written in order and the total is reported. */
TEST_F(LibcWasiTest, FdWriteMultipleIovecs)
{
    load_module(IOVEC_OPS);

    memcpy(guest(DATA_OFF), "ab", 2);
    memcpy(guest(DATA_OFF + 0x10), "cde\n", 4);
    guest_iovec_t iovs[2] = { { DATA_OFF, 2 }, { DATA_OFF + 0x10, 4 } };
    memcpy(guest(IOVS_OFF), iovs, sizeof(iovs));
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 1, IOVS_OFF, 2, OUT_OFF };
    ASSERT_TRUE(call("call_fd_write", argv));

    EXPECT_EQ(argv[0], 0u);
    EXPECT_EQ(read_u32(OUT_OFF), 6u);
}

/* An iovec whose buffer lies outside guest memory is rejected. */
TEST_F(LibcWasiTest, FdWriteOutOfRangeIovecFails)
{
    load_module(IOVEC_OPS);

    set_iovec(0xFFFF0000, 0x100);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 1, IOVS_OFF, 1, OUT_OFF };
    expect_rejected("call_fd_write", argv);
    EXPECT_EQ(read_u32(OUT_OFF), 0u);
}

/* An iovec array that lies outside guest memory is rejected. */
TEST_F(LibcWasiTest, FdWriteIovecArrayOutOfRangeFails)
{
    load_module(IOVEC_OPS);

    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 1, 0xFFFF0000, 2, OUT_OFF };
    expect_rejected("call_fd_write", argv);
}

/* A valid iovec passes conversion and reaches the fd layer (EBADF). */
TEST_F(LibcWasiTest, FdReadValidIovecReachesFdLayer)
{
    load_module(IOVEC_OPS);

    set_iovec(DATA_OFF, 8);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, OUT_OFF };
    ASSERT_TRUE(call("call_fd_read", argv));

    EXPECT_EQ(argv[0], WASI_EBADF);
}

/* pread and pwrite take the same iovec path as read and write. */
TEST_F(LibcWasiTest, FdPreadPwriteValidIovecReachesFdLayer)
{
    load_module(IOVEC_OPS);

    set_iovec(DATA_OFF, 8);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, 0, OUT_OFF };
    ASSERT_TRUE(call("call_fd_pread", argv));
    EXPECT_EQ(argv[0], WASI_EBADF);

    argv = { 99, IOVS_OFF, 1, 0, 0, OUT_OFF };
    ASSERT_TRUE(call("call_fd_pwrite", argv));
    EXPECT_EQ(argv[0], WASI_EBADF);
}

/* pread rejects an out-of-range iovec before reaching the fd layer. */
TEST_F(LibcWasiTest, FdPreadOutOfRangeIovecFails)
{
    load_module(IOVEC_OPS);

    set_iovec(0xFFFF0000, 0x100);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, 0, OUT_OFF };
    expect_rejected("call_fd_pread", argv);
}

/* A valid iovec reaches sock_send's send path. */
TEST_F(LibcWasiTest, SockSendValidIovecReachesSocketLayer)
{
    load_module(IOVEC_OPS);

    set_single_iovec("payload", IOVS_OFF, DATA_OFF,
                     (uint32_t)strlen("payload"));
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, OUT_OFF };
    ASSERT_TRUE(call("call_sock_send", argv));

    EXPECT_EQ(argv[0], WASI_EBADF);
}

/* An out-of-range iovec is rejected with EINVAL and not sent. */
TEST_F(LibcWasiTest, SockSendOutOfRangeIovecFails)
{
    load_module(IOVEC_OPS);

    set_iovec(0xFFFF0000, 0x100);
    write_u32(OUT_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, OUT_OFF };
    expect_rejected("call_sock_send", argv);
}

/* An empty iovec list still goes through the conversion path. */
TEST_F(LibcWasiTest, SockSendEmptyIovecList)
{
    load_module(IOVEC_OPS);

    write_u32(OUT_OFF, 0xFFFFFFFF);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 0, 0, OUT_OFF };
    ASSERT_TRUE(call("call_sock_send", argv));

    EXPECT_EQ(argv[0], WASI_EBADF);
}

/* sock_recv rejects an out-of-range iovec before receiving. */
TEST_F(LibcWasiTest, SockRecvOutOfRangeIovecFails)
{
    load_module(IOVEC_OPS);

    set_iovec(0xFFFF0000, 0x100);
    write_u32(OUT_OFF, 0);
    write_u32(OUT2_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, OUT_OFF, OUT2_OFF };
    expect_rejected("call_sock_recv", argv);
}

/* A valid iovec reaches the receive path. */
TEST_F(LibcWasiTest, SockRecvValidIovecReachesSocketLayer)
{
    load_module(IOVEC_OPS);

    set_iovec(DATA_OFF, 16);
    write_u32(OUT_OFF, 0);
    write_u32(OUT2_OFF, 0);

    std::vector<uint32_t> argv = { 99, IOVS_OFF, 1, 0, OUT_OFF, OUT2_OFF };
    ASSERT_TRUE(call("call_sock_recv", argv));

    EXPECT_EQ(argv[0], WASI_EBADF);
}

/*
 * A wasi-threads thread rewrites iovec[0].buf_len between a valid length and
 * one past the end of memory while the main thread calls sock_send. Every
 * call must return either EBADF (valid iovec) or EINVAL (invalid iovec), and
 * the runtime must not crash.
 */
TEST_F(LibcWasiTest, SockSendConcurrentIovecRewrite)
{
    load_module(IOVEC_THREADS);

    std::vector<uint32_t> argv = { 20000 };
    ASSERT_TRUE(call("run", argv)) << "call raised an exception";

    EXPECT_EQ(argv[0], 0u) << "unexpected sock_send results under race";
}

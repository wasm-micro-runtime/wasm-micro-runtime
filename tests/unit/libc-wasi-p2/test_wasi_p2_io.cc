/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <chrono>
#include <thread>
#include <sys/socket.h>

extern "C" {
#include "wasi_p2_io.h"
#include "wasi_p2_common.h"
#include "wasi_p2_error.h"
#include "component-model/wasm_component_host_resource.h"
}

// Test fixture for wasi:io tests.
class WasiP2IoTest : public testing::Test {
protected:
    int pipe_fds[2] = {-1, -1};
    wasi_pollable_context_t in_pollable, out_pollable;

    void SetUp() override {
        wasm_runtime_init();
        if (pipe(pipe_fds) == -1) {
            FAIL() << "Failed to create pipe";
        }
        SET_INPUT_POLLABLE(&in_pollable, pipe_fds[0], true);
        SET_OUTPUT_POLLABLE(&out_pollable, pipe_fds[1], true);

        // Instantiate the global host resource table
        bool success = instantiate_host_resource_table();
        ASSERT_TRUE(success);
    }

    void TearDown() override {
        if (pipe_fds[0] != -1) {
            close(pipe_fds[0]);
        }
        if (pipe_fds[1] != -1) {
            close(pipe_fds[1]);
        }
        // Destroy the global host resource table
        destroy_host_resource_table();
        wasm_runtime_destroy();
    }
};

// Test `pollable.ready` and `pollable.block`.
TEST_F(WasiP2IoTest, wasi_pollable_ready_and_block) {
    ASSERT_FALSE(wasi_pollable_ready(&in_pollable));

    char buf[1] = {'a'};
    write(pipe_fds[1], buf, 1);

    ASSERT_TRUE(wasi_pollable_ready(&in_pollable));

    wasi_pollable_block(&in_pollable);
    read(pipe_fds[0], buf, 1);
}

// Test `poll.poll`.
TEST_F(WasiP2IoTest, wasi_poll) {
    int pipe2_fds[2];
    ASSERT_NE(pipe(pipe2_fds), -1);
    wasi_pollable_context_t in_pollable2;
    SET_INPUT_POLLABLE(&in_pollable2, pipe2_fds[0], true);

    const wasi_pollable_context_t* pollables[] = { &in_pollable, &in_pollable2 };
    wasi_list_u32_t ret;

    char buf[1] = {'a'};
    write(pipe2_fds[1], buf, 1);

    wasi_poll(pollables, 2, &ret);

    ASSERT_EQ(ret.len, 1);
    ASSERT_EQ(ret.buf[0], 1);

    wasm_runtime_free(ret.buf);
    close(pipe2_fds[0]);
    close(pipe2_fds[1]);
}

// Test non-blocking read from an input stream.
TEST_F(WasiP2IoTest, wasi_input_stream_read) {
    char write_buf[] = "hello";
    write(pipe_fds[1], write_buf, sizeof(write_buf));

    wasi_result_list_u8_stream_error_t ret;
    wasi_input_stream_read(pipe_fds[0], sizeof(write_buf), &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok.buf_len, sizeof(write_buf));
    ASSERT_EQ(memcmp(ret.u.ok.buf, write_buf, sizeof(write_buf)), 0);

    wasm_runtime_free(ret.u.ok.buf);
}

// Test blocking read from an input stream.
TEST_F(WasiP2IoTest, wasi_input_stream_blocking_read) {
    char write_buf[] = "hello";
    write(pipe_fds[1], write_buf, sizeof(write_buf));

    wasi_result_list_u8_stream_error_t ret;
    wasi_input_stream_blocking_read(pipe_fds[0], sizeof(write_buf), &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok.buf_len, sizeof(write_buf));
    ASSERT_EQ(memcmp(ret.u.ok.buf, write_buf, sizeof(write_buf)), 0);

    wasm_runtime_free(ret.u.ok.buf);
}

// Test skipping bytes on a seekable input stream.
TEST_F(WasiP2IoTest, wasi_input_stream_skip) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    char write_buf[] = "hello world";
    write(fd, write_buf, sizeof(write_buf));
    lseek(fd, 0, SEEK_SET);

    wasi_result_u64_stream_error_t ret;
    wasi_input_stream_skip(fd, 6, &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, 6);

    char read_buf[6];
    read(fd, read_buf, 5);
    read_buf[5] = 0;
    ASSERT_STREQ(read_buf, "world");

    fclose(tmpf);
}

// Test that `check-write` correctly reports a full socket buffer after a non-blocking flush.
TEST_F(WasiP2IoTest, wasi_output_stream_non_blocking_flush_socket) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    char write_buf[] = "hello";
    wasi_list_u8_t payload = { .buf = (uint8_t *)write_buf, .buf_len = sizeof(write_buf) };
    wasi_result_void_stream_error_t write_ret;
    wasi_output_stream_write(fds[0], &payload, &write_ret);
    ASSERT_FALSE(write_ret.is_err);

    wasi_result_void_stream_error_t flush_ret;
    wasi_output_stream_flush(fds[0], &flush_ret);
    ASSERT_FALSE(flush_ret.is_err);

    wasi_result_u64_stream_error_t check_ret;
    wasi_output_stream_check_write(fds[0], &check_ret);
    ASSERT_FALSE(check_ret.is_err);
    ASSERT_EQ(check_ret.u.ok, 0);

    char read_buf[sizeof(write_buf)];
    read(fds[1], read_buf, sizeof(write_buf));

    // After reading, the socket buffer should be empty and check_write should work again
    // It might take a moment for the kernel to update the buffer status
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    wasi_output_stream_check_write(fds[0], &check_ret);
    ASSERT_FALSE(check_ret.is_err);
    ASSERT_GT(check_ret.u.ok, 0);

    close(fds[0]);
    close(fds[1]);
}

// Test `check-write` on a writable output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_check_write) {
    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_check_write(pipe_fds[1], &ret);
    ASSERT_FALSE(ret.is_err);
    ASSERT_GT(ret.u.ok, 0);
}

// Test the accuracy of `check-write` on a socket.
TEST_F(WasiP2IoTest, wasi_output_stream_check_write_socket_accuracy) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    wasi_result_u64_stream_error_t ret1;
    wasi_output_stream_check_write(fds[0], &ret1);
    ASSERT_FALSE(ret1.is_err);
    ASSERT_GT(ret1.u.ok, 0);

    char write_buf[] = "hello";
    write(fds[0], write_buf, sizeof(write_buf));

    wasi_result_u64_stream_error_t ret2;
    wasi_output_stream_check_write(fds[0], &ret2);
    ASSERT_FALSE(ret2.is_err);
    ASSERT_LT(ret2.u.ok, ret1.u.ok);

    // Set a specific buffer size
    int sndbuf_size = 8192;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf_size, sizeof(sndbuf_size));

    wasi_result_u64_stream_error_t ret3;
    wasi_output_stream_check_write(fds[0], &ret3);
    ASSERT_FALSE(ret3.is_err);
    // The kernel usually doubles the value set by setsockopt
    ASSERT_LE(ret3.u.ok, sndbuf_size * 2);

    close(fds[0]);
    close(fds[1]);
}

// Test writing to an output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_write) {
    wasi_list_u8_t payload = { .buf = (uint8_t *)"hello", .buf_len = 5 };
    wasi_result_void_stream_error_t ret;
    wasi_output_stream_write(pipe_fds[1], &payload, &ret);
    ASSERT_FALSE(ret.is_err);

    char read_buf[6];
    read(pipe_fds[0], read_buf, 5);
    read_buf[5] = 0;
    ASSERT_STREQ(read_buf, "hello");
}

// Test that a partial write to a full non-blocking stream fails correctly.
TEST_F(WasiP2IoTest, wasi_output_stream_write_partial)
{
    // Make the pipe non-blocking
    fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK);

    // Fill the pipe buffer
    const int buf_size = 1024 * 1024;
    char *buf = (char *)wasm_runtime_malloc(buf_size);
    ASSERT_NE(buf, nullptr);
    memset(buf, 'a', buf_size);
    while (write(pipe_fds[1], buf, 1024) > 0)
        ;
    wasm_runtime_free(buf);

    // Try to write more data
    wasi_list_u8_t payload = { .buf = (uint8_t *)"hello", .buf_len = 5 };
    wasi_result_void_stream_error_t ret;
    wasi_output_stream_write(pipe_fds[1], &payload, &ret);

    ASSERT_TRUE(ret.is_err);
    ASSERT_EQ(ret.u.err.kind, WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED);
}

// Test blocking write and flush to an output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_blocking_write_and_flush) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    wasi_list_u8_t payload = { .buf = (uint8_t *)"hello", .buf_len = 5 };
    wasi_result_void_stream_error_t ret;
    wasi_output_stream_blocking_write_and_flush(fd, &payload, &ret);
    ASSERT_FALSE(ret.is_err);

    lseek(fd, 0, SEEK_SET);
    char read_buf[6];
    read(fd, read_buf, 5);
    read_buf[5] = 0;
    ASSERT_STREQ(read_buf, "hello");

    fclose(tmpf);
}

// Test flushing an output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_flush) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    wasi_result_void_stream_error_t ret;
    wasi_output_stream_flush(fd, &ret);
    ASSERT_FALSE(ret.is_err);

    fclose(tmpf);
}

// Test blocking flush on an output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_blocking_flush) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    wasi_result_void_stream_error_t ret;
    wasi_output_stream_blocking_flush(fd, &ret);
    ASSERT_FALSE(ret.is_err);

    fclose(tmpf);
}

// Test writing zeroes to an output stream.
TEST_F(WasiP2IoTest, wasi_output_stream_write_zeroes) {
    wasi_result_void_stream_error_t ret;
    wasi_output_stream_write_zeroes(pipe_fds[1], 5, &ret);
    ASSERT_FALSE(ret.is_err);

    char read_buf[6];
    read(pipe_fds[0], read_buf, 5);
    read_buf[5] = 0;
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(read_buf[i], 0);
    }
}

// Test writing zeroes to a file.
TEST_F(WasiP2IoTest, wasi_output_stream_write_zeroes_file) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    char write_buf[] = "hello";
    write(fd, write_buf, sizeof(write_buf));

    wasi_result_void_stream_error_t ret;
    wasi_output_stream_write_zeroes(fd, 10, &ret);
    ASSERT_FALSE(ret.is_err);

    struct stat statbuf;
    fstat(fd, &statbuf);
    ASSERT_EQ(statbuf.st_size, sizeof(write_buf) + 10);

    lseek(fd, sizeof(write_buf), SEEK_SET);
    char read_buf[11];
    read(fd, read_buf, 10);
    read_buf[10] = 0;
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ(read_buf[i], 0);
    }

    fclose(tmpf);
}

// Test blocking write zeroes and flush.
TEST_F(WasiP2IoTest, wasi_output_stream_blocking_write_zeroes_and_flush) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int fd = fileno(tmpf);

    wasi_result_void_stream_error_t ret;
    wasi_output_stream_blocking_write_zeroes_and_flush(fd, 5, &ret);
    ASSERT_FALSE(ret.is_err);

    lseek(fd, 0, SEEK_SET);
    char read_buf[6];
    read(fd, read_buf, 5);
    read_buf[5] = 0;
    for (int i = 0; i < 5; i++) {
        ASSERT_EQ(read_buf[i], 0);
    }

    fclose(tmpf);
}

// Test reading from an input stream at EOF.
TEST_F(WasiP2IoTest, wasi_input_stream_read_eof)
{
    close(pipe_fds[1]); // Close the write end to simulate EOF

    wasi_result_list_u8_stream_error_t ret;
    wasi_input_stream_read(pipe_fds[0], 10, &ret);

    ASSERT_TRUE(ret.is_err);
    ASSERT_EQ(ret.u.err.kind, WASI_STREAM_ERROR_KIND_CLOSED);

    // Second read on a closed stream should return an empty list as well
    wasi_input_stream_read(pipe_fds[0], 10, &ret);
    ASSERT_EQ(ret.u.err.kind, WASI_STREAM_ERROR_KIND_CLOSED);
}

// Test that `splice` fails when the destination stream is closed.
TEST_F(WasiP2IoTest, wasi_output_stream_splice_write_error)
{
    int src_pipe_fds[2];
    ASSERT_NE(pipe(src_pipe_fds), -1);

    char write_buf[] = "hello";
    write(src_pipe_fds[1], write_buf, sizeof(write_buf));

    close(pipe_fds[1]); // Close the write end of the destination pipe

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_splice(
        pipe_fds[1], src_pipe_fds[0], sizeof(write_buf), &ret);

    ASSERT_TRUE(ret.is_err);

    close(src_pipe_fds[0]);
    close(src_pipe_fds[1]);
}

// Test splicing a large amount of data.
TEST_F(WasiP2IoTest, wasi_output_stream_splice_large_data)
{
    int fds[2];
    ASSERT_NE(pipe(fds), -1);

    const int large_size = 65536;
    char *large_buf = (char *)wasm_runtime_malloc(large_size);
    ASSERT_NE(large_buf, nullptr);
    for (int i = 0; i < large_size; i++) {
        large_buf[i] = i % 256;
    }

    // Make pipe non-blocking for writing
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    // Write all data to pipe
    int written = 0;
    while (written < large_size) {
        int res = write(fds[1], large_buf + written, large_size - written);
        if (res > 0) {
            written += res;
        }
    }

    // Splice from pipe to a temporary file
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int tmp_fd = fileno(tmpf);

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_blocking_splice(tmp_fd, fds[0], large_size, &ret);
    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, large_size);

    // Verify the content of the temporary file
    lseek(tmp_fd, 0, SEEK_SET);
    char *read_buf = (char *)wasm_runtime_malloc(large_size);
    ASSERT_NE(read_buf, nullptr);
    ASSERT_EQ(read(tmp_fd, read_buf, large_size), large_size);
    ASSERT_EQ(memcmp(large_buf, read_buf, large_size), 0);

    wasm_runtime_free(large_buf);
    wasm_runtime_free(read_buf);
    fclose(tmpf);
    close(fds[0]);
    close(fds[1]);
}

void *
splice_to_pipe_after_delay(void *arg)
{
    int *pipe_fds = (int *)arg;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char write_buf[] = "hello";
    write(pipe_fds[1], write_buf, sizeof(write_buf));
    return NULL;
}

// Test that `blocking-splice` actually blocks.
TEST_F(WasiP2IoTest, wasi_output_stream_blocking_splice_blocks)
{
    pthread_t thread;
    int src_pipe_fds[2];
    ASSERT_NE(pipe(src_pipe_fds), -1);

    pthread_create(&thread, NULL, splice_to_pipe_after_delay, src_pipe_fds);

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_blocking_splice(
        pipe_fds[1], src_pipe_fds[0], 5, &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, 5);

    char read_buf[6];
    read(pipe_fds[0], read_buf, 5);
    read_buf[5] = 0;
    ASSERT_STREQ(read_buf, "hello");

    pthread_join(thread, NULL);
    close(src_pipe_fds[0]);
    close(src_pipe_fds[1]);
}

void *
write_to_pipe_after_delay(void *arg)
{
    int *pipe_fds = (int *)arg;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char write_buf[] = "hello";
    write(pipe_fds[1], write_buf, sizeof(write_buf));
    return NULL;
}

// Test that `blocking-read` actually blocks.
TEST_F(WasiP2IoTest, wasi_input_stream_blocking_read_blocks)
{
    pthread_t thread;
    pthread_create(&thread, NULL, write_to_pipe_after_delay, pipe_fds);

    wasi_result_list_u8_stream_error_t ret;
    wasi_input_stream_blocking_read(pipe_fds[0], 5, &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok.buf_len, 5);
    ASSERT_EQ(memcmp(ret.u.ok.buf, "hello", 5), 0);

    wasm_runtime_free(ret.u.ok.buf);
    pthread_join(thread, NULL);
}

// Test skipping bytes on a non-seekable stream (a pipe).
TEST_F(WasiP2IoTest, wasi_input_stream_skip_non_seekable)
{
    char write_buf[] = "hello world";
    write(pipe_fds[1], write_buf, sizeof(write_buf));

    wasi_result_u64_stream_error_t ret;
    wasi_input_stream_skip(pipe_fds[0], 6, &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, 6);

    char read_buf[6];
    read(pipe_fds[0], read_buf, 5);
    read_buf[5] = 0;
    ASSERT_STREQ(read_buf, "world");
}

#include <signal.h>
// Test that writing to a closed stream fails correctly.
TEST_F(WasiP2IoTest, wasi_output_stream_write_to_closed)
{
    signal(SIGPIPE, SIG_IGN);
    close(pipe_fds[0]);

    wasi_list_u8_t payload = { .buf = (uint8_t *)"hello", .buf_len = 5 };
    wasi_result_void_stream_error_t ret;
    wasi_output_stream_write(pipe_fds[1], &payload, &ret);
    ASSERT_TRUE(ret.is_err);
    ASSERT_EQ(ret.u.err.kind, WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED);

    // Second write should also fail
    wasi_output_stream_write(pipe_fds[1], &payload, &ret);
    ASSERT_TRUE(ret.is_err);
    ASSERT_EQ(ret.u.err.kind, WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED);
}

void *
skip_from_pipe_after_delay(void *arg)
{
    int *pipe_fds = (int *)arg;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char write_buf[] = "hello";
    write(pipe_fds[1], write_buf, sizeof(write_buf));
    return NULL;
}

// Test that `blocking-skip` actually blocks.
TEST_F(WasiP2IoTest, wasi_input_stream_blocking_skip_blocks)
{
    pthread_t thread;
    pthread_create(&thread, NULL, skip_from_pipe_after_delay, pipe_fds);

    wasi_result_u64_stream_error_t ret;
    wasi_input_stream_blocking_skip(pipe_fds[0], 5, &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, 5);

    pthread_join(thread, NULL);
}

// Test splicing data from a file to a pipe.
TEST_F(WasiP2IoTest, wasi_output_stream_splice_file_to_pipe) {
    FILE *tmpf = tmpfile();
    ASSERT_NE(tmpf, nullptr);
    int src_fd = fileno(tmpf);

    char write_buf[] = "hello world";
    write(src_fd, write_buf, sizeof(write_buf));
    lseek(src_fd, 0, SEEK_SET);

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_splice(pipe_fds[1], src_fd, sizeof(write_buf), &ret);

    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, sizeof(write_buf));

    char read_buf[sizeof(write_buf) + 1];
    read(pipe_fds[0], read_buf, sizeof(write_buf));
    read_buf[sizeof(write_buf)] = 0;
    ASSERT_STREQ(read_buf, write_buf);

    fclose(tmpf);
}

// Test splicing data from one file to another.
TEST_F(WasiP2IoTest, wasi_output_stream_blocking_splice_file_to_file) {
    FILE *src_tmpf = tmpfile();
    ASSERT_NE(src_tmpf, nullptr);
    int src_fd = fileno(src_tmpf);

    FILE *dst_tmpf = tmpfile();
    ASSERT_NE(dst_tmpf, nullptr);
    int dst_fd = fileno(dst_tmpf);

    char write_buf[] = "splice file to file";
    write(src_fd, write_buf, sizeof(write_buf));
    lseek(src_fd, 0, SEEK_SET);

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_blocking_splice(dst_fd, src_fd, sizeof(write_buf), &ret);
    ASSERT_FALSE(ret.is_err);
    ASSERT_EQ(ret.u.ok, sizeof(write_buf));

    char read_buf[sizeof(write_buf) + 1];
    lseek(dst_fd, 0, SEEK_SET);
    read(dst_fd, read_buf, sizeof(write_buf));
    read_buf[sizeof(write_buf)] = 0;
    ASSERT_STREQ(read_buf, write_buf);

    fclose(src_tmpf);
    fclose(dst_tmpf);
}

// Test that `splice` fails when the source stream is unreadable.
TEST_F(WasiP2IoTest, wasi_output_stream_splice_read_error) {
    int src_pipe_fds[2];
    ASSERT_NE(pipe(src_pipe_fds), -1);

    // Close the read end of the source pipe to trigger an error
    close(src_pipe_fds[0]);

    wasi_result_u64_stream_error_t ret;
    wasi_output_stream_splice(pipe_fds[1], src_pipe_fds[0], 10, &ret);

    ASSERT_TRUE(ret.is_err);

    close(src_pipe_fds[1]);
}

extern "C" const char *wasi_error_to_debug_string(HostResource *hr_err);

// Test the `wasi_error_to_debug_string` function.
TEST_F(WasiP2IoTest, wasi_error_to_debug_string_test) {
    uint32_t err = wasi_error_new(STREAM_TYPE_SOCKET, WASI_NETWORK_ERROR_CODE_UNKNOWN);
    HostResourceTable *hr_table = get_global_host_resource_table();
    HostResource *hr = host_resource_table_get(hr_table, err);
    const char *debug_str = wasi_error_to_debug_string(hr);
    ASSERT_NE(debug_str, nullptr);
    ASSERT_STREQ(debug_str, "unknown");

    err = wasi_error_new(STREAM_TYPE_SOCKET, WASI_NETWORK_ERROR_CODE_ACCESS_DENIED);
    hr = host_resource_table_get(hr_table, err);
    debug_str = wasi_error_to_debug_string(hr);
    ASSERT_NE(debug_str, nullptr);
    ASSERT_STREQ(debug_str, "access-denied");
}

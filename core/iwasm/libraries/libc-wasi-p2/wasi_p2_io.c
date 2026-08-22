/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* splice(2) / SPLICE_F_NONBLOCK used below are GNU extensions; request them
   before any system header is pulled in. Defining it in the TU keeps the file
   building where the compile flags don't set it (e.g. CodeQL's autobuild). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "wasi_p2_io.h"
#include "wasi_p2_error.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/uio.h>

#include "wasi_p2_common.h"

#include <sys/stat.h>
#include <sys/ioctl.h>
#if defined(__linux__)
#include <fcntl.h>
#endif
#include <sys/socket.h>
#include <pthread.h>

void
pollable_dtor(void *data)
{
    wasi_pollable_context_t *pollable = (wasi_pollable_context_t *)data;
    if (pollable->own_fd) {
        close(pollable->fd);
    }
}

/**
 * @brief A node in a linked list used to track output streams that are
 *        currently undergoing a `flush` operation.
 */
typedef struct FlushingStreamNode {
    wasi_output_stream_t stream;
    struct FlushingStreamNode *next;
} FlushingStreamNode;

/**
 * @brief The head of the global linked list of flushing streams. Access to this
 *        list must be protected by the `flushing_streams_list_lock`.
 */
static FlushingStreamNode *flushing_streams_list = NULL;

/**
 * @brief A mutex to ensure thread-safe access to the `flushing_streams_list`.
 */
static pthread_mutex_t flushing_streams_list_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Adds an output stream to the global list of flushing streams.
 * @details This is an internal helper function, likely called when a `flush`
 *          operation is initiated on a stream. Thread safety is handled by
 *          the calling function.
 * @param stream The output stream handle to add to the list.
 */
static void
add_to_flushing_list(wasi_output_stream_t stream)
{
    FlushingStreamNode *node = wasm_runtime_malloc(sizeof(FlushingStreamNode));
    if (!node)
        return;

    node->stream = stream;
    node->next = flushing_streams_list;
    flushing_streams_list = node;
}

/**
 * @brief Checks if a stream is in the global list of flushing streams.
 * @details This is an internal helper function used to determine if a `flush`
 *          operation is currently in progress for a given stream. According to
 *          the `wasi:io/streams` specification, `check-write` should return 0
 *          while a flush is ongoing. Thread safety is handled by the
 *          calling function.
 * @param stream The output stream handle to check.
 * @return `true` if the stream is currently in the flushing list, `false`
 * otherwise.
 */
static bool
is_in_flushing_list(wasi_output_stream_t stream)
{
    FlushingStreamNode *curr = flushing_streams_list;
    while (curr) {
        if (curr->stream == stream) {
            return true;
        }
        curr = curr->next;
    }
    return false;
}

/**
 * @brief Removes an output stream from the global list of flushing streams.
 * @details This is an internal helper function, likely called after a `flush`
 *          operation has completed for a stream. It uses a pointer-to-pointer
 *          traversal to efficiently remove the node from the linked list.
 *          Thread safety is handled by the calling function.
 * @param stream The output stream handle to remove from the list.
 */
static void
remove_from_flushing_list(wasi_output_stream_t stream)
{
    // Use a pointer-to-pointer to traverse the list. This allows modifying
    // the `next` pointer of the previous node (or the list head) directly.
    FlushingStreamNode **p = &flushing_streams_list;
    while (*p) {
        FlushingStreamNode *entry = *p;
        if (entry->stream == stream) {
            // Found the node. Bypass it by pointing the previous `next`
            // pointer to the current node's `next`.
            *p = entry->next;
            wasm_runtime_free(entry);
            break;
        }
        // Advance to the address of the next pointer in the list.
        p = &(*p)->next;
    }
}

// wasi:io/poll

static short
pollable_get_events(const wasi_pollable_context_t *pollable)
{
    switch (pollable->type) {
        case WASI_POLLABLE_IN:
            return POLLIN;
        case WASI_POLLABLE_OUT:
            return POLLOUT;
        case WASI_POLLABLE_SOCK:
            return POLLIN | POLLPRI | POLLOUT;
        default:
            return 0;
    }
}

void
wasi_pollable_block(wasi_pollable_context_t *pollable)
{
    struct pollfd pfd;
    pfd.fd = pollable->fd;
    pfd.events = pollable_get_events(pollable);
    poll(&pfd, 1, -1);
}

/**
 * @brief Check if a pollable resource is ready.
 * @details Implements the non-blocking check for a `pollable` resource from
 *          the `wasi:io/poll` interface. This function returns immediately
 *          with the current readiness state of the pollable.
 * @param pollable The pollable handle to check. In this POSIX implementation,
 *                 this is expected to be a file descriptor.
 * @return `true` if the pollable is ready, `false` otherwise.
 */
bool
wasi_pollable_ready(wasi_pollable_context_t *pollable)
{
    struct pollfd pfd;
    pfd.fd = pollable->fd;
    pfd.events = pollable_get_events(pollable);

    // Use the underlying POSIX poll function with a timeout of 0.
    // This makes the call non-blocking, returning immediately with the
    // current status of the descriptor.
    int ret = poll(&pfd, 1, 0);

    // `poll` returns the number of descriptors that are ready.
    // We return true only if our single descriptor is ready.
    return ret == 1;
}

/**
 * @brief Wait for a set of pollable resources to become ready.
 * @details Implements the `poll` function from the `wasi:io/poll` interface.
 *          This function takes a list of pollables and blocks until at least
 *          one of them is ready. It is similar to the `poll` system call in
 * POSIX.
 *
 * @note The caller is responsible for freeing the returned list (`ret->buf`).
 *
 * @param pollables An array of pointers to pollable context to wait on.
 * @param n_pollables The number of pollable contexts in the array.
 * @param[out] ret A pointer to a list struct that will be populated with the
 *                 indices of the pollables that have become ready.
 */
void

wasi_poll(const wasi_pollable_context_t **pollables, uint32_t n_pollables,
          wasi_list_u32_t *ret)
{
    if (!pollables || !ret) {
        return;
    }
    ret->buf = NULL;
    ret->len = 0;

    // Allocate a POSIX pollfd array to match the input pollables.
    struct pollfd *pfds =
        wasm_runtime_malloc(sizeof(struct pollfd) * n_pollables);
    if (!pfds) {
        return;
    }

    // Initialize the pollfd array for the poll system call.
    for (uint32_t i = 0; i < n_pollables; i++) {
        pfds[i].fd = pollables[i]->fd;
        pfds[i].events = pollable_get_events(pollables[i]);
    }

    // Block indefinitely (-1 timeout) until at least one descriptor is ready.
    int n = poll(pfds, n_pollables, -1);
    if (n <= 0) {
        // Handle error or unexpected timeout.
        wasm_runtime_free(pfds);
        return;
    }

    // Allocate the return buffer to hold the indices of the ready pollables.
    ret->buf = wasm_runtime_malloc(sizeof(uint32_t) * n);
    if (!ret->buf) {
        wasm_runtime_free(pfds);
        return;
    }

    // Iterate through the results and populate the return list with the
    // indices of the pollables that have the POLLIN event set.
    uint32_t j = 0;
    for (uint32_t i = 0; i < n_pollables; i++) {
        if (pfds[i].revents) {
            ret->buf[j++] = i;
        }
    }
    ret->len = j;

    wasm_runtime_free(pfds);
}

// wasi:io/streams

/**
 * @brief Perform a non-blocking read from an input stream.
 * @details Implements the `read` method on the `input-stream` resource from the
 *          `wasi:io/streams` interface. It attempts to read up to `len` bytes,
 *          but may return fewer if not all data is immediately available.
 *
 * @note The caller is responsible for freeing the returned list
 * (`ret->u.ok.buf`).
 *
 * @param stream The input stream handle (file descriptor) to read from.
 * @param len The maximum number of bytes to read.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 list of bytes read. On failure, it contains a stream error.
 */
void
wasi_input_stream_read(wasi_input_stream_t stream, uint64_t len,
                       wasi_result_list_u8_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    uint8_t *buf = wasm_runtime_malloc(len);
    if (!buf) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = ENOMEM;
        return;
    }

    ssize_t s = read(stream, buf, len);
    if (s < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ret->is_err = false;
            ret->u.ok.buf = NULL;
            ret->u.ok.buf_len = 0;
            return;
        }
        else {
            wasm_runtime_free(buf);
            ret->is_err = true;
            ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
            ret->u.err.payload.error = errno;
            return;
        }
    }
    if (s == 0) {
        wasm_runtime_free(buf);
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_CLOSED;
        return;
    }

    if (len == 0) {
        ret->is_err = false;
        ret->u.ok.buf = NULL;
        ret->u.ok.buf_len = 0;
        return;
    }

    if ((uint64_t)s < len) {
        if (s > 0) {
            uint8_t *new_buf = wasm_runtime_realloc(buf, s);
            if (!new_buf) {
                wasm_runtime_free(buf);
                ret->is_err = true;
                ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
                ret->u.err.payload.error = ENOMEM;
                return;
            }
            buf = new_buf;
        }
        else {
            wasm_runtime_free(buf);
            buf = NULL;
        }
    }

    ret->is_err = false;
    ret->u.ok.buf = buf;
    ret->u.ok.buf_len = s;
}

/**
 * @brief Perform a blocking read from an input stream.
 * @details Implements the `blocking-read` method on the `input-stream` resource
 *          from the `wasi:io/streams` interface. It blocks until data is
 *          available and then performs a read.
 *
 * @note The caller is responsible for freeing the returned list
 * (`ret->u.ok.buf`).
 *
 * @param stream The input stream handle (file descriptor) to read from.
 * @param len The maximum number of bytes to read.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 list of bytes read. On failure, it contains a stream error.
 */
void
wasi_input_stream_blocking_read(wasi_input_stream_t stream, uint64_t len,
                                wasi_result_list_u8_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    struct pollfd pfd;
    pfd.fd = stream;
    pfd.events = POLLIN;
    int n = 0;

    // Block until the stream is ready for reading.
    // The loop handles cases where the poll system call is interrupted by a
    // signal.
    do {
        n = poll(&pfd, 1, -1);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    if (n == 0) {
        // This should not happen with a timeout of -1, but as a safeguard,
        // return an empty list to indicate no data was read.
        ret->is_err = false;
        ret->u.ok.buf = NULL;
        ret->u.ok.buf_len = 0;
        return;
    }

    // Now that we know there's data available, we can delegate to the
    // non-blocking read function to perform the actual read operation.
    wasi_input_stream_read(stream, len, ret);
}

/**
 * @brief Skip bytes from an input stream.
 * @details Implements the `skip` method on the `input-stream` resource from the
 *          `wasi:io/streams` interface. It attempts to efficiently skip bytes
 *          using `lseek` for seekable streams, and falls back to reading and
 *          discarding data for non-seekable streams.
 * @param stream The input stream handle (file descriptor).
 * @param len The maximum number of bytes to skip.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 number of bytes actually skipped. On failure, it contains a
 *                 stream error.
 */
void
wasi_input_stream_skip(wasi_input_stream_t stream, uint64_t len,
                       wasi_result_u64_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    // Try to use lseek for seekable streams
    off_t current_pos = lseek(stream, 0, SEEK_CUR);
    if (current_pos != -1) {
        off_t new_pos = lseek(stream, len, SEEK_CUR);
        if (new_pos != -1) {
            ret->is_err = false;
            ret->u.ok = new_pos - current_pos;
            return;
        }
        // If lseek fails, it might be a non-seekable stream (e.g., a pipe).
        // Reset errno and fall through to the read-based approach.
        errno = 0;
    }

    // Fallback for non-seekable streams: read and discard
    char buf[4096];
    uint64_t skipped_total = 0;
    while (skipped_total < len) {
        uint64_t to_read = len - skipped_total;
        if (to_read > sizeof(buf)) {
            to_read = sizeof(buf);
        }

        ssize_t bytes_read = read(stream, buf, to_read);

        if (bytes_read < 0) {
            // Retry if the read was interrupted by a signal.
            if (errno == EINTR) {
                continue;
            }
            ret->is_err = true;
            ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
            ret->u.err.payload.error = errno;
            return;
        }

        if (bytes_read == 0) {
            // End of stream
            break;
        }

        skipped_total += bytes_read;
    }

    if (!skipped_total && len) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_CLOSED;
        return;
    }

    ret->is_err = false;
    ret->u.ok = skipped_total;
}

/**
 * @brief Skip bytes from a stream, blocking until data is available.
 * @details Implements the `blocking-skip` method on the `input-stream` resource
 *          from the `wasi:io/streams` interface. It blocks until the stream is
 *          ready and then performs a skip operation.
 * @param stream The input stream handle (file descriptor).
 * @param len The maximum number of bytes to skip.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 number of bytes actually skipped. On failure, it contains a
 *                 stream error.
 */
void
wasi_input_stream_blocking_skip(wasi_input_stream_t stream, uint64_t len,
                                wasi_result_u64_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    struct pollfd pfd;
    pfd.fd = stream;
    pfd.events = POLLIN;
    int n = 0;

    // Block until the stream is ready for reading.
    // The loop handles cases where the poll system call is interrupted by a
    // signal.
    do {
        n = poll(&pfd, 1, -1);
    } while (n < 0 && errno == EINTR);

    if (n < 0) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    if (n == 0) {
        // This should not happen with a timeout of -1, but as a safeguard,
        // return 0 to indicate no bytes were skipped.
        ret->is_err = false;
        ret->u.ok = 0;
        return;
    }

    // Now that the stream is ready, delegate to the non-blocking skip function
    // to perform the actual skip operation.
    wasi_input_stream_skip(stream, len, ret);
}

/**
 * @brief Check readiness for writing to an output stream.
 * @details Implements the `check-write` method on the `output-stream` resource
 *          from the `wasi:io/streams` interface. This function is non-blocking
 *          and returns the number of bytes that can be written without
 * blocking.
 * @param stream The output stream handle (file descriptor).
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 number of bytes permitted for the next write. On failure, it
 *                 contains a stream error.
 */
void
wasi_output_stream_check_write(wasi_output_stream_t stream,
                               wasi_result_u64_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    // Lock the mutex to safely check and update the flushing streams list.
    pthread_mutex_lock(&flushing_streams_list_lock);

    // If the stream is currently flushing, we must check if it's done.
    if (is_in_flushing_list(stream)) {
        int queue_size = 0;
        // Use ioctl with TIOCOUTQ to get the number of bytes in the output
        // buffer.
        if (ioctl(stream, TIOCOUTQ, &queue_size) < 0) {
            pthread_mutex_unlock(&flushing_streams_list_lock);
            ret->is_err = true;
            ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
            ret->u.err.payload.error = errno;
            return;
        }

        if (queue_size == 0) {
            // If the output queue is empty, the flush is complete.
            remove_from_flushing_list(stream);
        }
        else {
            // If the queue is not empty, the flush is still in progress.
            // According to the spec, we must return a permit of 0 bytes.
            pthread_mutex_unlock(&flushing_streams_list_lock);
            ret->is_err = false;
            ret->u.ok = 0;
            return;
        }
    }

    pthread_mutex_unlock(&flushing_streams_list_lock);

    // Determine the type of the descriptor to provide an accurate buffer size.
    struct stat statbuf;
    if (fstat(stream, &statbuf) < 0) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    if (S_ISSOCK(statbuf.st_mode)) {
        int available_space = 0;
        socklen_t len = sizeof(available_space);
        if (getsockopt(stream, SOL_SOCKET, SO_SNDBUF, &available_space, &len)
            < 0) {
            ret->is_err = true;
            ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
            ret->u.err.payload.error = errno;
            return;
        }

        int used_space = 0;
        if (ioctl(stream, TIOCOUTQ, &used_space) == 0) {
            available_space -= used_space;
        }

        ret->is_err = false;
        ret->u.ok = available_space > 0 ? available_space : 0;
    }
    else if (S_ISFIFO(statbuf.st_mode)) {
        // For pipes, we can't easily get the available buffer space.
        // Return a reasonable chunk size as a permit.
        ret->is_err = false;
        ret->u.ok = 4096;
    }
    else {
        // For regular files, writes are usually buffered by the OS and rarely
        // block. We can permit a large write.
        ret->is_err = false;
        ret->u.ok = 65536; // 64k, a reasonable buffer size
    }
}

/**
 * @brief Perform a non-blocking write to an output stream.
 * @details Implements the `write` method on the `output-stream` resource from
 *          the `wasi:io/streams` interface. It attempts to write the entire
 *          payload in a single operation.
 * @note Per the WIT specification, this function should only be called after
 *       `check-write` has granted a permit of sufficient size. A partial write
 *       is treated as an error.
 * @param stream The output stream handle (file descriptor).
 * @param payload A list containing the data to be written.
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_write(wasi_output_stream_t stream,
                         const wasi_list_u8_t *payload,
                         wasi_result_void_stream_error_t *ret)
{
    if (!payload || !ret) {
        return;
    }

    const uint8_t *buf = payload->buf;
    size_t len = payload->buf_len;

    // Perform the underlying POSIX write operation.
    ssize_t s = write(stream, buf, len);

    if (s < 0) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    if ((size_t)s < len) {
        // A partial write is considered an error, as `check-write` should
        // have guaranteed enough space for the entire write to succeed.
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = EACCES;
    }
    else {
        // The full payload was written successfully.
        ret->is_err = false;
    }
}

/**
 * @brief Perform a blocking write of a list of bytes and then a blocking flush.
 * @details Implements the `blocking-write-and-flush` method on the
 * `output-stream` resource from the `wasi:io/streams` interface. This function
 * writes the entire payload, blocking as necessary, and then blocks until the
 *          stream is fully flushed.
 * @param stream The output stream handle (file descriptor).
 * @param payload A list containing the data to be written.
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_blocking_write_and_flush(
    wasi_output_stream_t stream, const wasi_list_u8_t *payload,
    wasi_result_void_stream_error_t *ret)
{
    if (!payload || !ret) {
        return;
    }

    uint64_t remaining = payload->buf_len;
    uint8_t *buf = payload->buf;
    wasi_pollable_context_t pollable = { stream, false, WASI_POLLABLE_OUT };

    // Loop until the entire payload has been written.
    while (remaining > 0) {
        wasi_pollable_block(&pollable);
        wasi_result_u64_stream_error_t check_res;
        wasi_output_stream_check_write(stream, &check_res);
        if (check_res.is_err) {
            ret->is_err = true;
            ret->u.err = check_res.u.err;
            return;
        }

        uint64_t n = check_res.u.ok;
        uint64_t len = remaining < n ? remaining : n;
        if (len == 0) {
            // If the permit is 0, loop again to block until a non-zero
            // permit is available.
            continue;
        }

        wasi_list_u8_t chunk = { .buf = buf, .buf_len = len };
        wasi_result_void_stream_error_t write_res;
        wasi_output_stream_write(stream, &chunk, &write_res);
        if (write_res.is_err) {
            ret->is_err = true;
            ret->u.err = write_res.u.err;
            return;
        }

        // Advance the buffer pointer and update the remaining byte count.
        buf += len;
        remaining -= len;
    }

    // After all bytes have been written, perform a blocking flush to ensure
    // all data is sent to its final destination.
    wasi_output_stream_blocking_flush(stream, ret);
}

/**
 * @brief Request to flush buffered output for a stream.
 * @details Implements the `flush` method on the `output-stream` resource from
 *          the `wasi:io/streams` interface. This function is non-blocking.
 *          It initiates a flush and returns immediately.
 * @note For regular files, this function is a no-op due to the blocking nature
 *       of `fsync`. For sockets, it initiates a flush by marking the stream,
 *       which will cause subsequent `check-write` calls to return 0 until the
 *       kernel's output buffer is empty.
 * @param stream The output stream handle (file descriptor).
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_flush(wasi_output_stream_t stream,
                         wasi_result_void_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    struct stat statbuf;
    if (fstat(stream, &statbuf) < 0) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    if (S_ISREG(statbuf.st_mode) || S_ISFIFO(statbuf.st_mode)) {
        // For regular files and pipes, flush is a no-op because writes are
        // generally buffered by the OS or immediately available. The
        // blocking-flush function is responsible for ensuring data is
        // physically written to disk (for files).
        ret->is_err = false;
        return;
    }

    // Lock the mutex to safely check and update the flushing streams list.
    pthread_mutex_lock(&flushing_streams_list_lock);

    int queue_size = 0;
    // Use ioctl with TIOCOUTQ to get the number of bytes in the kernel's output
    // buffer.
    if (ioctl(stream, TIOCOUTQ, &queue_size) < 0) {
        pthread_mutex_unlock(&flushing_streams_list_lock);
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
        ret->u.err.payload.error = errno;
        return;
    }

    // If there is data in the output queue, mark the stream as flushing.
    if (queue_size > 0) {
        if (!is_in_flushing_list(stream)) {
            add_to_flushing_list(stream);
        }
    }

    pthread_mutex_unlock(&flushing_streams_list_lock);

    ret->is_err = false;
}

/**
 * @brief Block until all buffered output has been flushed to its destination.
 * @details Implements the `blocking-flush` method on the `output-stream`
 *          resource from the `wasi:io/streams` interface. This function
 *          guarantees that all data written prior to this call is durably
 *          stored.
 * @param stream The output stream handle (file descriptor).
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_blocking_flush(wasi_output_stream_t stream,
                                  wasi_result_void_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    // For regular files, fsync is the correct blocking flush operation.
    struct stat statbuf;
    if (fstat(stream, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        if (fsync(stream) != 0) {
            // fsync can fail with EINVAL or EROFS on file systems that
            // don't support it for the given descriptor. In these cases,
            // we should not report an error.
            if (errno != EINVAL && errno != EROFS) {
                ret->is_err = true;
                ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
                ret->u.err.payload.error = errno;
                return;
            }
        }
        ret->is_err = false;
        return;
    }

    // For other stream types (sockets, pipes), use the polling mechanism.
    // 1. Initiate the flush.
    wasi_result_void_stream_error_t flush_res;
    wasi_output_stream_flush(stream, &flush_res);
    if (flush_res.is_err) {
        *ret = flush_res;
        return;
    }

    // 2. Poll until the stream is ready for writing again.
    wasi_pollable_context_t pollable = { stream, false, WASI_POLLABLE_OUT };
    while (true) {
        wasi_pollable_block(&pollable);

        wasi_result_u64_stream_error_t check_res;
        wasi_output_stream_check_write(stream, &check_res);

        if (check_res.is_err) {
            ret->is_err = true;
            ret->u.err = check_res.u.err;
            return;
        }

        // The flush is complete when check-write grants a permit > 0,
        // which means the stream is no longer considered to be flushing.
        if (check_res.u.ok > 0) {
            break;
        }
    }

    ret->is_err = false;
}

/**
 * @brief Write a sequence of zero bytes to an output stream.
 * @details Implements the `write-zeroes` method on the `output-stream` resource
 *          from the `wasi:io/streams` interface. It includes an optimization
 *          for regular files using `ftruncate` and falls back to manual
 *          writing for other stream types (e.g., sockets, pipes).
 * @param stream The output stream handle (file descriptor).
 * @param len The number of zero bytes to write.
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_write_zeroes(wasi_output_stream_t stream, uint64_t len,
                                wasi_result_void_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    struct stat statbuf;
    if (fstat(stream, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        off_t current_pos = lseek(stream, 0, SEEK_CUR);
        if (current_pos != -1) {
            // ftruncate is often a highly efficient way to extend a file with
            // zeroes.
            if (ftruncate(stream, current_pos + len) == 0) {
                lseek(stream, len, SEEK_CUR); // Advance the file offset.
                ret->is_err = false;
                return;
            }
            // If ftruncate fails, we ignore the error and fall through to the
            // manual write method below.
        }
    }

    char buf[4096] = { 0 };
    uint64_t remaining = len;

    while (remaining > 0) {
        uint64_t write_len = remaining < sizeof(buf) ? remaining : sizeof(buf);
        wasi_list_u8_t payload = { .buf = (uint8_t *)buf,
                                   .buf_len = write_len };
        wasi_result_void_stream_error_t write_res;

        // Reuse the existing write function to write a chunk of zeroes.
        wasi_output_stream_write(stream, &payload, &write_res);
        if (write_res.is_err) {
            ret->is_err = true;
            ret->u.err = write_res.u.err;
            return;
        }
        remaining -= write_len;
    }

    ret->is_err = false;
}

/**
 * @brief Perform a blocking write of a sequence of zero bytes, then a blocking
 * flush.
 * @details Implements the `blocking-write-zeroes-and-flush` method on the
 *          `output-stream` resource from `wasi:io/streams`. This function
 *          writes the specified number of zero bytes, blocking as necessary,
 *          and then blocks until the stream is fully flushed.
 * @param stream The output stream handle (file descriptor).
 * @param len The number of zero bytes to write.
 * @param[out] ret A pointer to the result struct. On success, `is_err` is
 * false. On failure, it contains a stream error.
 */
void
wasi_output_stream_blocking_write_zeroes_and_flush(
    wasi_output_stream_t stream, uint64_t len,
    wasi_result_void_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

    uint64_t remaining = len;

    // Loop until the specified number of zero bytes has been written.
    wasi_pollable_context_t pollable = { stream, false, WASI_POLLABLE_OUT };
    while (remaining > 0) {
        wasi_pollable_block(&pollable);
        wasi_result_u64_stream_error_t check_res;
        wasi_output_stream_check_write(stream, &check_res);
        if (check_res.is_err) {
            ret->is_err = true;
            ret->u.err = check_res.u.err;
            return;
        }

        uint64_t n = check_res.u.ok;
        uint64_t write_len = remaining < n ? remaining : n;
        if (write_len == 0) {
            // If the permit is 0, loop again to block until a non-zero
            // permit is available.
            continue;
        }

        // Perform the non-blocking write of zeroes for the permitted chunk
        // size. This reuses the optimized `write_zeroes` implementation.
        wasi_result_void_stream_error_t write_res;
        wasi_output_stream_write_zeroes(stream, write_len, &write_res);
        if (write_res.is_err) {
            ret->is_err = true;
            ret->u.err = write_res.u.err;
            return;
        }
        remaining -= write_len;
    }

    // After all bytes have been written, perform a blocking flush.
    wasi_output_stream_blocking_flush(stream, ret);
}

/**
 * @brief Splice data from an input stream to an output stream.
 * @details Implements the `splice` method on the `output-stream` resource from
 *          the `wasi:io/streams` interface. This function moves data from a
 *          source to a destination without copying it into the WebAssembly
 * memory. It includes a zero-copy optimization for Linux using `splice(2)`.
 * @param stream The destination output stream handle (file descriptor).
 * @param src The source input stream handle (file descriptor).
 * @param len The maximum number of bytes to splice.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 number of bytes actually spliced. On failure, it contains a
 *                 stream error.
 */
void
wasi_output_stream_splice(wasi_output_stream_t stream, wasi_input_stream_t src,
                          uint64_t len, wasi_result_u64_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

#if defined(__linux__)
    // --- Optimization for Linux using zero-copy splice(2) ---
    struct stat stat_in, stat_out;
    if (fstat(src, &stat_in) == 0 && fstat(stream, &stat_out) == 0) {
        bool in_is_pipe = S_ISFIFO(stat_in.st_mode);
        bool out_is_pipe = S_ISFIFO(stat_out.st_mode);

        // splice(2) is most effective with pipes.
        if (in_is_pipe || out_is_pipe) {
            ssize_t s = splice(src, NULL, stream, NULL, len, SPLICE_F_NONBLOCK);
            if (s >= 0) {
                ret->is_err = false;
                ret->u.ok = s;
                return;
            }
            // EINVAL can mean splice is not supported for these descriptor
            // types. In that case, we fall through to the generic read/write
            // implementation.
            if (errno != EINVAL) {
                ret->is_err = true;
                ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
                ret->u.err.payload.error = errno;
                return;
            }
        }
    }
#endif

    // --- Fallback implementation for non-Linux or non-pipe streams ---
    // This follows the logic described in the WIT specification:
    // check-write, then read, then write.

    // 1. Check how much data the destination stream can accept.
    wasi_result_u64_stream_error_t check_res;
    wasi_output_stream_check_write(stream, &check_res);
    if (check_res.is_err) {
        *ret = check_res;
        return;
    }

    uint64_t n = check_res.u.ok;
    // Determine the amount to read: the minimum of what's requested, and
    // what the destination can accept.
    uint64_t read_len = len < n ? len : n;
    if (read_len == 0) {
        ret->is_err = false;
        ret->u.ok = 0;
        return;
    }

    // 2. Read that amount from the source stream.
    wasi_result_list_u8_stream_error_t read_res;
    wasi_input_stream_read(src, read_len, &read_res);
    if (read_res.is_err) {
        ret->is_err = true;
        ret->u.err = read_res.u.err;
        return;
    }

    // 3. Write the data to the destination stream.
    if (read_res.u.ok.buf_len > 0) {
        wasi_result_void_stream_error_t write_res;
        wasi_output_stream_write(stream, &read_res.u.ok, &write_res);
        if (write_res.is_err) {
            wasm_runtime_free(read_res.u.ok.buf);
            ret->is_err = true;
            ret->u.err = write_res.u.err;
            return;
        }
        ret->is_err = false;
        ret->u.ok = read_res.u.ok.buf_len;
    }
    else {
        // Nothing was read, so nothing was spliced.
        ret->is_err = false;
        ret->u.ok = 0;
    }

    // Clean up the buffer allocated by wasi_input_stream_read.
    if (read_res.u.ok.buf) {
        wasm_runtime_free(read_res.u.ok.buf);
    }
}

/**
 * @brief Splice data from an input stream to an output stream, blocking until
 *        the requested number of bytes have been moved.
 * @details Implements the `blocking-splice` method on the `output-stream`
 * resource from the `wasi:io/streams` interface. This function moves data until
 *          `len` bytes have been spliced or the source stream ends. It includes
 *          a zero-copy optimization for Linux using a blocking `splice(2)`
 * loop.
 * @param stream The destination output stream handle (file descriptor).
 * @param src The source input stream handle (file descriptor).
 * @param len The number of bytes to splice.
 * @param[out] ret A pointer to the result struct. On success, it contains the
 *                 number of bytes actually spliced. On failure, it contains a
 *                 stream error.
 */
void
wasi_output_stream_blocking_splice(wasi_output_stream_t stream,
                                   wasi_input_stream_t src, uint64_t len,
                                   wasi_result_u64_stream_error_t *ret)
{
    if (!ret) {
        return;
    }

#if defined(__linux__)
    // --- Optimization for Linux using blocking zero-copy splice(2) ---
    struct stat stat_in, stat_out;
    if (fstat(src, &stat_in) == 0 && fstat(stream, &stat_out) == 0) {
        bool in_is_pipe = S_ISFIFO(stat_in.st_mode);
        bool out_is_pipe = S_ISFIFO(stat_out.st_mode);

        if (in_is_pipe || out_is_pipe) {
            uint64_t total_spliced = 0;
            while (total_spliced < len) {
                // Call splice with a flag of 0 to make it a blocking operation.
                ssize_t s =
                    splice(src, NULL, stream, NULL, len - total_spliced, 0);
                if (s > 0) {
                    total_spliced += s;
                }
                else if (s == 0) {
                    // EOF
                    break;
                }
                else {
                    // Handle errors, retrying on EINTR.
                    if (errno == EINTR) {
                        continue;
                    }
                    ret->is_err = true;
                    ret->u.err.kind =
                        WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
                    ret->u.err.payload.error = errno;
                    return;
                }
            }
            ret->is_err = false;
            ret->u.ok = total_spliced;
            return;
        }
    }
#endif

    // --- Fallback implementation using a blocking read/write loop ---
    uint64_t total_spliced = 0;
    char buf[4096];

    while (total_spliced < len) {
        uint64_t to_read = len - total_spliced;
        if (to_read > sizeof(buf)) {
            to_read = sizeof(buf);
        }

        // Perform a blocking read from the source.
        ssize_t bytes_read = read(src, buf, to_read);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            ret->is_err = true;
            ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
            ret->u.err.payload.error = errno;
            return;
        }

        if (bytes_read == 0) {
            // End of source stream
            break;
        }

        // Perform a blocking write to the destination, handling partial writes.
        uint64_t total_written = 0;
        while (total_written < (uint64_t)bytes_read) {
            ssize_t bytes_written =
                write(stream, buf + total_written, bytes_read - total_written);

            if (bytes_written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                ret->is_err = true;
                ret->u.err.kind = WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED;
                ret->u.err.payload.error = errno;
                return;
            }
            total_written += bytes_written;
        }

        total_spliced += total_written;
    }

    if (!total_spliced && len) {
        ret->is_err = true;
        ret->u.err.kind = WASI_STREAM_ERROR_KIND_CLOSED;
        return;
    }

    ret->is_err = false;
    ret->u.ok = total_spliced;
}

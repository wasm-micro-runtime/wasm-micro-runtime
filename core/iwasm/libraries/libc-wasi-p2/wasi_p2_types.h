/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_TYPES_H
#define WASI_P2_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#include "platform_wasi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef __wasi_filesize_t wasi_filesize_t;
typedef __wasi_linkcount_t wasi_link_count_t;

typedef struct wasi_datetime_t {
    uint64_t seconds;
    uint32_t nanoseconds;
} wasi_datetime_t;

typedef uint64_t wasi_instant_t;
typedef uint64_t wasi_duration_t;

typedef enum wasi_pollable_type {
    WASI_POLLABLE_IN,
    WASI_POLLABLE_OUT,
    WASI_POLLABLE_SOCK
} wasi_pollable_type_t;

typedef struct wasi_pollable_context {
    int fd;
    bool own_fd;
    wasi_pollable_type_t type;
} wasi_pollable_context_t;

#define SET_POLLABLE_CTX(pollable, f, own, t) \
    do {                                      \
        (pollable)->fd = f;                   \
        (pollable)->own_fd = own;             \
        (pollable)->type = t;                 \
    } while (0)
#define SET_INPUT_POLLABLE(pollable, fd, own) \
    SET_POLLABLE_CTX(pollable, fd, own, WASI_POLLABLE_IN)
#define SET_OUTPUT_POLLABLE(pollable, fd, own) \
    SET_POLLABLE_CTX(pollable, fd, own, WASI_POLLABLE_OUT)

typedef int32_t wasi_error_t;
typedef int32_t wasi_input_stream_t;
typedef int32_t wasi_output_stream_t;

typedef struct wasi_list_u8_t {
    uint8_t *buf;
    uint64_t buf_len;
} wasi_list_u8_t;

typedef struct wasi_list_u32_t {
    uint32_t *buf;
    uint32_t len;
} wasi_list_u32_t;

#define WASI_ERROR_CODE_SUCCESS 0
#define WASI_UDP_SEND_SUCCESS -1

typedef enum wasi_network_error_code_t {
    WASI_NETWORK_ERROR_CODE_UNKNOWN,
    WASI_NETWORK_ERROR_CODE_ACCESS_DENIED,
    WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED,
    WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT,
    WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY,
    WASI_NETWORK_ERROR_CODE_TIMEOUT,
    WASI_NETWORK_ERROR_CODE_CONCURRENCY_CONFLICT,
    WASI_NETWORK_ERROR_CODE_NOT_IN_PROGRESS,
    WASI_NETWORK_ERROR_CODE_WOULD_BLOCK,
    WASI_NETWORK_ERROR_CODE_INVALID_STATE,
    WASI_NETWORK_ERROR_CODE_NEW_SOCKET_LIMIT,
    WASI_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE,
    WASI_NETWORK_ERROR_CODE_ADDRESS_IN_USE,
    WASI_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE,
    WASI_NETWORK_ERROR_CODE_CONNECTION_REFUSED,
    WASI_NETWORK_ERROR_CODE_CONNECTION_RESET,
    WASI_NETWORK_ERROR_CODE_CONNECTION_ABORTED,
    WASI_NETWORK_ERROR_CODE_DATAGRAM_TOO_LARGE,
    WASI_NETWORK_ERROR_CODE_NAME_UNRESOLVABLE,
    WASI_NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE,
    WASI_NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE
} wasi_network_error_code_t;

typedef enum wasi_stream_error_kind_t {
    WASI_STREAM_ERROR_KIND_LAST_OPERATION_FAILED,
    WASI_STREAM_ERROR_KIND_CLOSED,
} wasi_stream_error_kind_t;

typedef struct wasi_stream_error_t {
    wasi_stream_error_kind_t kind;
    union {
        wasi_error_t error;
    } payload;
} wasi_stream_error_t;

typedef struct wasi_result_list_u8_stream_error_t {
    bool is_err;
    union {
        wasi_list_u8_t ok;
        wasi_stream_error_t err;
    } u;
} wasi_result_list_u8_stream_error_t;

typedef struct wasi_result_u64_stream_error_t {
    bool is_err;
    union {
        uint64_t ok;
        wasi_stream_error_t err;
    } u;
} wasi_result_u64_stream_error_t;

typedef struct wasi_result_void_stream_error_t {
    bool is_err;
    union {
        wasi_stream_error_t err;
    } u;
} wasi_result_void_stream_error_t;

typedef __wasi_addr_ip_t wasi_ip_address_t;

typedef enum wasi_filesystem_error_code_t {
    WASI_FILESYSTEM_CODE_ACCESS,
    WASI_FILESYSTEM_CODE_WOULD_BLOCK,
    WASI_FILESYSTEM_CODE_ALREADY,
    WASI_FILESYSTEM_CODE_BAD_DESCRIPTOR,
    WASI_FILESYSTEM_CODE_BUSY,
    WASI_FILESYSTEM_CODE_DEADLOCK,
    WASI_FILESYSTEM_CODE_QUOTA,
    WASI_FILESYSTEM_CODE_EXISTS,
    WASI_FILESYSTEM_CODE_FILE_TOO_LARGE,
    WASI_FILESYSTEM_CODE_ILLEGAL_BYTE_SEQUENCE,
    WASI_FILESYSTEM_CODE_IN_PROGRESS,
    WASI_FILESYSTEM_CODE_INTERRUPTED,
    WASI_FILESYSTEM_CODE_INVALID,
    WASI_FILESYSTEM_CODE_IO,
    WASI_FILESYSTEM_CODE_IS_DIRECTORY,
    WASI_FILESYSTEM_CODE_LOOP,
    WASI_FILESYSTEM_CODE_TOO_MANY_LINKS,
    WASI_FILESYSTEM_CODE_MESSAGE_SIZE,
    WASI_FILESYSTEM_CODE_NAME_TOO_LONG,
    WASI_FILESYSTEM_CODE_NO_DEVICE,
    WASI_FILESYSTEM_CODE_NO_ENTRY,
    WASI_FILESYSTEM_CODE_NO_LOCK,
    WASI_FILESYSTEM_CODE_INSUFFICIENT_MEMORY,
    WASI_FILESYSTEM_CODE_INSUFFICIENT_SPACE,
    WASI_FILESYSTEM_CODE_NOT_DIRECTORY,
    WASI_FILESYSTEM_CODE_NOT_EMPTY,
    WASI_FILESYSTEM_CODE_NOT_RECOVERABLE,
    WASI_FILESYSTEM_CODE_UNSUPPORTED,
    WASI_FILESYSTEM_CODE_NO_TTY,
    WASI_FILESYSTEM_CODE_NO_SUCH_DEVICE,
    WASI_FILESYSTEM_CODE_OVERFLOW,
    WASI_FILESYSTEM_CODE_NOT_PERMITTED,
    WASI_FILESYSTEM_CODE_PIPE,
    WASI_FILESYSTEM_CODE_READ_ONLY,
    WASI_FILESYSTEM_CODE_INVALID_SEEK,
    WASI_FILESYSTEM_CODE_TEXT_FILE_BUSY,
    WASI_FILESYSTEM_CODE_CROSS_DEVICE
} wasi_filesystem_error_code_t;

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_TYPES_H */

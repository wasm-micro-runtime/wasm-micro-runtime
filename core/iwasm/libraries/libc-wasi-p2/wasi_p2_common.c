/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasi_p2_common.h"
#include "wasi_p2_types.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>

wasi_network_error_code_t
errno_to_wasi_network(int err)
{
    static const struct {
        int err;
        wasi_network_error_code_t wasi_err;
    } error_map[] = {
        { EACCES, WASI_NETWORK_ERROR_CODE_ACCESS_DENIED },
        { EPERM, WASI_NETWORK_ERROR_CODE_ACCESS_DENIED },

        { EOPNOTSUPP, WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED },
        { ENOTSUP, WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED },

        { EINVAL, WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT },

        { ENOMEM, WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY },
        { ENOBUFS, WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY },
        { EAI_MEMORY, WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY },
        { ENOSPC, WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY },

        { ETIMEDOUT, WASI_NETWORK_ERROR_CODE_TIMEOUT }, // Not explicit in wit

        { EALREADY, WASI_NETWORK_ERROR_CODE_CONCURRENCY_CONFLICT },

        { ESRCH, WASI_NETWORK_ERROR_CODE_NOT_IN_PROGRESS }, // Not explicit in
                                                            // wit, maybe EINVAL

        { EAGAIN, WASI_NETWORK_ERROR_CODE_WOULD_BLOCK },
        { EINPROGRESS, WASI_NETWORK_ERROR_CODE_WOULD_BLOCK },
#if EWOULDBLOCK != EAGAIN
        { EWOULDBLOCK, WASI_NETWORK_ERROR_CODE_WOULD_BLOCK },
#endif

        { ENOTCONN, WASI_NETWORK_ERROR_CODE_INVALID_STATE },

        { EMFILE, WASI_NETWORK_ERROR_CODE_NEW_SOCKET_LIMIT },
        { ENFILE, WASI_NETWORK_ERROR_CODE_NEW_SOCKET_LIMIT },

        { EADDRNOTAVAIL, WASI_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE },

        { EADDRINUSE, WASI_NETWORK_ERROR_CODE_ADDRESS_IN_USE },

        { ENETUNREACH, WASI_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE },
        { ENETDOWN, WASI_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE },

        { ECONNREFUSED, WASI_NETWORK_ERROR_CODE_CONNECTION_REFUSED },

        { ECONNRESET, WASI_NETWORK_ERROR_CODE_CONNECTION_RESET },
#ifdef ENETRESET
        { ENETRESET, WASI_NETWORK_ERROR_CODE_CONNECTION_RESET },
#endif

        { ECONNABORTED, WASI_NETWORK_ERROR_CODE_CONNECTION_ABORTED },

        { EMSGSIZE, WASI_NETWORK_ERROR_CODE_DATAGRAM_TOO_LARGE },

        { EHOSTUNREACH,
          WASI_NETWORK_ERROR_CODE_NAME_UNRESOLVABLE }, // no true correspondent
                                                       // in errno.h

        { ETIMEDOUT, WASI_NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE },

        { EIO,
          WASI_NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE } // no true
                                                               // correspondent
                                                               // in errno.h
    };

    for (size_t i = 0; i < sizeof(error_map) / sizeof(error_map[0]); i++) {
        if (error_map[i].err == err) {
            return error_map[i].wasi_err;
        }
    }

    return WASI_NETWORK_ERROR_CODE_UNKNOWN;
}

const char *
wasi_network_error_code_to_string(wasi_network_error_code_t error_code)
{
    switch (error_code) {
        case WASI_NETWORK_ERROR_CODE_UNKNOWN:
            return "unknown";
        case WASI_NETWORK_ERROR_CODE_ACCESS_DENIED:
            return "access-denied";
        case WASI_NETWORK_ERROR_CODE_NOT_SUPPORTED:
            return "not-supported";
        case WASI_NETWORK_ERROR_CODE_INVALID_ARGUMENT:
            return "invalid-argument";
        case WASI_NETWORK_ERROR_CODE_OUT_OF_MEMORY:
            return "out-of-memory";
        case WASI_NETWORK_ERROR_CODE_TIMEOUT:
            return "timeout";
        case WASI_NETWORK_ERROR_CODE_CONCURRENCY_CONFLICT:
            return "concurrency-conflict";
        case WASI_NETWORK_ERROR_CODE_NOT_IN_PROGRESS:
            return "not-in-progress";
        case WASI_NETWORK_ERROR_CODE_WOULD_BLOCK:
            return "would-block";
        case WASI_NETWORK_ERROR_CODE_INVALID_STATE:
            return "invalid-state";
        case WASI_NETWORK_ERROR_CODE_NEW_SOCKET_LIMIT:
            return "new-socket-limit";
        case WASI_NETWORK_ERROR_CODE_ADDRESS_NOT_BINDABLE:
            return "address-not-bindable";
        case WASI_NETWORK_ERROR_CODE_ADDRESS_IN_USE:
            return "address-in-use";
        case WASI_NETWORK_ERROR_CODE_REMOTE_UNREACHABLE:
            return "remote-unreachable";
        case WASI_NETWORK_ERROR_CODE_CONNECTION_REFUSED:
            return "connection-refused";
        case WASI_NETWORK_ERROR_CODE_CONNECTION_RESET:
            return "connection-reset";
        case WASI_NETWORK_ERROR_CODE_CONNECTION_ABORTED:
            return "connection-aborted";
        case WASI_NETWORK_ERROR_CODE_DATAGRAM_TOO_LARGE:
            return "datagram-too-large";
        case WASI_NETWORK_ERROR_CODE_NAME_UNRESOLVABLE:
            return "name-unresolvable";
        case WASI_NETWORK_ERROR_CODE_TEMPORARY_RESOLVER_FAILURE:
            return "temporary-resolver-failure";
        case WASI_NETWORK_ERROR_CODE_PERMANENT_RESOLVER_FAILURE:
            return "permanent-resolver-failure";
        default:
            return "unknown";
    }
}

/// @brief Copy string from host memory to wasm linear memory
/// @param exec_env exec env holding the wasm linear memory
/// @param wasm_offset offset to wasm linear memory where string will be copied
/// @param str string to be copied
/// @return true of copy was successfull, false otherwise
bool
copy_string_to_wasm(wasm_exec_env_t exec_env, int32_t *wasm_offset,
                    const char *str)
{
    int32_t str_len = strlen(str);
    uint32_t str_offset = 0;
    void *native_ptr = NULL;

    str_offset = wasm_runtime_call_realloc(exec_env->cx, 0, 0, 4, str_len + 1);
    native_ptr =
        wasm_runtime_addr_app_to_native_p2(exec_env, (uint64)str_offset);
    if (str_offset == 0) {
        return false;
    }

    memcpy(native_ptr, str, str_len + 1);

    wasm_offset[0] = (int32_t)str_offset;
    wasm_offset[1] = str_len;

    return true;
}

/// @brief Write IP address information to wasm linear memory
/// @param exec_env execution environment holding the desired wasm linear memory
/// @param is_some true if ip adrress info is not empty
/// @param ip_address ip adrress information to copy
/// @return
int32_t
wasi_p2_write_option_ip_address(wasm_exec_env_t exec_env, bool is_some,
                                const wasi_ip_address_t *ip_address)
{
    uint32_t wasm_offset = 0;
    uint8_t *native_ptr = NULL;

    wasm_offset = wasm_runtime_call_realloc(
        exec_env->cx, 0, 0, 4, is_some ? 1 + sizeof(wasi_ip_address_t) : 1);
    native_ptr = wasm_runtime_addr_app_to_native_p2(exec_env, wasm_offset);
    if (wasm_offset == 0) {
        return 0;
    }

    native_ptr[0] = is_some;
    if (is_some) {
        memcpy(native_ptr + 1, ip_address, sizeof(wasi_ip_address_t));
    }

    return (int32_t)wasm_offset;
}

/// @brief Copy string from wasm linear memory to host meory
/// @param exec_env execution environment holding the desired wasm linear memory
/// @param wasm_str_ptr address of string to be copied
/// @param wasm_str_len size of string
/// @return memory address where the string was written
char *
copy_wasm_string_to_native(wasm_exec_env_t exec_env,
                           const int32_t *wasm_str_ptr, int32_t wasm_str_len)
{
    char *native_str = wasm_runtime_malloc(wasm_str_len + 1);
    if (!native_str) {
        return NULL;
    }

    memcpy(native_str, wasm_str_ptr, wasm_str_len);
    native_str[wasm_str_len] = '\0';
    return native_str;
}

StringEncoding
wasm_get_string_encoding(WASMExecEnv *exec_env)
{
    if (!exec_env || !exec_env->cx || !exec_env->cx->canonical_opts
        || exec_env->cx->canonical_opts->lift_lower_opts
        || exec_env->cx->canonical_opts->lift_lower_opts->lift_opts
               ->string_encoding)
        return ENCODING_UTF_8;
    return exec_env->cx->canonical_opts->lift_lower_opts->lift_opts
        ->string_encoding;
}

wasi_filesystem_error_code_t
errno_to_wasi_filesystem(int err)
{
    static const struct {
        int err;
        wasi_filesystem_error_code_t wasi_err;
    } error_map[] = {
        { EACCES, WASI_FILESYSTEM_CODE_ACCESS },
        { EAGAIN, WASI_FILESYSTEM_CODE_WOULD_BLOCK },
        { EWOULDBLOCK, WASI_FILESYSTEM_CODE_WOULD_BLOCK },
        { EALREADY, WASI_FILESYSTEM_CODE_ALREADY },
        { EBADF, WASI_FILESYSTEM_CODE_BAD_DESCRIPTOR },
        { EBUSY, WASI_FILESYSTEM_CODE_BUSY },
        { EDEADLK, WASI_FILESYSTEM_CODE_DEADLOCK },
        { EDQUOT, WASI_FILESYSTEM_CODE_QUOTA },
        { EEXIST, WASI_FILESYSTEM_CODE_EXISTS },
        { EFBIG, WASI_FILESYSTEM_CODE_FILE_TOO_LARGE },
        { EILSEQ, WASI_FILESYSTEM_CODE_ILLEGAL_BYTE_SEQUENCE },
        { EINPROGRESS, WASI_FILESYSTEM_CODE_IN_PROGRESS },
        { EINTR, WASI_FILESYSTEM_CODE_INTERRUPTED },
        { EINVAL, WASI_FILESYSTEM_CODE_INVALID },
        { EIO, WASI_FILESYSTEM_CODE_IO },
        { EISDIR, WASI_FILESYSTEM_CODE_IS_DIRECTORY },
        { ELOOP, WASI_FILESYSTEM_CODE_LOOP },
        { EMLINK, WASI_FILESYSTEM_CODE_TOO_MANY_LINKS },
        { EMSGSIZE, WASI_FILESYSTEM_CODE_MESSAGE_SIZE },
        { ENAMETOOLONG, WASI_FILESYSTEM_CODE_NAME_TOO_LONG },
        { ENODEV, WASI_FILESYSTEM_CODE_NO_DEVICE },
        { ENOENT, WASI_FILESYSTEM_CODE_NO_ENTRY },
        { ENOLCK, WASI_FILESYSTEM_CODE_NO_LOCK },
        { ENOMEM, WASI_FILESYSTEM_CODE_INSUFFICIENT_MEMORY },
        { ENOSPC, WASI_FILESYSTEM_CODE_INSUFFICIENT_SPACE },
        { ENOTDIR, WASI_FILESYSTEM_CODE_NOT_DIRECTORY },
        { ENOTEMPTY, WASI_FILESYSTEM_CODE_NOT_EMPTY },
        { ENOTRECOVERABLE, WASI_FILESYSTEM_CODE_NOT_RECOVERABLE },
        { ENOTSUP, WASI_FILESYSTEM_CODE_UNSUPPORTED },
        { ENOSYS, WASI_FILESYSTEM_CODE_UNSUPPORTED },
        { ENOTTY, WASI_FILESYSTEM_CODE_NO_TTY },
        { ENXIO, WASI_FILESYSTEM_CODE_NO_SUCH_DEVICE },
        { EOVERFLOW, WASI_FILESYSTEM_CODE_OVERFLOW },
        { EPERM, WASI_FILESYSTEM_CODE_NOT_PERMITTED },
        { EPIPE, WASI_FILESYSTEM_CODE_PIPE },
        { EROFS, WASI_FILESYSTEM_CODE_READ_ONLY },
        { ESPIPE, WASI_FILESYSTEM_CODE_INVALID_SEEK },
        { ETXTBSY, WASI_FILESYSTEM_CODE_TEXT_FILE_BUSY },
        { EXDEV, WASI_FILESYSTEM_CODE_CROSS_DEVICE },
    };

    for (size_t i = 0; i < sizeof(error_map) / sizeof(error_map[0]); i++) {
        if (error_map[i].err == err) {
            return error_map[i].wasi_err;
        }
    }

    return WASI_FILESYSTEM_CODE_INVALID;
}

const char *
wasi_filesystem_error_code_to_string(wasi_filesystem_error_code_t error_code)
{
    switch (error_code) {
        case WASI_FILESYSTEM_CODE_ACCESS:
            return "access";
        case WASI_FILESYSTEM_CODE_WOULD_BLOCK:
            return "would-block";
        case WASI_FILESYSTEM_CODE_ALREADY:
            return "already";
        case WASI_FILESYSTEM_CODE_BAD_DESCRIPTOR:
            return "bad-descriptor";
        case WASI_FILESYSTEM_CODE_BUSY:
            return "busy";
        case WASI_FILESYSTEM_CODE_DEADLOCK:
            return "deadlock";
        case WASI_FILESYSTEM_CODE_QUOTA:
            return "quota";
        case WASI_FILESYSTEM_CODE_EXISTS:
            return "exist";
        case WASI_FILESYSTEM_CODE_FILE_TOO_LARGE:
            return "file-too-large";
        case WASI_FILESYSTEM_CODE_ILLEGAL_BYTE_SEQUENCE:
            return "illegal-byte-sequence";
        case WASI_FILESYSTEM_CODE_IN_PROGRESS:
            return "in-progress";
        case WASI_FILESYSTEM_CODE_INTERRUPTED:
            return "interrupted";
        case WASI_FILESYSTEM_CODE_INVALID:
            return "invalid";
        case WASI_FILESYSTEM_CODE_IO:
            return "io";
        case WASI_FILESYSTEM_CODE_IS_DIRECTORY:
            return "is-directory";
        case WASI_FILESYSTEM_CODE_LOOP:
            return "loop";
        case WASI_FILESYSTEM_CODE_TOO_MANY_LINKS:
            return "too-many-links";
        case WASI_FILESYSTEM_CODE_MESSAGE_SIZE:
            return "message-size";
        case WASI_FILESYSTEM_CODE_NAME_TOO_LONG:
            return "name-too-long";
        case WASI_FILESYSTEM_CODE_NO_DEVICE:
            return "no-device";
        case WASI_FILESYSTEM_CODE_NO_ENTRY:
            return "no-entry";
        case WASI_FILESYSTEM_CODE_NO_LOCK:
            return "no-lock";
        case WASI_FILESYSTEM_CODE_INSUFFICIENT_MEMORY:
            return "insufficient-memory";
        case WASI_FILESYSTEM_CODE_INSUFFICIENT_SPACE:
            return "insufficient-space";
        case WASI_FILESYSTEM_CODE_NOT_DIRECTORY:
            return "not-directory";
        case WASI_FILESYSTEM_CODE_NOT_EMPTY:
            return "not-empty";
        case WASI_FILESYSTEM_CODE_NOT_RECOVERABLE:
            return "not-recoverable";
        case WASI_FILESYSTEM_CODE_UNSUPPORTED:
            return "unsupported";
        case WASI_FILESYSTEM_CODE_NO_TTY:
            return "no-tty";
        case WASI_FILESYSTEM_CODE_NO_SUCH_DEVICE:
            return "no-such-device";
        case WASI_FILESYSTEM_CODE_OVERFLOW:
            return "overflow";
        case WASI_FILESYSTEM_CODE_NOT_PERMITTED:
            return "not-permitted";
        case WASI_FILESYSTEM_CODE_PIPE:
            return "pipe";
        case WASI_FILESYSTEM_CODE_READ_ONLY:
            return "read-only";
        case WASI_FILESYSTEM_CODE_INVALID_SEEK:
            return "invalid-seek";
        case WASI_FILESYSTEM_CODE_TEXT_FILE_BUSY:
            return "text-file-busy";
        case WASI_FILESYSTEM_CODE_CROSS_DEVICE:
            return "cross-device";
        default:
            return "invalid";
    }
}

wit_value_t
get_result_error_val(uint32_t error_code)
{
    wit_value_t error_val = wit_enum_ctor(error_code);
    return wit_result_ctor(true, error_val);
}

wit_value_t
get_datetime(uint64_t seconds, uint32_t nanoseconds)
{
    wit_value_t seconds_val = wit_u64_ctor(seconds);
    wit_value_t nanoseconds_val = wit_u32_ctor(nanoseconds);
    ComponentWITRecordField *datetime_fields =
        (ComponentWITRecordField *)wasm_runtime_malloc(
            2 * sizeof(ComponentWITRecordField));
    init_record_field(&datetime_fields[0], "seconds", 7, seconds_val);
    init_record_field(&datetime_fields[1], "nanoseconds", 11, nanoseconds_val);

    return wit_record_ctor(datetime_fields, 2);
}

wit_value_t
get_result_datetime(uint64_t seconds, uint32_t nanoseconds)
{
    wit_value_t datetime_val = get_datetime(seconds, nanoseconds);
    return wit_result_ctor(false, datetime_val);
}
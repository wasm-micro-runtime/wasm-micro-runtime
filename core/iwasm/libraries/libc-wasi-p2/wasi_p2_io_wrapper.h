/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_IO_WRAPPER_H
#define WASI_P2_IO_WRAPPER_H

#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/* wasi:io/error */

void
wasi_io_error_to_debug_string_wrapper(wasm_exec_env_t exec_env,
                                      uint32_t error_handle,
                                      uint32_t offset_addr);

/* wasi:io/poll */

uint32_t
wasi_io_poll_pollable_ready_wrapper(wasm_exec_env_t exec_env,
                                    uint32_t pollable_handle);

void
wasi_io_poll_pollable_block_wrapper(wasm_exec_env_t exec_env,
                                    uint32_t pollable_handle);

void
wasi_io_poll_poll_wrapper(wasm_exec_env_t exec_env, uint32_t pollables_ptr,
                          uint32_t pollables_len, uint32_t offset_addr);

/* wasi:io/streams */

void
wasi_io_streams_input_stream_read_wrapper(wasm_exec_env_t exec_env,
                                          uint32_t input_stream_handle,
                                          int64_t len, uint32_t offset_addr);
void
wasi_io_streams_input_stream_blocking_read_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t input_stream_handle,
                                                   int64_t len,
                                                   uint32_t offset_addr);
void
wasi_io_streams_input_stream_skip_wrapper(wasm_exec_env_t exec_env,
                                          uint32_t input_stream_handle,
                                          int64_t len, uint32_t offset_addr);
void
wasi_io_streams_input_stream_blocking_skip_wrapper(wasm_exec_env_t exec_env,
                                                   uint32_t input_stream_handle,
                                                   int64_t len,
                                                   uint32_t offset_addr);
uint32_t
wasi_io_streams_input_stream_subscribe_wrapper(wasm_exec_env_t exec_env,
                                               uint32_t input_stream_handle);
void
wasi_io_streams_output_stream_check_write_wrapper(wasm_exec_env_t exec_env,
                                                  uint32_t output_stream_handle,
                                                  uint32_t offset_addr);
void
wasi_io_streams_output_stream_write_wrapper(wasm_exec_env_t exec_env,
                                            uint32_t output_stream_handle,
                                            uint32_t contents_ptr,
                                            uint32_t contents_len,
                                            uint32_t offset_addr);
void
wasi_io_streams_output_stream_blocking_write_and_flush_wrapper(
    wasm_exec_env_t exec_env, uint32_t output_stream_handle,
    uint32_t contents_ptr, uint32_t contents_len, uint32_t offset_addr);
void
wasi_io_streams_output_stream_flush_wrapper(wasm_exec_env_t exec_env,
                                            uint32_t output_stream_handle,
                                            uint32_t offset_addr);
void
wasi_io_streams_output_stream_blocking_flush_wrapper(
    wasm_exec_env_t exec_env, uint32_t output_stream_handle,
    uint32_t offset_addr);
uint32_t
wasi_io_streams_output_stream_subscribe_wrapper(wasm_exec_env_t exec_env,
                                                uint32_t output_stream_handle);
void
wasi_io_streams_output_stream_write_zeroes_wrapper(
    wasm_exec_env_t exec_env, uint32_t output_stream_handle, int64_t len,
    uint32_t offset_addr);
void
wasi_io_streams_output_stream_blocking_write_zeroes_and_flush_wrapper(
    wasm_exec_env_t exec_env, uint32_t output_stream_handle, int64_t len,
    uint32_t offset_addr);
void
wasi_io_streams_output_stream_splice_wrapper(wasm_exec_env_t exec_env,
                                             uint32_t output_stream_handle,
                                             uint32_t src_input_stream_handle,
                                             int64_t len, uint32_t offset_addr);
void
wasi_io_streams_output_stream_blocking_splice_wrapper(
    wasm_exec_env_t exec_env, uint32_t output_stream_handle,
    uint32_t src_input_stream_handle, int64_t len, uint32_t offset_addr);

#ifdef __cplusplus
}
#endif

#endif /* WASI_P2_IO_WRAPPER_H */

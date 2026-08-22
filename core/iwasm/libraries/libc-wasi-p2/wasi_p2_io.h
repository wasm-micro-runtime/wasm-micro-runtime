/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_IO_H
#define WASI_P2_IO_H

#include <stdint.h>
#include <stdbool.h>

#include "wasi_p2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// wasi:io/error
void
wasi_pollable_block(wasi_pollable_context_t *pollable);

bool
wasi_pollable_ready(wasi_pollable_context_t *pollable);

void
wasi_poll(const wasi_pollable_context_t **pollables, uint32_t n_pollables,
          wasi_list_u32_t *ret);

// wasi:io/streams
void
wasi_input_stream_read(wasi_input_stream_t stream, uint64_t len,
                       wasi_result_list_u8_stream_error_t *ret);

void
wasi_input_stream_blocking_read(wasi_input_stream_t stream, uint64_t len,
                                wasi_result_list_u8_stream_error_t *ret);

void
wasi_input_stream_skip(wasi_input_stream_t stream, uint64_t len,
                       wasi_result_u64_stream_error_t *ret);

void
wasi_input_stream_blocking_skip(wasi_input_stream_t stream, uint64_t len,
                                wasi_result_u64_stream_error_t *ret);

void
wasi_output_stream_check_write(wasi_output_stream_t stream,
                               wasi_result_u64_stream_error_t *ret);

void
wasi_output_stream_write(wasi_output_stream_t stream,
                         const wasi_list_u8_t *payload,
                         wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_blocking_write_and_flush(
    wasi_output_stream_t stream, const wasi_list_u8_t *payload,
    wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_flush(wasi_output_stream_t stream,
                         wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_blocking_flush(wasi_output_stream_t stream,
                                  wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_write_zeroes(wasi_output_stream_t stream, uint64_t len,
                                wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_blocking_write_zeroes_and_flush(
    wasi_output_stream_t stream, uint64_t len,
    wasi_result_void_stream_error_t *ret);

void
wasi_output_stream_splice(wasi_output_stream_t stream, wasi_input_stream_t src,
                          uint64_t len, wasi_result_u64_stream_error_t *ret);

void
wasi_output_stream_blocking_splice(wasi_output_stream_t stream,
                                   wasi_input_stream_t src, uint64_t len,
                                   wasi_result_u64_stream_error_t *ret);

void
pollable_dtor(void *data);

#ifdef __cplusplus
}
#endif

#endif /* WASI_IO_H */

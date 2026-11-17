/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef WASI_HTTP_IO_H
#define WASI_HTTP_IO_H
#include "wasi_http.h"

bool
http_start_task(wasi_http *http, p3_task *task, uint32_t request,
                uint32_t *body, uint32_t *trailers);
bool
http_close_end(wasi_http *http, uint32_t *end);
bool
http_send_future(wasi_http *http, uint32_t *end, bool error);
bool
http_receive_trailers(wasi_http *http, uint32_t *end, uint32_t *trailers);
#endif

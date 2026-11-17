/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_H2_H
#define STACKLESS_H2_H
#include "wasi_http.h"
#include <nghttp2/nghttp2.h>

#ifndef H2_STREAMS
#define H2_STREAMS 4
#define H2_PER_CONNECTION 4
#define H2_CONNECTIONS 2
#define H2_CONNECTION_HEAP (28 * 1024)
#endif
#define H2_WINDOW 2048
#define H2_MAX_BODY (64 * 1024)

typedef struct h2_server h2_server;
typedef struct h2_connection {
    h2_server *server;
    nghttp2_session *session;
    const uint8_t *wire;
    size_t wire_size, heap;
    bool failed;
} h2_connection;
typedef struct h2_stream {
    h2_connection *connection;
    p3_task task;
    int32_t id;
    uint32_t request, input, input_trailers, response, output_trailers,
        received;
    uint16_t read_at, queued;
    bool input_end, local_end, closed, aborted, deferred, send_body;
    bool method, scheme;
    uint8_t input_bytes[H2_WINDOW];
} h2_stream;
struct h2_server {
    p3_vm vm;
    wasi_http http;
    h2_connection connections[H2_CONNECTIONS];
    h2_stream streams[H2_STREAMS];
    size_t heap, heap_peak;
    unsigned live, peak, completed, reset, refused, protocol_errors;
    unsigned callbacks, input_peak;
};

bool
h2_open(h2_server *server, unsigned slot);
void
h2_close(h2_connection *c);
bool
h2_receive(h2_connection *c, const uint8_t *bytes, size_t size);
bool
h2_output(h2_connection *c);
bool
h2_pump(h2_server *server, bool *ready);
#endif

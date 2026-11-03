/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_HTTP_H
#define STACKLESS_HTTP_H
#include "vm.h"
#define HTTP_CONNECTIONS 4
#define HTTP_CHUNK 1024
typedef struct http_connection {
    p3_task task;
    uint32_t input;
    size_t sent, length;
    bool live, disconnected, done;
    uint8_t outcome, output[HTTP_CHUNK];
} http_connection;
typedef struct http_server {
    p3_vm vm;
    http_connection connections[HTTP_CONNECTIONS];
    uint32_t completed, cancelled, disconnected, callbacks;
} http_server;
bool
http_open(http_server *server, unsigned slot);
bool
http_input(http_server *server, unsigned slot, uint8_t *data, uint32_t size);
bool
http_end_input(http_server *server, unsigned slot);
bool
http_disconnect(http_server *server, unsigned slot);
bool
http_progress(http_server *server, unsigned slot);
bool
http_finished(const http_connection *connection);
bool
http_reap(http_server *server, unsigned slot);
bool
http_native_loop(http_server *server, int32_t iterations);
#endif

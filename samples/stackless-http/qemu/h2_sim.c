/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "h2.h"
#include "h2_inputs.h"
#include "payloads.h"
#include "standard_handler.h"
#include "upload.h"
#include "wasm_export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static h2_server server;
static _Alignas(16) uint8_t pool[128 * 1024];
int
pico_libc_peak(void);
void
systick(void)
{
}
#define CHECK(condition)                                                \
    do {                                                                \
        if (!(condition)) {                                             \
            printf("H2 M33 FAIL %d %s: %s %s\n", __LINE__, #condition,  \
                   server.vm.error,                                     \
                   server.vm.async.error ? server.vm.async.error : ""); \
            fflush(stdout);                                             \
            exit(1);                                                    \
        }                                                               \
    } while (0)

typedef struct job {
    const uint8_t *body;
    uint32_t size, offset;
    bool headers, end, trailers;
} job;
typedef struct peer {
    job jobs[2];
    uint32_t upload, window, stream_window;
} peer;
static uint32_t
be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8
           | p[3];
}
static void
feed(h2_connection *c, uint8_t type, uint8_t flags, uint8_t stream,
     const uint8_t *payload, uint32_t length)
{
    uint8_t frame[9 + 379] = {
        0, (uint8_t)(length >> 8), (uint8_t)length, type, flags, 0, 0, 0, stream
    };
    CHECK(length <= sizeof(frame) - 9);
    if (length)
        memcpy(frame + 9, payload, length);
    CHECK(h2_receive(c, frame, 9 + length));
}
static void
allow_response(h2_connection *c, uint8_t stream)
{
    static const uint8_t credit[] = { 0, 0, 0x50, 0 };
    feed(c, NGHTTP2_WINDOW_UPDATE, 0, stream, credit, sizeof(credit));
}
static void
drain(h2_connection *c, peer *p)
{
    CHECK(h2_output(c));
    while (c->wire_size) {
        const uint8_t *frame = c->wire;
        CHECK(c->wire_size >= 9);
        uint32_t length =
            (uint32_t)frame[0] << 16 | (uint32_t)frame[1] << 8 | frame[2];
        uint32_t id = be32(frame + 5);
        CHECK(length == c->wire_size - 9);
        const uint8_t *data = frame + 9;
        switch (frame[3]) {
            case NGHTTP2_WINDOW_UPDATE:
                CHECK(length == 4 && (id == 0 || id == 3));
                if (id)
                    p->stream_window += be32(data);
                else
                    p->window += be32(data);
                break;
            case NGHTTP2_HEADERS:
            {
                CHECK(id == 1 || id == 3);
                job *j = &p->jobs[id >> 1];
                CHECK(!j->end && (frame[4] & NGHTTP2_FLAG_END_HEADERS));
                if (j->headers) {
                    CHECK(id == 3 && (frame[4] & NGHTTP2_FLAG_END_STREAM));
                    j->trailers = j->end = true;
                }
                j->headers = true;
                break;
            }
            case NGHTTP2_DATA:
            {
                CHECK(id == 1 || id == 3);
                job *j = &p->jobs[id >> 1];
                CHECK(j->headers && !j->end && length <= j->size - j->offset);
                CHECK(!memcmp(data, j->body + j->offset, length));
                j->offset += length;
                j->end = (frame[4] & NGHTTP2_FLAG_END_STREAM) != 0;
                break;
            }
            case NGHTTP2_SETTINGS:
                if (!(frame[4] & NGHTTP2_FLAG_ACK))
                    feed(c, NGHTTP2_SETTINGS, NGHTTP2_FLAG_ACK, 0, NULL, 0);
                break;
            default:
                CHECK(false);
        }
        c->wire_size = 0;
        CHECK(h2_output(c));
    }
}
int
main(void)
{
    MemAllocOption mem = { 0 };
    mem.pool.heap_buf = pool;
    mem.pool.heap_size = sizeof(pool);
    CHECK(p3_vm_open_with_engine(
        &server.vm, wasm_engine_new_with_args(Alloc_With_Pool, &mem),
        handler_wasm, sizeof(handler_wasm)));
    wasi_http_init(&server.http, &server.vm);
    peer peers[2] = {
        { .jobs = { { json_body, sizeof(json_body) },
                    { upload_body, sizeof(upload_body) } },
          .window = 65535,
          .stream_window = H2_WINDOW },
        { .jobs = { { png_body, sizeof(png_body) },
                    { upload_body, sizeof(upload_body) } },
          .window = 65535,
          .stream_window = H2_WINDOW },
    };
    for (unsigned i = 0; i < 2; ++i)
        CHECK(h2_open(&server, i));
    CHECK(h2_receive(&server.connections[0], h2_json_requests,
                     sizeof(h2_json_requests)));
    CHECK(h2_receive(&server.connections[1], h2_png_requests,
                     sizeof(h2_png_requests)));
    allow_response(&server.connections[0], 1);
    allow_response(&server.connections[1], 1);
    allow_response(&server.connections[0], 3);
    bool ready, progressed_while_blocked = false;
    for (unsigned tick = 0; tick < 8192 && server.live; ++tick) {
        CHECK(h2_pump(&server, &ready));
        for (unsigned i = 0; i < 2; ++i) {
            h2_connection *c = &server.connections[i];
            peer *p = &peers[i];
            drain(c, p);
            if (p->upload == sizeof(upload_body))
                continue;
            uint32_t n = sizeof(upload_body) - p->upload;
            if (n > p->window)
                n = p->window;
            if (n > p->stream_window)
                n = p->stream_window;
            if (n > 379)
                n = 379;
            if (!n)
                continue;
            bool end = p->upload + n == sizeof(upload_body);
            feed(c, NGHTTP2_DATA, end ? NGHTTP2_FLAG_END_STREAM : 0, 3,
                 upload_body + p->upload, n);
            p->window -= n;
            p->stream_window -= n;
            p->upload += n;
        }
        if (!progressed_while_blocked && peers[0].jobs[1].end) {
            CHECK(!peers[1].jobs[1].offset && !peers[1].jobs[1].end);
            progressed_while_blocked = true;
            allow_response(&server.connections[1], 3);
        }
    }
    CHECK(progressed_while_blocked && server.completed == 4
          && server.peak == 4);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(peers[i].jobs[0].offset == peers[i].jobs[0].size
              && peers[i].jobs[0].end);
        CHECK(peers[i].jobs[1].offset == peers[i].jobs[1].size
              && peers[i].jobs[1].trailers);
        h2_close(&server.connections[i]);
    }
    CHECK(!server.live && !server.vm.async.live && !server.http.live
          && !server.heap);
    mem_alloc_info_t usage = { 0 };
    CHECK(wasm_runtime_get_mem_alloc_info(&usage));
    printf("H2 M33 PASS completed=%lu peak_streams=%lu peak_handles=%lu "
           "peak_resources=%lu "
           "input_peak=%lu h2_heap_peak=%lu libc_peak=%d pool_peak=%lu "
           "server_bytes=%lu\n",
           (unsigned long)server.completed, (unsigned long)server.peak,
           (unsigned long)server.vm.async.peak, (unsigned long)server.http.peak,
           (unsigned long)server.input_peak, (unsigned long)server.heap_peak,
           pico_libc_peak(), (unsigned long)usage.highmark_size,
           (unsigned long)sizeof(server));
    p3_vm_close(&server.vm);
    fflush(stdout);
    return 0;
}

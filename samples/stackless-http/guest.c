/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/* Freestanding wasm32 guest. The build wraps these imports/exports in a
 * validated Component Model component with canonical stackless lifting. */
#include <stdint.h>
#include <stddef.h>
#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))
#define BLOCKED UINT32_MAX
#define CHUNK 1024U
#define CONNECTIONS 4U

IMPORT("stream.new") uint64_t stream_new(void);
IMPORT("stream.read") uint32_t stream_read(uint32_t, uint32_t, uint32_t);
IMPORT("stream.write") uint32_t stream_write(uint32_t, uint32_t, uint32_t);
IMPORT("stream.cancel-read") uint32_t cancel_read(uint32_t);
IMPORT("stream.drop-readable") void drop_read(uint32_t);
IMPORT("stream.drop-writable") void drop_write(uint32_t);
IMPORT("future.new") uint64_t future_new(void);
IMPORT("future.write") uint32_t future_write(uint32_t, uint32_t);
IMPORT("future.drop-writable") void drop_future(uint32_t);
IMPORT("waitable-set.new") uint32_t set_new(void);
IMPORT("waitable-set.drop") void set_drop(uint32_t);
IMPORT("waitable.join") void join(uint32_t, uint32_t);
IMPORT("task.return") void task_return(uint32_t, uint32_t);
IMPORT("task.cancel") void task_cancel(void);
IMPORT("context.get") uint32_t context_get(void);
IMPORT("context.set") void context_set(uint32_t);

enum { HEADERS = 1, RESPONSE_HEADER, RESPONSE_BODY, FINISH };
typedef struct {
    uint32_t set, rx, tx, done, phase, count, length, offset;
    uint32_t header_length;
    const char *header;
    uint8_t result;
    uint8_t input[2 * CHUNK], output[CHUNK];
} Request;
static Request requests[CONNECTIONS];
static const char small_header[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 15\r\nConnection: close\r\n\r\n";
static const char large_header[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n";
static const char bad_header[] = "HTTP/1.1 400 Bad Request\r\nContent-Length: "
                                 "0\r\nConnection: close\r\n\r\n";
static const char long_header[] =
    "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: "
    "0\r\nConnection: close\r\n\r\n";
static const char small_body[] = "stackless wasm\n";

static uint32_t
ptr(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}
static uint32_t
wait_for(Request *r)
{
    return 2U | (r->set << 4);
}
static uint32_t
exit_request(Request *r)
{
    if (r->done)
        drop_future(r->done);
    set_drop(r->set);
    r->set = r->phase = r->rx = r->tx = r->done = 0;
    return 0;
}
static uint32_t
finish(Request *r, uint8_t error)
{
    drop_write(r->tx);
    r->tx = 0;
    r->result = error;
    r->phase = FINISH;
    if (future_write(r->done, ptr(&r->result)) == BLOCKED)
        return wait_for(r);
    return exit_request(r);
}
static uint32_t
emit_response(Request *r, uint32_t previous)
{
    /* At most one bounded chunk per callback. This native adapter only takes
     * bytes when it has socket capacity, so a fresh write parks at WAIT. */
    if ((previous & 15U) == 1)
        return finish(r, 1);
    r->offset += previous >> 4;
    if (r->phase == RESPONSE_HEADER && r->offset == r->header_length) {
        r->phase = RESPONSE_BODY;
        r->offset = 0;
    }
    uint32_t status;
    if (r->phase == RESPONSE_HEADER)
        status = stream_write(r->tx, ptr(r->header + r->offset),
                              r->header_length - r->offset);
    else if (r->offset == r->length)
        return finish(r, 0);
    else {
        uint32_t n = r->length - r->offset;
        if (n > CHUNK)
            n = CHUNK;
        if (r->length == sizeof(small_body) - 1)
            status = stream_write(r->tx, ptr(small_body + r->offset), n);
        else {
            for (uint32_t i = 0; i < n; ++i)
                r->output[i] = (uint8_t)('a' + ((r->offset + i) % 26));
            status = stream_write(r->tx, ptr(r->output), n);
        }
    }
    if (status == BLOCKED)
        return wait_for(r);
    if ((status & 15U) == 1)
        return finish(r, 1);
    /* Immediate completion is legal with another adapter. Yield rather than
     * monopolizing a host turn on an arbitrarily large body. */
    r->offset += status >> 4;
    return 1U | (r->set << 4);
}
static uint32_t
respond(Request *r, const char *header, uint32_t header_len, uint32_t length)
{
    drop_read(r->rx);
    r->rx = 0;
    uint64_t body = stream_new(), done = future_new();
    r->tx = (uint32_t)(body >> 32);
    r->done = (uint32_t)(done >> 32);
    join(r->tx, r->set);
    join(r->done, r->set);
    r->header = header;
    r->header_length = header_len;
    r->length = length;
    r->offset = 0;
    r->phase = RESPONSE_HEADER;
    /* The lifted call resolves before either returned endpoint completes. */
    task_return((uint32_t)body, (uint32_t)done);
    return emit_response(r, 0);
}
static uint32_t
receive_headers(Request *r, uint32_t status)
{
    if ((status & 15U) == 1)
        return respond(r, bad_header, sizeof(bad_header) - 1, 0);
    uint32_t before = r->count;
    r->count += status >> 4;
    for (uint32_t i = before > 3 ? before - 3 : 0; i + 3 < r->count; ++i)
        if (r->input[i] == '\r' && r->input[i + 1] == '\n'
            && r->input[i + 2] == '\r' && r->input[i + 3] == '\n') {
            const uint8_t *a = r->input;
            if (i < 6 || a[0] != 'G' || a[1] != 'E' || a[2] != 'T'
                || a[3] != ' ' || a[4] != '/')
                return respond(r, bad_header, sizeof(bad_header) - 1, 0);
            if (i >= 11 && a[5] == 'l' && a[6] == 'a' && a[7] == 'r'
                && a[8] == 'g' && a[9] == 'e' && a[10] == ' ')
                return respond(r, large_header, sizeof(large_header) - 1,
                               65536);
            return respond(r, small_header, sizeof(small_header) - 1,
                           sizeof(small_body) - 1);
        }
    if (r->count == sizeof(r->input))
        return respond(r, long_header, sizeof(long_header) - 1, 0);
    status = stream_read(r->rx, ptr(r->input + r->count),
                         sizeof(r->input) - r->count);
    if (status != BLOCKED)
        __builtin_trap(); /* This profile supplies input only after WAIT. */
    return wait_for(r);
}
EXPORT("handle") uint32_t handle(uint32_t slot, uint32_t input)
{
    if (slot >= CONNECTIONS || requests[slot].phase)
        __builtin_trap();
    Request *r = &requests[slot];
    context_set(slot);
    r->phase = HEADERS;
    r->count = 0;
    r->set = set_new();
    r->rx = input;
    join(input, r->set);
    return receive_headers(r, 0);
}
EXPORT("handle-event")
uint32_t handle_event(uint32_t code, uint32_t endpoint, uint32_t payload)
{
    /* Task-specific context is supplied by the canonical context slot. */
    uint32_t slot = context_get();
    if (slot >= CONNECTIONS)
        __builtin_trap();
    Request *r = &requests[slot];
    if (code == 6 && r->phase == HEADERS) {
        cancel_read(r->rx);
        drop_read(r->rx);
        task_cancel();
        return exit_request(r);
    }
    if (code == 2 && endpoint == r->rx && r->phase == HEADERS)
        return receive_headers(r, payload);
    if ((code == 3 && endpoint == r->tx) || (code == 0 && r->tx))
        return emit_response(r, payload);
    if (code == 5 && endpoint == r->done && r->phase == FINISH)
        return exit_request(r);
    __builtin_trap();
}
EXPORT("native-loop") uint32_t native_loop(uint32_t iterations)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < iterations; ++i)
        total += context_get();
    return total;
}

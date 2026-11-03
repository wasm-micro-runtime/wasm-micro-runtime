/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include <stdint.h>
#include <stddef.h>
#include "payloads.h"

#define IMPORT(module, name) \
    __attribute__((import_module(module), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))
#define TYPES "wasi:http/types@0.3.2"
#define HANDLER "wasi:http/handler@0.3.2"
#define BLOCKED UINT32_MAX
#define CHUNK 768
#define CONNECTIONS 4

IMPORT(TYPES, "[constructor]fields") uint32_t fields_new(void);
IMPORT(TYPES, "[method]fields.append")
void append(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
IMPORT(TYPES, "[method]request.get-method") void get_method(uint32_t, uint32_t);
IMPORT(TYPES, "[method]request.get-path-with-query")
void get_path(uint32_t, uint32_t);
IMPORT(TYPES, "[resource-drop]request") void drop_request(uint32_t);
IMPORT(TYPES, "[static]request.consume-body")
void consume_body(uint32_t, uint32_t, uint32_t);
IMPORT(TYPES, "[static]response.new")
void response_new(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
IMPORT(TYPES, "[method]response.set-status-code")
uint32_t set_status(uint32_t, uint32_t);
IMPORT("[export]" HANDLER, "[task-return]handle")
void
task_return(uint32_t, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t,
            uint32_t, uint32_t);

#define BODY_READ "[static]request.consume-body"
#define BODY_WRITE "[static]response.new"
#define STATUS_FUTURE "[static]request.consume-body"
#define TRAILERS_FUTURE "[static]response.new"
IMPORT(TYPES, "[stream-new-0]" BODY_WRITE) uint64_t stream_new(void);
IMPORT(TYPES, "[async-lower][stream-read-1]" BODY_READ)
uint32_t
stream_read(uint32_t, uint32_t, uint32_t);
IMPORT(TYPES, "[async-lower][stream-write-0]" BODY_WRITE)
uint32_t
stream_write(uint32_t, uint32_t, uint32_t);
IMPORT(TYPES, "[stream-drop-readable-1]" BODY_READ) void drop_read(uint32_t);
IMPORT(TYPES, "[stream-drop-writable-0]" BODY_WRITE) void drop_write(uint32_t);
IMPORT(TYPES, "[future-new-0]" STATUS_FUTURE) uint64_t status_new(void);
IMPORT(TYPES, "[async-lower][future-write-0]" STATUS_FUTURE)
uint32_t
status_write(uint32_t, uint32_t);
IMPORT(TYPES, "[future-drop-writable-0]" STATUS_FUTURE)
void status_drop_write(uint32_t);
IMPORT(TYPES, "[future-drop-readable-2]" BODY_WRITE)
void status_drop_read(uint32_t);
IMPORT(TYPES, "[future-new-1]" TRAILERS_FUTURE) uint64_t trailers_new(void);
IMPORT(TYPES, "[async-lower][future-write-1]" TRAILERS_FUTURE)
uint32_t
trailers_write(uint32_t, uint32_t);
IMPORT(TYPES, "[async-lower][future-read-2]" BODY_READ)
uint32_t
trailers_read(uint32_t, uint32_t);
IMPORT(TYPES, "[future-drop-writable-1]" TRAILERS_FUTURE)
void trailers_drop_write(uint32_t);
IMPORT(TYPES, "[future-drop-readable-2]" BODY_READ)
void trailers_drop_read(uint32_t);
IMPORT("$root", "[waitable-set-new]") uint32_t set_new(void);
IMPORT("$root", "[waitable-set-drop]") void set_drop(uint32_t);
IMPORT("$root", "[waitable-join]") void join(uint32_t, uint32_t);
IMPORT("$root", "[context-get-0]") uint32_t context_get(void);
IMPORT("$root", "[context-set-0]") void context_set(uint32_t);

enum phase { READ = 1, WRITE, TRAILERS_READ, STATUS_WRITE, TRAILERS_WRITE };
typedef struct request {
    uint32_t set, rx, tx, input_status, input_trailers, output_trailers;
    uint32_t offset, size;
    const uint8_t *bytes;
    enum phase phase;
    uint8_t upload[CHUNK];
    _Alignas(8) uint8_t result[40];
    uint8_t eof, lost;
} Request;

static Request active[CONNECTIONS];
static uint8_t path[512];

static uint32_t
ptr(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}
EXPORT("cabi_realloc")
uint32_t
cabi_realloc(uint32_t old, uint32_t old_size, uint32_t align, uint32_t size)
{
    /* Only the path getter allocates in this synchronous, bounded demo. */
    if (old || old_size || align != 1 || size > sizeof(path))
        __builtin_trap();
    return ptr(path);
}
static int
same(const uint8_t *text, uint32_t size, const char *expected, uint32_t len)
{
    if (size != len)
        return 0;
    for (uint32_t i = 0; i < len; ++i)
        if (text[i] != (uint8_t)expected[i])
            return 0;
    return 1;
}
static uint32_t
wait(Request *r)
{
    return (r->set << 4) | 2;
}
static uint32_t
yield(void)
{
    return 1;
}
static uint32_t
write_trailers(Request *r)
{
    r->result[0] = r->result[8] = 0;
    if (r->input_status && !r->lost) {
        uint32_t trailers = fields_new();
        uint32_t header_result[4];
        append(trailers, ptr("x-upload-check"), 14, ptr("complete"), 8,
               ptr(header_result));
        if (header_result[0])
            __builtin_trap();
        r->result[8] = 1;
        for (unsigned i = 0; i < 4; ++i)
            r->result[12 + i] = (uint8_t)(trailers >> (i * 8));
    }
    r->phase = TRAILERS_WRITE;
    uint32_t status = trailers_write(r->output_trailers, ptr(r->result));
    if (status == BLOCKED)
        return wait(r);
    trailers_drop_write(r->output_trailers);
    set_drop(r->set);
    r->set = 0;
    return 0;
}
static uint32_t
write_status(Request *r, uint32_t status)
{
    trailers_drop_read(r->input_trailers);
    r->input_trailers = 0;
    r->result[0] = r->lost || status != 0 || r->result[0] || r->result[8];
    r->result[8] = r->result[0] ? 7 : 0;
    r->lost = r->result[0];
    r->phase = STATUS_WRITE;
    if (status_write(r->input_status, ptr(r->result)) == BLOCKED)
        return wait(r);
    status_drop_write(r->input_status);
    return write_trailers(r);
}
static uint32_t
finish(Request *r)
{
    drop_write(r->tx);
    r->tx = 0;
    if (!r->input_trailers)
        return write_trailers(r);
    drop_read(r->rx);
    r->rx = 0;
    if (r->lost)
        return write_status(r, 1);
    r->phase = TRAILERS_READ;
    uint32_t status = trailers_read(r->input_trailers, ptr(r->result));
    return status == BLOCKED ? wait(r) : write_status(r, status);
}
static uint32_t
write_body(Request *r, uint32_t status)
{
    r->offset += status >> 4;
    if ((status & 15) == 1) {
        r->lost = 1;
        return finish(r);
    }
    if (r->offset == r->size) {
        if (!r->rx || r->eof)
            return finish(r);
        r->phase = READ;
        return yield();
    }
    uint32_t size = r->size - r->offset;
    r->phase = WRITE;
    status = stream_write(r->tx, ptr(r->bytes + r->offset),
                          size > CHUNK ? CHUNK : size);
    if (status == BLOCKED)
        return wait(r);
    if ((status & 15) == 1) {
        r->lost = 1;
        return finish(r);
    }
    r->offset += status >> 4;
    return yield();
}
static uint32_t
read_body(Request *r, uint32_t status)
{
    if (status >> 4) {
        r->eof = (status & 15) == 1;
        r->bytes = r->upload;
        r->size = status >> 4;
        r->offset = 0;
        return write_body(r, 0);
    }
    if ((status & 15) == 1)
        return finish(r);
    r->phase = READ;
    status = stream_read(r->rx, ptr(r->upload), sizeof(r->upload));
    if (status == BLOCKED)
        return wait(r);
    return read_body(r, status);
}
EXPORT("[async-lift]" HANDLER "#handle")
uint32_t
handle(uint32_t request)
{
    unsigned slot = 0;
    while (slot < CONNECTIONS && active[slot].set)
        ++slot;
    if (slot == CONNECTIONS)
        __builtin_trap();
    Request *r = &active[slot];
    r->set = set_new();
    r->rx = r->offset = r->size = r->eof = r->lost = 0;
    r->input_status = r->input_trailers = 0;
    r->bytes = NULL;
    context_set(slot);

    uint32_t method[3], route[3], fields = fields_new(), status = 404;
    get_method(request, ptr(method));
    get_path(request, ptr(route));
    const char *type = "application/octet-stream";
    uint32_t type_len = 24;
    const uint8_t *text = (const uint8_t *)(uintptr_t)route[1];
    if (method[0] == 0 && route[0] && same(text, route[2], "/data.json", 10)) {
        status = 200;
        type = "application/json";
        type_len = 16;
        r->bytes = json_body;
        r->size = sizeof(json_body);
    }
    else if (method[0] == 0 && route[0]
             && same(text, route[2], "/image.png", 10)) {
        status = 200;
        type = "image/png";
        type_len = 9;
        r->bytes = png_body;
        r->size = sizeof(png_body);
    }
    else if (method[0] == 2 && route[0] && same(text, route[2], "/upload", 7)) {
        status = 200;
        type = "image/png";
        type_len = 9;
        uint64_t done = status_new();
        uint32_t ends[2];
        r->input_status = (uint32_t)(done >> 32);
        join(r->input_status, r->set);
        consume_body(request, (uint32_t)done, ptr(ends));
        r->rx = ends[0];
        r->input_trailers = ends[1];
        join(r->rx, r->set);
        join(r->input_trailers, r->set);
    }
    if (!r->rx)
        drop_request(request);
    append(fields, ptr("content-type"), 12, ptr(type), type_len,
           ptr(r->result));
    if (r->result[0])
        __builtin_trap();
    uint64_t body = stream_new(), trailers = trailers_new();
    uint32_t response[2];
    r->tx = (uint32_t)(body >> 32);
    r->output_trailers = (uint32_t)(trailers >> 32);
    join(r->tx, r->set);
    join(r->output_trailers, r->set);
    response_new(fields, 1, (uint32_t)body, (uint32_t)trailers, ptr(response));
    status_drop_read(response[1]);
    if (set_status(response[0], status))
        __builtin_trap();
    task_return(0, response[0], 0, 0, 0, 0, 0, 0);
    return r->rx ? read_body(r, 0) : write_body(r, 0);
}
EXPORT("[callback][async-lift]" HANDLER "#handle")
uint32_t
handle_event(uint32_t code, uint32_t end, uint32_t status)
{
    uint32_t slot = context_get();
    if (slot >= CONNECTIONS)
        __builtin_trap();
    Request *r = &active[slot];
    if (r->phase == READ && (!code || (code == 2 && end == r->rx)))
        return read_body(r, status);
    if (r->phase == WRITE && (!code || (code == 3 && end == r->tx)))
        return write_body(r, status);
    if (r->phase == TRAILERS_READ && code == 4 && end == r->input_trailers)
        return write_status(r, status);
    if (r->phase == STATUS_WRITE && code == 5 && end == r->input_status) {
        status_drop_write(r->input_status);
        return write_trailers(r);
    }
    if (r->phase == TRAILERS_WRITE && code == 5 && end == r->output_trailers) {
        trailers_drop_write(r->output_trailers);
        set_drop(r->set);
        r->set = 0;
        return 0;
    }
    __builtin_trap();
}
EXPORT("native-loop")
uint32_t
native_loop(uint32_t iterations)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < iterations; ++i)
        total += context_get();
    return total;
}

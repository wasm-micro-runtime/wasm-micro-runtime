/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "http.h"
#include "wasi_http_io.h"
#include <stdio.h>
#include <string.h>

#define HTTP_MAX_BODY (64 * 1024U)

bool
http_open(http_server *server, unsigned slot)
{
    if (slot >= HTTP_CONNECTIONS || server->connections[slot].live)
        return false;
    http_connection *c = &server->connections[slot];
    memset(c, 0, sizeof(*c));
    c->live = true;
    return true;
}
static bool
protocol_error(http_connection *c, unsigned code)
{
    const char *phrase = code == 431   ? "Request Header Fields Too Large"
                         : code == 413 ? "Content Too Large"
                                       : "Bad Request";
    int n = snprintf(
        (char *)c->output, sizeof(c->output),
        "HTTP/1.1 %u %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
        code, phrase);
    if (n < 0 || (size_t)n >= sizeof(c->output))
        return false;
    c->length = (size_t)n;
    c->done = true;
    return true;
}
static const uint8_t *
crlf(const uint8_t *start, const uint8_t *end)
{
    while (end - start >= 2) {
        if (start[0] == '\r' && start[1] == '\n')
            return start;
        ++start;
    }
    return NULL;
}
static bool
name_is(const uint8_t *p, size_t len, const char *name)
{
    if (len != strlen(name))
        return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned ch = p[i];
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        if (ch != (unsigned char)name[i])
            return false;
    }
    return true;
}
static unsigned
parse_request(http_connection *c, http_message *m, http_fields *fields,
              unsigned header)
{
    const uint8_t *start = c->request_bytes, *end = start + header;
    const uint8_t *line = crlf(start, end);
    const uint8_t *space =
        line ? memchr(start, ' ', (size_t)(line - start)) : NULL;
    if (!space)
        return 400;
    const uint8_t *path = space + 1;
    const uint8_t *version = memchr(path, ' ', (size_t)(line - path));
    int method = wasi_http_method(start, (uint32_t)(space - start));
    if (method < 0 || !version || version == path || line - version != 9
        || memcmp(version, " HTTP/1.1", 9)
        || !wasi_http_path(path, (uint32_t)(version - path)))
        return 400;
    m->method = (uint8_t)method;
    c->head = method == 1;
    if (method == 9)
        memcpy(m->method_other, start, (size_t)(space - start));
    memcpy(m->path, path, (size_t)(version - path));
    m->scheme = 1;
    bool length = false;
    for (start = line + 2; start != end - 2; start = line + 2) {
        line = crlf(start, end);
        const uint8_t *colon =
            line ? memchr(start, ':', (size_t)(line - start)) : NULL;
        if (!colon || colon == start)
            return 400;
        const uint8_t *value = colon + 1, *last = line;
        while (value < last && (*value == ' ' || *value == '\t'))
            ++value;
        while (last > value && (last[-1] == ' ' || last[-1] == '\t'))
            --last;
        uint32_t names = (uint32_t)(colon - start),
                 values = (uint32_t)(last - value);
        if (name_is(start, names, "content-length")) {
            if (length || !values)
                return 400;
            length = true;
            for (const uint8_t *p = value; p < last; ++p) {
                if (*p < '0' || *p > '9')
                    return 400;
                c->body_left = c->body_left * 10 + *p - '0';
                if (c->body_left > HTTP_MAX_BODY)
                    return 413;
            }
        }
        else if (name_is(start, names, "transfer-encoding"))
            return 400; /* Reject ambiguous framing and chunked uploads. */
        else if (name_is(start, names, "host")) {
            if (m->authority[0] || memchr(value, '@', values)
                || !wasi_http_authority(value, values))
                return 400;
            memcpy(m->authority, value, values);
        }
        else if (!name_is(start, names, "connection")) {
            int err = wasi_http_append(fields, start, names, value, values);
            if (err != -1)
                return err == 3 ? 431 : 400;
        }
    }
    if (!m->authority[0])
        return 400;
    return 0;
}
static bool
start_request(http_server *server, http_connection *c, unsigned header)
{
    wasi_http *http = &server->http;
    uint32_t request = wasi_http_new(http, HTTP_REQUEST, false);
    uint32_t fields = wasi_http_new(http, HTTP_FIELDS, true);
    if (!request || !fields)
        return false;
    http_message *m =
        &wasi_http_get(http, request, HTTP_REQUEST, false)->as.message;
    m->fields = fields;
    m->inbound = true;
    unsigned code = parse_request(
        c, m, &wasi_http_get(http, fields, HTTP_FIELDS, true)->as.fields,
        header);
    if (!code && c->used - header > c->body_left)
        code = 400;
    if (code)
        return wasi_http_drop(http, request, HTTP_REQUEST, false)
               && protocol_error(c, code);
    c->used -= header;
    memmove(c->request_bytes, c->request_bytes + header, c->used);
    c->parsed = true;
    return http_start_task(http, &c->task, request, &c->input, &c->trailers_tx);
}
uint32_t
http_input_capacity(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (c->done || c->disconnected || c->saw_eof)
        return 0;
    if (!c->parsed)
        return (uint32_t)sizeof(c->request_bytes) - c->used;
    if (c->used || !c->input)
        return 0;
    uint32_t cap = p3_native_capacity(&server->vm.async, c->input);
    return cap < c->body_left ? cap : c->body_left;
}
bool
http_input(http_server *server, unsigned slot, uint8_t *data, uint32_t size)
{
    http_connection *c = &server->connections[slot];
    if (!c->live || size > http_input_capacity(server, slot))
        return false;
    if (c->parsed) {
        uint32_t result;
        if (!p3_native_transfer(&server->vm.async, c->input, data, size,
                                &result)
            || result != size << 4)
            return false;
        c->body_left -= size;
        return true;
    }
    uint32_t old = c->used;
    memcpy(c->request_bytes + old, data, size);
    c->used += size;
    for (unsigned i = old > 3 ? old - 3 : 0; i + 4 <= c->used; ++i)
        if (!memcmp(c->request_bytes + i, "\r\n\r\n", 4))
            return start_request(server, c, i + 4);
    return c->used < sizeof(c->request_bytes) || protocol_error(c, 431);
}
bool
http_end_input(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    c->saw_eof = true;
    return http_close_end(&server->http, &c->input);
}
bool
http_disconnect(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (c->disconnected)
        return true;
    c->disconnected = true;
    c->sent = c->length;
    if (!http_end_input(server, slot))
        return false;
    if (!c->parsed)
        c->done = true;
    else if (!c->task.returned && !c->task.exited)
        p3_task_request_cancel(&c->task);
    return true;
}
static bool
wire_fields(http_connection *c, const http_fields *f)
{
    for (unsigned i = 0; i < f->count; ++i) {
        http_field h = f->field[i];
        const uint8_t *name = f->bytes + h.offset;
        if (name_is(name, h.name_size, "content-length"))
            continue;
        size_t count = (size_t)h.name_size + h.value_size + 4;
        if (count > sizeof(c->output) - c->length)
            return false;
        uint8_t *dst = c->output + c->length;
        memcpy(dst, name, h.name_size);
        memcpy(dst + h.name_size, ": ", 2);
        memcpy(dst + h.name_size + 2, name + h.name_size, h.value_size);
        memcpy(dst + count - 2, "\r\n", 2);
        c->length += count;
    }
    return true;
}
static bool
wire_headers(http_server *server, http_connection *c, const http_message *m)
{
    http_resource *headers =
        wasi_http_get(&server->http, m->fields, HTTP_FIELDS, true);
    if (!headers)
        return false;
    int n = snprintf((char *)c->output, sizeof(c->output), "HTTP/1.1 %u\r\n",
                     m->status);
    if (n < 0 || (size_t)n >= sizeof(c->output))
        return false;
    c->length = (size_t)n;
    bool body =
        !c->head && m->status >= 200 && m->status != 204 && m->status != 304;
    c->chunked = m->body && body;
    if (!wire_fields(c, &headers->as.fields))
        return false;
    const char *framing = c->chunked ? "Transfer-Encoding: chunked\r\n"
                          : body     ? "Content-Length: 0\r\n"
                                     : "";
    n = snprintf((char *)c->output + c->length, sizeof(c->output) - c->length,
                 "%sConnection: close\r\n\r\n", framing);
    if (n < 0 || (size_t)n >= sizeof(c->output) - c->length)
        return false;
    c->length += (size_t)n;
    return true;
}
static bool
wire_body(http_server *server, http_connection *c, http_message *m)
{
    if (!m->body)
        return true;
    if (c->disconnected || !c->chunked)
        return http_close_end(&server->http, &m->body);
    if (c->sent != c->length)
        return true;
    uint32_t status;
    if (!p3_native_transfer(&server->vm.async, m->body, c->output + 6,
                            HTTP_CHUNK - 8, &status))
        return false;
    if (status == P3_BLOCKED)
        return true;
    if ((status & 15U) == 1 && !http_close_end(&server->http, &m->body))
        return false;
    uint32_t count = status >> 4;
    if (!count)
        return true;
    char prefix[7];
    if (snprintf(prefix, sizeof(prefix), "%04x\r\n", (unsigned)count) != 6)
        return false;
    memcpy(c->output, prefix, 6);
    memcpy(c->output + 6 + count, "\r\n", 2);
    c->sent = 0;
    c->length = 8 + count;
    return true;
}
static bool
wire_finish(http_server *server, http_connection *c, http_message *m)
{
    if (c->chunked && !c->terminal_chunk && !c->disconnected) {
        memcpy(c->output, "0\r\n", 3);
        c->sent = 0;
        c->length = 3;
        if (c->response_trailers) {
            http_resource *r = wasi_http_get(
                &server->http, c->response_trailers, HTTP_FIELDS, true);
            if (!r || !wire_fields(c, &r->as.fields))
                return false;
        }
        memcpy(c->output + c->length, "\r\n", 2);
        c->length += 2;
        c->terminal_chunk = true;
        return true;
    }
    if (!http_send_future(&server->http, &m->sent, c->disconnected))
        return false;
    c->wire_done = true;
    return true;
}
bool
http_progress(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (c->done || !c->parsed)
        return true;
    for (unsigned i = 0; i < 64; ++i) {
        p3_context *cx = &server->vm.async;
        if (c->used && c->input) {
            uint32_t cap = p3_native_capacity(cx, c->input);
            uint32_t take = c->used < cap ? c->used : cap;
            if (take) {
                uint32_t result;
                if (!p3_native_transfer(cx, c->input, c->request_bytes, take,
                                        &result)
                    || result != take << 4)
                    return false;
                c->used -= take;
                c->body_left -= take;
                memmove(c->request_bytes, c->request_bytes + take, c->used);
            }
        }
        if (!c->body_left && !http_close_end(&server->http, &c->input))
            return false;
        if ((!c->input || c->disconnected)
            && !http_send_future(&server->http, &c->trailers_tx, c->disconnected))
            return false;
        if (!c->response && c->task.returned && !c->wire_done) {
            c->response = c->task.http_result;
            http_resource *r =
                wasi_http_get(&server->http, c->response, HTTP_RESPONSE, true);
            if (!r
                || (!c->disconnected
                    && !wire_headers(server, c, &r->as.message)))
                return false;
        }
        if (c->response) {
            http_resource *r =
                wasi_http_get(&server->http, c->response, HTTP_RESPONSE, true);
            if (!r)
                return false;
            http_message *m = &r->as.message;
            if (!wire_body(server, c, m)
                || !http_receive_trailers(&server->http, &m->trailers, &c->response_trailers))
                return false;
            if (!m->body && !m->trailers && c->sent == c->length
                && !wire_finish(server, c, m))
                return false;
        }
        if (!wasi_http_sweep(&server->http))
            return false;
        if (!p3_task_ready(&c->task))
            break;
        if (!p3_task_step(&c->task))
            return false;
        ++server->callbacks;
    }
    if (c->task.exited && (c->wire_done || c->disconnected)) {
        if (!http_close_end(&server->http, &c->input)
            || !http_send_future(&server->http, &c->trailers_tx, c->disconnected))
            return false;
        if (c->response_trailers
            && !wasi_http_drop(&server->http, c->response_trailers, HTTP_FIELDS,
                               true))
            return false;
        if (c->response) {
            if (!wasi_http_drop(&server->http, c->response, HTTP_RESPONSE,
                                true))
                return false;
            c->response = 0;
        }
        if (!wasi_http_sweep(&server->http))
            return false;
        c->done = true;
    }
    return true;
}
bool
http_finished(const http_connection *c)
{
    return c->done && c->sent == c->length;
}
bool
http_reap(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (!c->live || !http_finished(c))
        return false;
    if (c->task.cancelled)
        ++server->cancelled;
    else if (c->disconnected)
        ++server->disconnected;
    else
        ++server->completed;
    c->live = false;
    return true;
}
bool
http_native_loop(http_server *server, int32_t iterations)
{
    if (server->vm.current || iterations <= 0)
        return false;
    p3_task task = { .vm = &server->vm, .context = 1 };
    server->vm.current = &task;
    int32_t result;
    bool has_result;
    bool ok = p3_vm_call(&server->vm, "native-loop", &iterations, 1, &result,
                         &has_result);
    server->vm.current = NULL;
    return ok && has_result && result == iterations;
}

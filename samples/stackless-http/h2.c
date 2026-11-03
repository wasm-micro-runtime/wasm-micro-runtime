/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "h2.h"
#include "wasi_http_io.h"
#include <stdlib.h>
#include <string.h>

typedef union allocation {
#if defined(_MSC_VER) && defined(_WIN64)
    __declspec(align(16)) size_t size;
#elif defined(_MSC_VER)
    __declspec(align(8)) size_t size;
#else
    max_align_t alignment;
    size_t size;
#endif
} allocation;

static void *
allocate(size_t size, void *user)
{
    h2_connection *c = user;
    h2_server *s = c->server;
    size_t available = H2_CONNECTION_HEAP - c->heap;
    if (size > available || sizeof(allocation) > available - size)
        return NULL;
    size += sizeof(allocation);
    allocation *p = malloc(size);
    if (!p)
        return NULL;
    p->size = size;
    c->heap += size;
    s->heap += size;
    if (s->heap > s->heap_peak)
        s->heap_peak = s->heap;
    return p + 1;
}
static void
release(void *ptr, void *user)
{
    if (!ptr)
        return;
    h2_connection *c = user;
    allocation *p = (allocation *)ptr - 1;
    c->heap -= p->size;
    c->server->heap -= p->size;
    free(p);
}
static void *
allocate_zero(size_t n, size_t size, void *user)
{
    if (size && n > SIZE_MAX / size)
        return NULL;
    void *p = allocate(n * size, user);
    if (p)
        memset(p, 0, n * size);
    return p;
}
static void *
resize(void *ptr, size_t size, void *user)
{
    if (!ptr)
        return allocate(size, user);
    h2_connection *c = user;
    h2_server *s = c->server;
    allocation *p = (allocation *)ptr - 1;
    size_t old = p->size, available = H2_CONNECTION_HEAP - (c->heap - old);
    if (size > available || sizeof(*p) > available - size)
        return NULL;
    size += sizeof(*p);
    p = realloc(p, size);
    if (!p)
        return NULL;
    p->size = size;
    c->heap = c->heap - old + size;
    s->heap = s->heap - old + size;
    if (s->heap > s->heap_peak)
        s->heap_peak = s->heap;
    return p + 1;
}
static h2_stream *
stream(nghttp2_session *session, int32_t id)
{
    return nghttp2_session_get_stream_user_data(session, id);
}
static int
reject(nghttp2_session *session, int32_t id, uint32_t code)
{
    if (nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, id, code))
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
}
static int
begin_headers(nghttp2_session *session, const nghttp2_frame *frame, void *user)
{
    h2_connection *c = user;
    h2_server *s = c->server;
    int32_t id = frame->hd.stream_id;
    if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
        ++s->protocol_errors;
        return reject(session, id, NGHTTP2_PROTOCOL_ERROR);
    }
    h2_stream *slot = NULL;
    for (unsigned i = 0; i < H2_STREAMS; ++i)
        if (!s->streams[i].connection) {
            slot = &s->streams[i];
            break;
        }
    if (!slot) {
        return reject(session, id, NGHTTP2_REFUSED_STREAM);
    }
    memset(slot, 0, sizeof(*slot));
    slot->connection = c;
    slot->id = id;
    if (++s->live > s->peak)
        s->peak = s->live;
    slot->request = wasi_http_new(&s->http, HTTP_REQUEST, false);
    if (!slot->request
        || nghttp2_session_set_stream_user_data(session, id, slot))
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    http_resource *r =
        wasi_http_get(&s->http, slot->request, HTTP_REQUEST, false);
    r->as.message.inbound = true;
    r->as.message.fields = wasi_http_new(&s->http, HTTP_FIELDS, true);
    return r->as.message.fields ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}
static bool
eq(const uint8_t *name, size_t size, const char *text)
{
    return size == strlen(text) && !memcmp(name, text, size);
}
static int
header(nghttp2_session *session, const nghttp2_frame *frame,
       const uint8_t *name, size_t names, const uint8_t *value, size_t values,
       uint8_t flags, void *user)
{
    (void)flags;
    h2_connection *c = user;
    h2_stream *x = stream(session, frame->hd.stream_id);
    if (!x)
        return 0;
    wasi_http *http = &c->server->http;
    http_resource *r = wasi_http_get(http, x->request, HTTP_REQUEST, false);
    if (!r)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    http_message *m = &r->as.message;
    if (eq(name, names, ":method")) {
        int method = wasi_http_method(value, (uint32_t)values);
        if (method < 0 || method == 5)
            goto invalid;
        x->method = true;
        m->method = (uint8_t)method;
        if (method == 9)
            memcpy(m->method_other, value, values);
    }
    else if (eq(name, names, ":scheme")) {
        if (!eq(value, values, "http"))
            goto invalid;
        x->scheme = true;
        m->scheme = 1;
    }
    else if (eq(name, names, ":authority") || eq(name, names, "host")) {
        if (m->authority[0] || memchr(value, '@', values)
            || !wasi_http_authority(value, (uint32_t)values))
            goto invalid;
        memcpy(m->authority, value, values);
    }
    else if (eq(name, names, ":path")) {
        if (!values || !wasi_http_path(value, (uint32_t)values))
            goto invalid;
        memcpy(m->path, value, values);
    }
    else {
        http_resource *fields =
            wasi_http_get(http, m->fields, HTTP_FIELDS, true);
        if (!fields)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        if (wasi_http_append(&fields->as.fields, name, (uint32_t)names, value,
                             (uint32_t)values)
            != -1)
            goto invalid;
    }
    return 0;
invalid:
    ++c->server->protocol_errors;
    return reject(session, frame->hd.stream_id, NGHTTP2_PROTOCOL_ERROR);
}
static int
frame_received(nghttp2_session *session, const nghttp2_frame *frame, void *user)
{
    h2_connection *c = user;
    h2_stream *x = stream(session, frame->hd.stream_id);
    if (!x)
        return 0;
    if (frame->hd.type == NGHTTP2_HEADERS
        && frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
        wasi_http *http = &c->server->http;
        http_resource *r = wasi_http_get(http, x->request, HTTP_REQUEST, false);
        if (!r)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        if (!x->method || !x->scheme || !r->as.message.path[0]
            || !r->as.message.authority[0]) {
            ++c->server->protocol_errors;
            return nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, x->id,
                                             NGHTTP2_PROTOCOL_ERROR);
        }
        x->send_body = r->as.message.method != 1;
        if (!http_start_task(http, &x->task, x->request, &x->input,
                             &x->input_trailers))
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        x->request = 0; /* Ownership passed to the guest. */
    }
    if ((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA)
        && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM))
        x->input_end = true;
    return 0;
}
static int
data_received(nghttp2_session *session, uint8_t flags, int32_t id,
              const uint8_t *data, size_t len, void *user)
{
    (void)flags;
    h2_connection *c = user;
    h2_stream *x = stream(session, id);
    if (!len)
        return 0;
    if (!x || x->aborted)
        return nghttp2_session_consume_connection(session, len);
    if (len > H2_WINDOW - (size_t)x->queued
        || len > H2_MAX_BODY - x->received) {
        ++c->server->protocol_errors;
        x->aborted = true;
        if (nghttp2_session_consume_connection(session, len))
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        return nghttp2_submit_rst_stream(session, NGHTTP2_FLAG_NONE, id,
                                         NGHTTP2_ENHANCE_YOUR_CALM);
    }
    size_t write_at = (x->read_at + x->queued) % H2_WINDOW;
    size_t first = len < H2_WINDOW - write_at ? len : H2_WINDOW - write_at;
    memcpy(x->input_bytes + write_at, data, first);
    memcpy(x->input_bytes, data + first, len - first);
    x->queued += (uint16_t)len;
    x->received += (uint32_t)len;
    if (x->queued > c->server->input_peak)
        c->server->input_peak = x->queued;
    return 0;
}
static int
stream_closed(nghttp2_session *session, int32_t id, uint32_t error, void *user)
{
    (void)user;
    h2_stream *x = stream(session, id);
    if (x) {
        x->closed = true;
        x->aborted |= error != NGHTTP2_NO_ERROR;
    }
    return 0;
}
static int
frame_sent(nghttp2_session *session, const nghttp2_frame *frame, void *user)
{
    h2_connection *c = user;
    if (frame->hd.type == NGHTTP2_RST_STREAM
        && frame->rst_stream.error_code == NGHTTP2_REFUSED_STREAM)
        ++c->server->refused;
    h2_stream *x = stream(session, frame->hd.stream_id);
    if (x
        && (frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA)
        && (frame->hd.flags & NGHTTP2_FLAG_END_STREAM))
        x->local_end = true;
    return 0;
}
static size_t
wire_fields(const http_fields *f, nghttp2_nv *nv)
{
    size_t count = 0;
    for (unsigned i = 0; i < f->count; ++i) {
        http_field field = f->field[i];
        uint8_t *name = (uint8_t *)f->bytes + field.offset;
        /* The transport determines framing, as in the HTTP/1.1 fixture. */
        if (eq(name, field.name_size, "content-length"))
            continue;
        nv[count++] =
            (nghttp2_nv){ name, name + field.name_size, field.name_size,
                          field.value_size, NGHTTP2_NV_FLAG_NONE };
    }
    return count;
}
static http_message *
response(h2_server *s, h2_stream *x)
{
    http_resource *r =
        wasi_http_get(&s->http, x->response, HTTP_RESPONSE, true);
    return r ? &r->as.message : NULL;
}
static nghttp2_ssize
read_body(nghttp2_session *session, int32_t id, uint8_t *buf, size_t length,
          uint32_t *flags, nghttp2_data_source *source, void *user)
{
    h2_connection *c = user;
    h2_stream *x = source->ptr;
    h2_server *s = c->server;
    http_message *m = response(s, x);
    if (!m)
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    if (m->body) {
        uint32_t status;
        uint32_t take = length < 1024 ? (uint32_t)length : 1024;
        if (!p3_native_transfer(&s->vm.async, m->body, buf, take, &status))
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        if (status != P3_BLOCKED) {
            if ((status & 15) == 1 && !http_close_end(&s->http, &m->body))
                return NGHTTP2_ERR_CALLBACK_FAILURE;
            if (status >> 4)
                return (nghttp2_ssize)(status >> 4);
        }
    }
    if (m->body || m->trailers) {
        x->deferred = true;
        return NGHTTP2_ERR_DEFERRED;
    }
    *flags |= NGHTTP2_DATA_FLAG_EOF;
    if (x->output_trailers && x->send_body) {
        http_resource *r =
            wasi_http_get(&s->http, x->output_trailers, HTTP_FIELDS, true);
        if (!r)
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        nghttp2_nv nv[HTTP_FIELDS_MAX];
        size_t n = wire_fields(&r->as.fields, nv);
        if (n) {
            if (nghttp2_submit_trailer(session, id, nv, n))
                return NGHTTP2_ERR_CALLBACK_FAILURE;
            *flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
        }
    }
    return 0;
}
static bool
submit_response(h2_server *s, h2_stream *x, http_message *m)
{
    http_resource *r = wasi_http_get(&s->http, m->fields, HTTP_FIELDS, true);
    if (!r)
        return false;
    uint8_t status[] = { (uint8_t)('0' + m->status / 100),
                         (uint8_t)('0' + m->status / 10 % 10),
                         (uint8_t)('0' + m->status % 10) };
    nghttp2_nv nv[1 + HTTP_FIELDS_MAX] = { { (uint8_t *)":status", status, 7,
                                             sizeof(status),
                                             NGHTTP2_NV_FLAG_NONE } };
    size_t n = 1 + wire_fields(&r->as.fields, nv + 1);
    x->send_body &= m->status >= 200 && m->status != 204 && m->status != 304;
    nghttp2_data_provider2 provider = { .source.ptr = x,
                                        .read_callback = read_body };
    return !nghttp2_submit_response2(x->connection->session, x->id, nv, n,
                                     &provider);
}
bool
h2_open(h2_server *s, unsigned slot)
{
    h2_connection *c = &s->connections[slot];
    /* A closed socket's index cannot be reused until its detached tasks reap.
     */
    for (unsigned i = 0; i < H2_STREAMS; ++i)
        if (s->streams[i].connection == c)
            return false;
    *c = (h2_connection){ .server = s };
    nghttp2_session_callbacks *cb;
    nghttp2_option *opt;
    if (nghttp2_session_callbacks_new(&cb))
        return false;
    if (nghttp2_option_new(&opt)) {
        nghttp2_session_callbacks_del(cb);
        return false;
    }
    nghttp2_session_callbacks_set_on_begin_headers_callback(cb, begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(cb, header);
    nghttp2_session_callbacks_set_on_frame_recv_callback(cb, frame_received);
    nghttp2_session_callbacks_set_on_frame_send_callback(cb, frame_sent);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cb,
                                                              data_received);
    nghttp2_session_callbacks_set_on_stream_close_callback(cb, stream_closed);
    nghttp2_option_set_no_auto_window_update(opt, 1);
    nghttp2_option_set_max_deflate_dynamic_table_size(opt, 0);
    nghttp2_option_set_max_send_header_block_length(opt, 2 * 1024);
    nghttp2_option_set_max_continuations(opt, 2);
    nghttp2_option_set_max_settings(opt, 16);
    nghttp2_option_set_max_outbound_ack(opt, 16);
    nghttp2_mem mem = { c, allocate, release, allocate_zero, resize };
    int err = nghttp2_session_server_new3(&c->session, cb, c, opt, &mem);
    nghttp2_option_del(opt);
    nghttp2_session_callbacks_del(cb);
    const nghttp2_settings_entry settings[] = {
        { NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, H2_PER_CONNECTION },
        { NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, H2_WINDOW },
        { NGHTTP2_SETTINGS_HEADER_TABLE_SIZE, 0 },
        { NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 2048 },
    };
    if (err
        || nghttp2_submit_settings(c->session, NGHTTP2_FLAG_NONE, settings,
                                   sizeof(settings) / sizeof(*settings))) {
        h2_close(c);
        return false;
    }
    return true;
}
void
h2_close(h2_connection *c)
{
    if (c->session)
        nghttp2_session_del(c->session);
    c->session = NULL;
    c->wire_size = 0;
    for (unsigned i = 0; i < H2_STREAMS; ++i) {
        h2_stream *x = &c->server->streams[i];
        if (x->connection == c) {
            x->aborted |= !(x->closed && x->local_end);
            x->closed = true;
        }
    }
}
bool
h2_receive(h2_connection *c, const uint8_t *bytes, size_t size)
{
    return nghttp2_session_mem_recv2(c->session, bytes, size)
           == (nghttp2_ssize)size;
}
bool
h2_output(h2_connection *c)
{
    if (c->failed)
        return false;
    if (c->wire_size)
        return true;
    nghttp2_ssize len = nghttp2_session_mem_send2(c->session, &c->wire);
    if (len < 0)
        return false;
    c->wire_size = (size_t)len;
    return true;
}
static bool
pump_input(h2_server *s, h2_stream *x)
{
    nghttp2_session *session = x->connection->session;
    p3_context *cx = &s->vm.async;
    while (x->queued) {
        bool discard =
            x->aborted || !x->input || !p3_http_peer_open(cx, x->input);
        uint32_t n = x->queued;
        if (!discard) {
            uint32_t cap = p3_native_capacity(cx, x->input), status;
            if (!cap)
                break;
            if (n > cap)
                n = cap;
            if (n > (uint32_t)H2_WINDOW - x->read_at)
                n = H2_WINDOW - x->read_at;
            if (!p3_native_transfer(cx, x->input, x->input_bytes + x->read_at,
                                    n, &status)
                || status != n << 4)
                return false;
        }
        x->read_at = (uint16_t)((x->read_at + n) % H2_WINDOW);
        x->queued -= (uint16_t)n;
        if (session
            && (x->closed || x->aborted
                    ? nghttp2_session_consume_connection(session, n)
                    : nghttp2_session_consume(session, x->id, n)))
            return false;
    }
    if (x->aborted || (x->input_end && !x->queued)) {
        if (!http_close_end(&s->http, &x->input)
            || !http_send_future(&s->http, &x->input_trailers, x->aborted))
            return false;
    }
    return true;
}
static bool
pump_stream(h2_server *s, h2_stream *x, bool *ready)
{
    if (x->connection->session && x->connection->failed)
        return true;
    p3_context *cx = &s->vm.async;
    if (!pump_input(s, x))
        return false;
    if (x->aborted && x->task.started && !x->task.exited && !x->task.returned)
        p3_task_request_cancel(&x->task);
    if (x->task.returned && !x->response) {
        x->response = x->task.http_result;
        http_message *m = response(s, x);
        if (!m)
            return false;
        if (!x->aborted && !submit_response(s, x, m)) {
            x->connection->failed = true;
            return true;
        }
    }
    if (x->response) {
        http_message *m = response(s, x);
        if (!m
            || ((x->aborted || !x->send_body)
                && !http_close_end(&s->http, &m->body))
            || !http_receive_trailers(&s->http, &m->trailers,
                                      &x->output_trailers))
            return false;
        if (x->deferred && !x->closed && !x->aborted
            && (m->body ? (!p3_http_peer_open(cx, m->body)
                           || p3_native_capacity(cx, m->body))
                        : !m->trailers)) {
            if (nghttp2_session_resume_data(x->connection->session, x->id))
                return false;
            x->deferred = false;
        }
        if ((x->aborted || (x->local_end && !x->connection->wire_size))
            && !http_send_future(&s->http, &m->sent, x->aborted))
            return false;
    }
    if (!wasi_http_sweep(&s->http))
        return false;
    if (p3_task_ready(&x->task)) {
        ++s->callbacks;
        if (!p3_task_step(&x->task))
            return false;
        *ready = true;
    }
    if (!x->closed || x->connection->wire_size
        || (x->task.started && !x->task.exited))
        return true;
    if ((x->request
         && !wasi_http_drop(&s->http, x->request, HTTP_REQUEST, false))
        || (x->response
            && !wasi_http_drop(&s->http, x->response, HTTP_RESPONSE, true))
        || (x->output_trailers
            && !wasi_http_drop(&s->http, x->output_trailers, HTTP_FIELDS, true))
        || !http_close_end(&s->http, &x->input)
        || !http_send_future(&s->http, &x->input_trailers, x->aborted))
        return false;
    if (x->aborted)
        ++s->reset;
    else
        ++s->completed;
    x->connection = NULL;
    --s->live;
    return wasi_http_sweep(&s->http);
}
bool
h2_pump(h2_server *s, bool *ready)
{
    *ready = false;
    for (unsigned i = 0; i < H2_STREAMS; ++i)
        if (s->streams[i].connection && !pump_stream(s, &s->streams[i], ready))
            return false;
    return true;
}

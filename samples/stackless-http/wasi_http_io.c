/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "wasi_http_io.h"

bool
http_close_end(wasi_http *http, uint32_t *end)
{
    if (*end && !p3_native_drop(&http->vm->async, *end))
        return false;
    *end = 0;
    return true;
}
bool
http_send_future(wasi_http *http, uint32_t *end, bool error)
{
    if (!*end)
        return true;
    uint8_t value[P3_HTTP_RESULT_SIZE] = { 0 };
    if (error) {
        value[0] = 1;
        value[8] = 7; /* connection-terminated */
    }
    bool complete;
    if (!p3_http_finish(&http->vm->async, *end, value, &complete))
        return false;
    if (complete)
        *end = 0;
    return true;
}
bool
http_receive_trailers(wasi_http *http, uint32_t *end, uint32_t *trailers)
{
    if (!*end)
        return true;
    uint8_t value[P3_HTTP_RESULT_SIZE];
    bool complete;
    if (!p3_http_finish(&http->vm->async, *end, value, &complete))
        return false;
    if (!complete)
        return true;
    *end = 0;
    if (value[0] || value[8] > 1) {
        http->vm->async.error = "response trailers resolved to an error";
        return false;
    }
    if (value[8]) {
        uint32_t h = 0;
        for (unsigned i = 0; i < 4; ++i)
            h |= (uint32_t)value[12 + i] << (8 * i);
        http_resource *fields = wasi_http_get(http, h, HTTP_FIELDS, false);
        if (!fields)
            return false;
        fields->native = true;
        *trailers = h;
    }
    return true;
}
bool
http_start_task(wasi_http *http, p3_task *task, uint32_t request,
                uint32_t *body, uint32_t *trailers)
{
    p3_context *cx = &http->vm->async;
    http_resource *r = wasi_http_get(http, request, HTTP_REQUEST, false);
    if (!r)
        return false;
    http_message *m = &r->as.message;
    if (!wasi_http_pair(http, false, 1, &m->body, body)
        || !wasi_http_pair(http, true, P3_HTTP_TRAILERS, &m->trailers, trailers)
        || !p3_host_end(cx, *body) || !p3_host_end(cx, m->body)
        || !p3_host_end(cx, *trailers) || !p3_host_end(cx, m->trailers))
        return false;
    p3_task_init(task, http->vm, "handle-event");
    int32_t args[] = { (int32_t)request };
    return p3_task_start(task, "handle", args, 1);
}

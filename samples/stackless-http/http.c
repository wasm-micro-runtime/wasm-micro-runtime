/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "http.h"
#include <string.h>

bool
http_open(http_server *server, unsigned slot)
{
    if (slot >= HTTP_CONNECTIONS)
        return false;
    http_connection *c = &server->connections[slot];
    if (c->live)
        return false;
    memset(c, 0, sizeof(*c));
    uint64_t ends;
    if (!p3_call(&server->vm.async, P3_NEW, false, 1, true, 0, 0, 0, &ends))
        return false;
    c->input = (uint32_t)(ends >> 32);
    if (!p3_host_end(&server->vm.async, c->input))
        return false;
    int32_t args[] = { (int32_t)slot, (int32_t)ends };
    p3_task_init(&c->task, &server->vm, "handle-event");
    c->live = true;
    return p3_task_start(&c->task, "handle", args, 2);
}
bool
http_input(http_server *server, unsigned slot, uint8_t *data, uint32_t size)
{
    uint32_t result;
    return p3_native_transfer(&server->vm.async,
                              server->connections[slot].input, data, size,
                              &result)
           && result == (size << 4);
}
static bool
close_end(p3_context *cx, uint32_t *end)
{
    if (*end && !p3_native_drop(cx, *end))
        return false;
    *end = 0;
    return true;
}
bool
http_end_input(http_server *server, unsigned slot)
{
    return close_end(&server->vm.async, &server->connections[slot].input);
}
bool
http_disconnect(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (c->disconnected)
        return true;
    c->disconnected = true;
    c->sent = c->length = 0;
    if (!c->task.returned && !c->task.exited)
        (void)p3_task_request_cancel(&c->task);
    return http_end_input(server, slot)
           && close_end(&server->vm.async, &c->task.body);
}
bool
http_progress(http_server *server, unsigned slot)
{
    p3_context *cx = &server->vm.async;
    http_connection *c = &server->connections[slot];
    /* Bound the callbacks per connection per turn. Ready connections stay
     * eligible while the transport services other sockets. */
    for (unsigned i = 0; i < 4 && c->live; ++i) {
        bool progress = false;
        if (p3_task_ready(&c->task)) {
            if (!p3_task_step(&c->task))
                return false;
            ++server->callbacks;
            progress = true;
        }
        if (c->task.body && c->sent == c->length && !c->disconnected
            && p3_native_capacity(cx, c->task.body)) {
            uint32_t result;
            if (!p3_native_transfer(cx, c->task.body, c->output,
                                    sizeof(c->output), &result)
                || (result & 15U))
                return false;
            c->sent = 0;
            c->length = result >> 4;
            progress = true;
        }
        if (c->task.completion && p3_native_capacity(cx, c->task.completion)) {
            uint32_t result;
            if (!p3_native_transfer(cx, c->task.completion, &c->outcome, 1,
                                    &result)
                || result || !close_end(cx, &c->task.completion))
                return false;
            c->done = true;
            progress = true;
        }
        if (!progress)
            break;
    }
    return true;
}
bool
http_finished(const http_connection *c)
{
    return c->live && c->task.exited && c->sent == c->length;
}
bool
http_reap(http_server *server, unsigned slot)
{
    http_connection *c = &server->connections[slot];
    if (!http_finished(c) || c->task.failed || (c->task.returned && !c->done))
        return false;
    if (!http_end_input(server, slot)
        || !close_end(&server->vm.async, &c->task.body))
        return false;
    if (c->task.cancelled)
        ++server->cancelled;
    else if (c->disconnected)
        ++server->disconnected;
    else if (c->outcome == 0)
        ++server->completed;
    else
        return false;
    c->live = false;
    return true;
}
bool
http_native_loop(http_server *server, int32_t iterations)
{
    if (server->vm.current || iterations <= 0)
        return false;
    p3_task probe = { .vm = &server->vm, .context = 1 };
    int32_t result;
    bool has_result;
    server->vm.current = &probe;
    bool ok = p3_vm_call(&server->vm, "native-loop", &iterations, 1, &result,
                         &has_result);
    server->vm.current = NULL;
    return ok && has_result && result == iterations;
}

uint32_t
http_input_capacity(http_server *server, unsigned slot)
{
    uint32_t input = server->connections[slot].input;
    return input ? p3_native_capacity(&server->vm.async, input) : 0;
}

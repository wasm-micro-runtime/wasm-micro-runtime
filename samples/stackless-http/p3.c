/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "p3.h"
#include <string.h>

enum { FREE, STREAM_RX, STREAM_TX, FUTURE_RX, FUTURE_TX, SET };
enum { IDLE, COPYING, DONE };
_Static_assert(P3_HANDLES > 2 && P3_HANDLES <= 255, "handle index width");
static bool
fail(p3_context *cx, const char *message)
{
    cx->error = message;
    return false;
}
static uint32_t
index_of(uint32_t h)
{
    return h - 1U;
}
static uint32_t
handle(p3_context *cx, p3_slot *s)
{
    return (uint32_t)(s - cx->slots) + 1U;
}
static p3_slot *
get(p3_context *cx, uint32_t h)
{
    uint32_t i = index_of(h);
    if (i >= P3_HANDLES || cx->slots[i].kind == FREE) {
        fail(cx, "invalid or stale async handle");
        return NULL;
    }
    return &cx->slots[i];
}
static p3_slot *
allocate(p3_context *cx, unsigned kind)
{
    if (cx->free_count || cx->allocated < P3_HANDLES) {
        unsigned i =
            cx->free_count ? cx->free_slots[--cx->free_count] : cx->allocated++;
        p3_slot *s = &cx->slots[i];
        memset(s, 0, sizeof(*s));
        s->kind = (uint8_t)kind;
        if (++cx->live > cx->peak)
            cx->peak = cx->live;
        return s;
    }
    fail(cx, "async handle capacity exhausted");
    return NULL;
}
static void
release(p3_context *cx, p3_slot *s)
{
    s->kind = FREE;
    cx->free_slots[cx->free_count++] = (uint16_t)(s - cx->slots);
    --cx->live;
}
static void
unqueue(p3_context *cx, p3_slot *s)
{
    if (!s->queued)
        return;
    p3_slot *set = get(cx, s->set);
    if (s->prev)
        get(cx, s->prev)->next = s->next;
    else
        set->head = s->next;
    if (s->next)
        get(cx, s->next)->prev = s->prev;
    else
        set->tail = s->prev;
    s->prev = s->next = 0;
    s->queued = 0;
}
static void
enqueue(p3_context *cx, p3_slot *s)
{
    if (!s->set || !s->pending || s->queued)
        return;
    p3_slot *set = get(cx, s->set);
    bool was_empty = set->head == 0;
    s->prev = set->tail;
    if (set->tail)
        get(cx, set->tail)->next = handle(cx, s);
    else
        set->head = handle(cx, s);
    set->tail = handle(cx, s);
    s->queued = 1;
    if (was_empty && cx->wake)
        cx->wake(cx->wake_env, s->set);
}
static void
notify(p3_context *cx, p3_slot *s)
{
    s->pending = 1;
    enqueue(cx, s);
}
static uint32_t
consume(p3_context *cx, p3_slot *s)
{
    unsigned result;
    unqueue(cx, s);
    s->pending = s->buffer = 0;
    if (s->kind >= FUTURE_RX && s->progress == 1) {
        result =
            0; /* Completion wins over a later peer drop or cancellation. */
        s->state = DONE;
    }
    else if (!s->peer) {
        result = 1;
        s->state = DONE;
    }
    else {
        result = s->cancelling ? 2 : 0;
        s->state = IDLE;
    }
    s->cancelling = 0;
    return s->kind >= FUTURE_RX ? result : result | (s->progress << 4);
}
static uint8_t *
range(p3_context *cx, uint32_t ptr, uint32_t length)
{
    size_t size = 0;
    uint8_t *data = cx->memory ? cx->memory(cx->memory_env, &size) : NULL;
    if (!data || ptr > size || length > size - ptr) {
        fail(cx, "canonical buffer out of bounds");
        return NULL;
    }
    return data + ptr;
}
void
p3_init(p3_context *cx)
{
    memset(cx, 0, sizeof(*cx));
}
bool
p3_set_ready(p3_context *cx, uint32_t h)
{
    p3_slot *s = get(cx, h);
    return s && s->kind == SET && s->head != 0;
}
bool
p3_waiter(p3_context *cx, uint32_t h, bool add)
{
    p3_slot *s = get(cx, h);
    if (!s || s->kind != SET || (add ? s->waiters == UINT16_MAX : !s->waiters))
        return fail(cx, "invalid waitable set waiter");
    if (add)
        ++s->waiters;
    else
        --s->waiters;
    return true;
}
bool
p3_take_event(p3_context *cx, uint32_t h, p3_event *event)
{
    p3_slot *set = get(cx, h);
    if (!set || set->kind != SET)
        return fail(cx, "invalid waitable set");
    memset(event, 0, sizeof(*event));
    if (set->head) {
        p3_slot *s = get(cx, set->head);
        event->index = handle(cx, s);
        event->code = s->event;
        event->payload = consume(cx, s);
    }
    return true;
}
bool
p3_host_end(p3_context *cx, uint32_t h)
{
    p3_slot *s = get(cx, h);
    if (!s || s->kind == SET)
        return false;
    s->native = 1;
    return true;
}
#ifdef P3_WASI_HTTP
bool
p3_http_element(uint8_t element)
{
    return element == P3_HTTP_STATUS || element == P3_HTTP_TRAILERS;
}
bool
p3_http_lift(p3_context *cx, uint32_t h, bool future, uint8_t element)
{
    p3_slot *s = get(cx, h);
    if (!s || s->kind != (future ? FUTURE_RX : STREAM_RX) || s->native
        || s->state != IDLE || s->set || s->element != element)
        return fail(cx, "invalid owned HTTP endpoint");
    s->native = 1;
    return true;
}
bool
p3_http_lower(p3_context *cx, uint32_t h)
{
    p3_slot *s = get(cx, h);
    if (!s || !s->native || s->state != IDLE
        || (s->kind != FUTURE_RX && s->kind != STREAM_RX))
        return fail(cx, "invalid HTTP endpoint lowering");
    s->native = 0;
    return true;
}
bool
p3_http_finish(p3_context *cx, uint32_t h, uint8_t *value, bool *complete)
{
    p3_slot *s = get(cx, h);
    if (!s || !s->native || s->state != IDLE || !p3_http_element(s->element))
        return fail(cx, "invalid HTTP future endpoint");
    *complete = false;
    if (s->peer) {
        p3_slot *peer = get(cx, s->peer);
        if (!peer || !peer->buffer || peer->pending)
            return true;
        uint8_t *guest = range(cx, peer->ptr, P3_HTTP_RESULT_SIZE);
        if (!guest)
            return false;
        if (s->kind == FUTURE_TX)
            memcpy(guest, value, P3_HTTP_RESULT_SIZE);
        else
            memcpy(value, guest, P3_HTTP_RESULT_SIZE);
        peer->progress = 1;
        peer->buffer = 0;
        cx->bytes += P3_HTTP_RESULT_SIZE;
        ++cx->transfers;
        notify(cx, peer);
    }
    else if (s->kind == FUTURE_RX)
        return fail(cx, "HTTP future producer closed before completion");
    s->state = DONE;
    *complete = true;
    return p3_native_drop(cx, h);
}
#endif
bool
p3_lift_pair(p3_context *cx, uint32_t stream, uint32_t future)
{
    p3_slot *body = get(cx, stream), *done = get(cx, future);
    if (!body || !done || body->kind != STREAM_RX || done->kind != FUTURE_RX
        || body->native || done->native || body->set || done->set
        || body->state != IDLE || done->state != IDLE || body->element != 1
        || done->element != 1)
        return fail(cx, "cannot lift response stream/future endpoints");
    body->native = done->native = 1;
    return true;
}
bool
p3_native_drop(p3_context *cx, uint32_t h)
{
    p3_slot *s = get(cx, h);
    if (!s || !s->native)
        return false;
    s->native = 0;
    uint64_t ignored;
    enum p3_op op = s->kind == STREAM_TX || s->kind == FUTURE_TX ? P3_DROP_WRITE
                                                                 : P3_DROP_READ;
    return p3_call(cx, op, s->kind >= FUTURE_RX, s->element, true, h, 0, 0,
                   &ignored);
}
uint32_t
p3_native_capacity(p3_context *cx, uint32_t h)
{
    p3_slot *s = get(cx, h), *peer;
    if (!s || !s->native || !s->peer)
        return 0;
    peer = get(cx, s->peer);
    return peer && peer->buffer && !peer->pending
               ? peer->length - peer->progress
               : 0;
}
bool
p3_native_transfer(p3_context *cx, uint32_t h, uint8_t *bytes, uint32_t length,
                   uint32_t *result)
{
    p3_slot *s = get(cx, h), *peer;
    if (!s || !s->native || s->state != IDLE || s->element != 1)
        return fail(cx, "invalid native endpoint");
    if (s->kind >= FUTURE_RX && length != 1)
        return fail(cx, "native future requires one byte");
    *result = P3_BLOCKED;
    if (!s->peer) {
        *result = 1;
        s->state = DONE;
        return true;
    }
    peer = get(cx, s->peer);
    if (!peer->buffer || peer->pending)
        return true;
    uint32_t n = peer->length - peer->progress;
    if (n > length)
        n = length;
    uint8_t *guest = range(cx, peer->ptr, n);
    if (!guest || (!bytes && n))
        return false;
    bool write = s->kind == STREAM_TX || s->kind == FUTURE_TX;
    if (n)
        memcpy(write ? guest : bytes, write ? bytes : guest, n);
    peer->ptr += n;
    peer->progress += n;
    peer->buffer = 0;
    cx->bytes += n;
    ++cx->transfers;
    notify(cx, peer);
    if (s->kind >= FUTURE_RX) {
        *result = 0;
        s->state = DONE;
    }
    else
        *result = n << 4;
    return true;
}
bool
p3_call(p3_context *cx, enum p3_op op, bool future, uint8_t element,
        bool async_, uint32_t a, uint32_t b, uint32_t c, uint64_t *out)
{
    p3_slot *s, *other;
    unsigned kind = future ? FUTURE_RX : STREAM_RX;
    cx->error = NULL;
    *out = 0;
    bool http = false;
#ifdef P3_WASI_HTTP
    http = future && p3_http_element(element);
#endif
    if (!http && element != 0 && element != 1 && element != 2 && element != 4)
        return fail(cx, "unsupported canonical element type");
    if (op == P3_NEW || op == P3_SET_NEW) {
        s = allocate(cx, op == P3_SET_NEW ? SET : kind);
        if (!s)
            return false;
        if (op == P3_SET_NEW) {
            *out = handle(cx, s);
            return true;
        }
        other = allocate(cx, kind + 1);
        if (!other) {
            release(cx, s);
            return false;
        }
        s->element = other->element = element;
        s->peer = handle(cx, other);
        other->peer = handle(cx, s);
        s->event = future ? 4 : 2;
        other->event = future ? 5 : 3;
        *out = handle(cx, s) | ((uint64_t)handle(cx, other) << 32);
        return true;
    }
    s = get(cx, a);
    if (!s)
        return false;
    if (s->native)
        return fail(cx, "native endpoint is not guest-owned");
    if (op == P3_JOIN) {
        if (s->kind == SET)
            return fail(cx, "waitable.join requires an endpoint");
        other = b ? get(cx, b) : NULL;
        if (b && (!other || other->kind != SET))
            return fail(cx, "invalid waitable set");
        unqueue(cx, s);
        s->set = b;
        enqueue(cx, s);
        return true;
    }
    if (op == P3_SET_DROP || op == P3_SET_POLL || op == P3_SET_WAIT) {
        if (s->kind != SET)
            return fail(cx, "invalid waitable set");
        if (op == P3_SET_DROP) {
            if (s->waiters)
                return fail(cx, "cannot drop waitable set with a waiting task");
            for (unsigned i = 0; i < P3_HANDLES; ++i)
                if (cx->slots[i].kind && cx->slots[i].set == a) {
                    unqueue(cx, &cx->slots[i]);
                    cx->slots[i].set = 0;
                }
            release(cx, s);
            return true;
        }
        if (op == P3_SET_WAIT && !s->head)
            return fail(cx, "wait would block outside a stackless callback");
        if (b & 3U)
            return fail(cx, "unaligned canonical event pointer");
        uint8_t *dst = range(cx, b, 8);
        if (!dst)
            return false;
        uint32_t event[2] = { 0, 0 };
        if (s->head) {
            other = get(cx, s->head);
            event[0] = handle(cx, other);
            *out = other->event;
            event[1] = consume(cx, other);
        }
        /* Canonical memory is little endian on every native architecture. */
        for (unsigned i = 0; i < 8; ++i)
            dst[i] = (uint8_t)(event[i / 4] >> ((i % 4) * 8));
        return true;
    }
    bool write = op == P3_WRITE || op == P3_CANCEL_WRITE || op == P3_DROP_WRITE;
    if (s->kind != kind + (unsigned)write || s->element != element)
        return fail(cx, "canonical endpoint type mismatch");
    if (op == P3_DROP_READ || op == P3_DROP_WRITE) {
        if (s->state == COPYING)
            return fail(cx, "cannot drop a transferring endpoint");
        if (s->kind == FUTURE_TX && s->state != DONE)
            return fail(
                cx,
                "cannot drop future write end without first writing a value");
        unqueue(cx, s);
        if (s->peer) {
            other = get(cx, s->peer);
            other->peer = 0;
            if (other->state != DONE && !other->pending)
                notify(cx, other);
        }
        release(cx, s);
        return true;
    }
    if (!async_ && s->set)
        return fail(cx, "synchronous transfer in a waitable set");
    if (op == P3_CANCEL_READ || op == P3_CANCEL_WRITE) {
        if (s->state != COPYING)
            return fail(cx, "cancel requires a transferring endpoint");
        s->cancelling = 1;
        if (!s->pending)
            notify(cx, s);
        *out = consume(cx, s);
        return true;
    }
    if (s->state != IDLE)
        return fail(cx, "endpoint is not idle");
    if (future)
        c = 1;
    if (c > P3_MAX_TRANSFER)
        return fail(cx, "canonical transfer exceeds maximum length");
    unsigned width = http ? 40 : element == 4 ? 4 : element != 0;
    unsigned align = http ? 8 : width;
    if (align && (b & (align - 1)))
        return fail(cx, "unaligned canonical buffer");
    if (width && !range(cx, b, c * width))
        return false;
    s->ptr = b;
    s->length = c;
    s->progress = 0;
    s->state = COPYING;
    if (!s->peer) {
        notify(cx, s);
    }
    else {
        other = get(cx, s->peer);
        if (!other->buffer)
            s->buffer = 1;
        else if (element > 1)
            return fail(cx, "cannot read from and write to intra-component "
                            "future/stream with non-numeric payload");
        else if (c && other->length > other->progress) {
            uint32_t n = c < other->length - other->progress
                             ? c
                             : other->length - other->progress;
            if (element) {
                uint8_t *mine = range(cx, b, n);
                uint8_t *theirs = range(cx, other->ptr, n);
                if (!mine || !theirs)
                    return false;
                memmove(write ? theirs : mine, write ? mine : theirs, n);
            }
            s->progress = n;
            other->progress += n;
            other->ptr += n * element;
            cx->bytes += (uint64_t)n * element;
            ++cx->transfers;
            notify(cx, s);
            notify(cx, other);
            if (other->length == other->progress)
                other->buffer = 0;
        }
        else if (c || (!write && other->length == other->progress)) {
            notify(cx, other);
            other->buffer = 0;
            s->buffer = 1;
        }
        else
            notify(cx, s);
    }
    if (s->pending)
        *out = consume(cx, s);
    else if (async_)
        *out = P3_BLOCKED;
    else
        return fail(cx,
                    "blocking canonical transfer requires stackful execution");
    return true;
}

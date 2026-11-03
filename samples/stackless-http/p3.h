/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_P3_H
#define STACKLESS_P3_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Consumer-owned, bounded storage. No runtime-private types or scheduler. */
#ifndef P3_HANDLES
#define P3_HANDLES 64
#endif
#ifdef P3_WASI_HTTP
#define P3_HTTP_STATUS 40
#define P3_HTTP_TRAILERS 41
#define P3_HTTP_RESULT_SIZE 40
bool
p3_http_element(uint8_t element);
#endif
#define P3_BLOCKED UINT32_MAX
#define P3_MAX_TRANSFER 0x0fffffffU
enum p3_op {
    P3_NEW,
    P3_READ,
    P3_WRITE,
    P3_CANCEL_READ,
    P3_CANCEL_WRITE,
    P3_DROP_READ,
    P3_DROP_WRITE,
    P3_SET_NEW,
    P3_SET_POLL,
    P3_SET_DROP,
    P3_JOIN,
    P3_SET_WAIT,
    P3_TASK_RETURN,
    P3_TASK_CANCEL,
    P3_CONTEXT_GET,
    P3_CONTEXT_SET
};
typedef struct p3_slot {
    uint32_t peer, set, prev, next, head, tail;
    uint32_t ptr, length, progress;
    uint8_t kind, state, element, event, queued, pending, buffer, cancelling;
    uint8_t native;
    uint16_t waiters;
} p3_slot;
typedef struct p3_context {
    p3_slot slots[P3_HANDLES];
    uint8_t *(*memory)(void *env, size_t *size);
    void (*wake)(void *env, uint32_t set);
    void *memory_env, *wake_env;
    const char *error;
    uint32_t live, peak, transfers;
    uint16_t free_slots[P3_HANDLES], free_count, allocated;
    uint64_t bytes;
} p3_context;
void
p3_init(p3_context *cx);
/* Element 0 is unit and 1 is u8. Tags 2/4 identify bool/char, for which an
 * intra-instance transfer traps. Future selects single-value semantics.
 * All pointers are guest offsets, resolved afresh at actual transfer time.
 * Returns false on a canonical trap; no guest-supplied C pointers are used. */
bool
p3_call(p3_context *cx, enum p3_op op, bool future, uint8_t element,
        bool async_, uint32_t a, uint32_t b, uint32_t c, uint64_t *result);
bool
p3_set_ready(p3_context *cx, uint32_t set);
bool
p3_waiter(p3_context *cx, uint32_t set, bool add);
typedef struct p3_event {
    uint32_t code, index, payload;
} p3_event;
bool
p3_take_event(p3_context *cx, uint32_t set, p3_event *event);
bool
p3_host_end(p3_context *cx, uint32_t end);
#ifdef P3_WASI_HTTP
bool
p3_http_lift(p3_context *cx, uint32_t h, bool future, uint8_t element);
bool
p3_http_peer_open(p3_context *cx, uint32_t h);
bool
p3_http_lower(p3_context *cx, uint32_t h);
bool
p3_http_finish(p3_context *cx, uint32_t h, uint8_t *value, bool *complete);
#endif
bool
p3_lift_pair(p3_context *cx, uint32_t stream, uint32_t future);
/* Native buffers are borrowed only for this call and never retained. */
bool
p3_native_transfer(p3_context *cx, uint32_t end, uint8_t *bytes,
                   uint32_t length, uint32_t *result);
uint32_t
p3_native_capacity(p3_context *cx, uint32_t end);
bool
p3_native_drop(p3_context *cx, uint32_t end);
#endif

/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#ifndef WASM_EXT_DEBUG_H
#define WASM_EXT_DEBUG_H
#include "wasm_c_api.h"
#include "wasm_ext.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental v1, separately opt-in. No pointers, locals, or guest memory in
 * snapshots. Instances are store-local ordinals (WAMR: 0), joined by the
 * consumer to a content-hashed original module. Offsets are ORIGINAL Wasm
 * file offsets, never interpreter IR offsets. Zero flags means unmapped.
 * Frames are youngest first; tail-eliminated callers are absent. */
#define WASM_EXT_DEBUG_FRAMES 32
#define WASM_EXT_DEBUG_MAPPED 1
#define WASM_EXT_DEBUG_TRUNCATED 1
#define WASM_EXT_DEBUG_VERSION 1
typedef struct wasm_ext_debug_frame_t {
    uint32_t instance, function, offset, flags;
} wasm_ext_debug_frame_t;
typedef struct wasm_ext_debug_snapshot_t {
    uint32_t version, size, count, flags;
    wasm_ext_debug_frame_t frames[WASM_EXT_DEBUG_FRAMES];
} wasm_ext_debug_snapshot_t;
enum {
    WASM_EXT_DEBUG_START = 1,
    WASM_EXT_DEBUG_HOST = 2,
    WASM_EXT_DEBUG_PENDING = 3,
    WASM_EXT_DEBUG_RESUME = 4,
    WASM_EXT_DEBUG_TRAP = 5,
    WASM_EXT_DEBUG_CANCEL = 6,
    WASM_EXT_DEBUG_DONE = 7,
    WASM_EXT_DEBUG_ASYNC_ERROR = 8
};
/* Called on the owner thread at safe boundaries, never from a signal handler.
 * The snapshot is borrowed only for this callback. Copy it to consumer-owned
 * bounded storage. Do not reenter ANY runtime/call API, throw, or longjmp.
 * Register once before the first resume; env outlives call. NULL disables.
 * No allocations during capture; source-map storage is allocated at load.
 * Native crashes inside an import see HOST, while suspended tasks see PENDING.
 * RESUME retains the await-site, DONE clears frames; TRAP captures the fault.
 * Missing debug information must remain visibly unmapped in the symbolizer. */
typedef void (*wasm_ext_debug_observer_t)(void *, uint32_t,
                                          const wasm_ext_debug_snapshot_t *);
WASM_API_EXTERN bool
wasm_ext_call_observe(wasm_ext_call_t *, wasm_ext_debug_observer_t, void *);
#ifdef __cplusplus
}
#endif
#endif

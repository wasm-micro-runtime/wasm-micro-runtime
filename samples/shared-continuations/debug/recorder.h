/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
#ifndef DIAGNOSTIC_RECORDER_H
#define DIAGNOSTIC_RECORDER_H
#include "wasm_ext_debug.h"
#ifdef _MSC_VER
#include <intrin.h>
typedef volatile long debug_atomic;
static inline uint32_t
debug_load(debug_atomic *value)
{
    return (uint32_t)_InterlockedCompareExchange(value, 0, 0);
}
static inline void
debug_store(debug_atomic *value, uint32_t bank)
{
    _InterlockedExchange(value, (long)bank);
}
#else
#include <stdatomic.h>
typedef _Atomic uint32_t debug_atomic;
static inline uint32_t
debug_load(debug_atomic *value)
{
    return atomic_load_explicit(value, memory_order_acquire);
}
static inline void
debug_store(debug_atomic *value, uint32_t bank)
{
    atomic_store_explicit(value, bank, memory_order_release);
}
#endif
/* Consumer policy, not a runtime ABI. Single writer; readers stop the writer
 * (debuggers) or inspect the mapping after process death. Never read a live
 * mutating bank without synchronization. No process pointers in this format. */
#define DEBUG_ANCESTORS 4
#define DEBUG_MAGIC 0x57444247u
typedef struct debug_segment {
    uint32_t task, parent, event, cause, error_flags;
    wasm_ext_debug_snapshot_t guest;
    char error[128];
} debug_segment;
typedef struct debug_task {
    debug_segment current;
    uint32_t ancestors, truncated, causes, causes_truncated;
    debug_segment ancestry[DEBUG_ANCESTORS];
    debug_segment failures[DEBUG_ANCESTORS];
} debug_task;
typedef struct debug_record {
    uint32_t magic, version, size;
    debug_atomic active;
    uint32_t pid;
    unsigned char manifest_sha256[32], reserved[12];
    debug_task banks[2];
} debug_record;
extern debug_record *debug_published;
/* Generation IDs are never reused; zero and wrap are rejected. Ancestors are
 * copied at spawn, so parent destruction/slot reuse cannot retarget a trace. */
bool
debug_task_start(debug_task *, uint32_t *, const debug_task *);
bool
debug_task_error(debug_task *, uint32_t, uint32_t, const char *);
bool
debug_task_cause(debug_task *, const debug_task *);
void
debug_observe(void *, uint32_t, const wasm_ext_debug_snapshot_t *);
bool
debug_open(const char *);
void
debug_close(void);
#endif

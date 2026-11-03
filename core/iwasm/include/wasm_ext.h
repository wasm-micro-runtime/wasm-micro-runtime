/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
/* Experimental shared continuation API. Include the runtime's wasm.h first.
 * Not part of the standard WebAssembly C API. Thread-confined, numeric v1.
 * Stores, functions, instances and callback environments outlive invocations.
 * No nested calls, reentrant invocation APIs, unwinding/longjmp across
 * callbacks, or deleting a store while an invocation is live. Input/completion
 * vectors are borrowed only for the call. Results are written to caller-owned
 * storage, never replaced. Invalid completion leaves pending. cancel/delete are
 * terminal and notify the pending host exactly once.
 * cancel/delete discard the core continuation; they do not run guest cleanup
 * and are not Component Model cooperative task cancellation. PENDING is a host
 * boundary only, not a canonical WAIT/YIELD event or a fairness guarantee.
 * Notification does not establish external I/O quiescence. The adapter must
 * fence late completions and retain buffer/store exclusion until external work
 * no longer accesses guest memory; it must never use a deleted call pointer.
 */
#ifndef WASM_EXT_H
#define WASM_EXT_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct wasm_ext_call_t wasm_ext_call_t;
typedef int32_t wasm_ext_status_t;
enum {
    WASM_EXT_ERROR = -1,
    WASM_EXT_DONE = 0,
    WASM_EXT_PENDING = 1,
    WASM_EXT_CANCELLED = 2
};
typedef wasm_ext_status_t (*wasm_ext_callback_t)(void *, const wasm_val_vec_t *,
                                                 wasm_val_vec_t *,
                                                 wasm_trap_t **);
typedef void (*wasm_ext_cancel_t)(void *);
/* Callback env stays caller-owned. Only numeric signatures, <=1 result in v1.
 */
WASM_API_EXTERN wasm_func_t *
wasm_ext_func_new(wasm_store_t *, const wasm_functype_t *, wasm_ext_callback_t,
                  void *, wasm_ext_cancel_t);
WASM_API_EXTERN wasm_ext_call_t *
wasm_ext_call_new(const wasm_func_t *, const wasm_val_vec_t *);
/* NULL completion starts execution; pending requires a correctly typed
 * completion. out_trap receives an owned trap or NULL. ERROR may be recoverable
 * while pending. DONE/ERROR/CANCELLED never replay guest instructions or
 * callbacks. */
WASM_API_EXTERN wasm_ext_status_t
wasm_ext_call_resume(wasm_ext_call_t *, const wasm_val_vec_t *completion,
                     wasm_val_vec_t *results, wasm_trap_t **out_trap);
WASM_API_EXTERN void
wasm_ext_call_cancel(wasm_ext_call_t *);
WASM_API_EXTERN void
wasm_ext_call_delete(wasm_ext_call_t *);
#ifdef __cplusplus
}
#endif
#endif

/* Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
/* Included only when diagnostics are explicitly enabled. */
static void
ext_debug_event(wasm_ext_call_t *call, uint32 event, bool capture)
{
    wasm_ext_debug_snapshot_t *out = &call->snapshot;
    WASMModuleInstance *instance =
        (WASMModuleInstance *)call->exec_env->module_inst;
    WASMInterpFrame *frame;
    if (!call->observer)
        return;
    if (!out->version || capture || event == WASM_EXT_DEBUG_START
        || event == WASM_EXT_DEBUG_DONE) {
        memset(out, 0, sizeof(*out));
        out->version = WASM_EXT_DEBUG_VERSION;
        out->size = sizeof(*out);
    }
    if (capture) {
        for (frame = wasm_exec_env_get_cur_frame(call->exec_env); frame;
             frame = frame->prev_frame) {
            WASMFunctionInstance *fi = frame->function;
            wasm_ext_debug_frame_t *record;
            WASMFunction *func;
            uintptr_t ip, base;
            if (!fi || fi->is_import_func)
                continue;
            if (out->count == WASM_EXT_DEBUG_FRAMES) {
                out->flags |= WASM_EXT_DEBUG_TRUNCATED;
                break;
            }
            record = &out->frames[out->count++];
            record->function = (uint32)(fi - instance->e->functions);
            func = fi->u.func;
            ip = (uintptr_t)frame->ip;
            base = (uintptr_t)func->code_compiled;
            /* Saved IP is after the call operands or faulting opcode. */
            if (func->debug_offsets && ip > base
                && ip - base <= func->code_compiled_size) {
                record->offset = func->debug_offsets[ip - base - 1];
                if (record->offset)
                    record->flags = WASM_EXT_DEBUG_MAPPED;
            }
        }
    }
    call->observer(call->debug_env, event, out);
}
void
wasm_c_api_ext_debug_host(WASMExecEnv *env)
{
    if (ext_current && ext_current->exec_env == env)
        ext_debug_event(ext_current, WASM_EXT_DEBUG_HOST, true);
}
void
wasm_c_api_ext_debug_trap(WASMExecEnv *env)
{
    if (ext_current && ext_current->exec_env == env)
        ext_debug_event(ext_current, WASM_EXT_DEBUG_TRAP, true);
}
bool
wasm_ext_call_observe(wasm_ext_call_t *call, wasm_ext_debug_observer_t observer,
                      void *env)
{
    if (!call || call->started || call->running
        || call->function->store->ext_active != call)
        return false;
    call->observer = observer;
    call->debug_env = env;
    return true;
}

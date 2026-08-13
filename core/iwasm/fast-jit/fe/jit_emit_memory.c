/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "jit_emit_memory.h"
#include "jit_emit_exception.h"
#include "jit_emit_function.h"
#include "../jit_frontend.h"
#include "../jit_codegen.h"
#include "../../interpreter/wasm_runtime.h"
#include "../../common/wasm_memory.h"
#include "jit_emit_control.h"

#ifndef OS_ENABLE_HW_BOUND_CHECK
static JitReg
get_memory_boundary(JitCompContext *cc, uint32 mem_idx, uint32 bytes)
{
    JitReg memory_boundary;

    switch (bytes) {
        case 1:
        {
            memory_boundary =
                get_mem_bound_check_1byte_reg(cc->jit_frame, mem_idx);
            break;
        }
        case 2:
        {
            memory_boundary =
                get_mem_bound_check_2bytes_reg(cc->jit_frame, mem_idx);
            break;
        }
        case 4:
        {
            memory_boundary =
                get_mem_bound_check_4bytes_reg(cc->jit_frame, mem_idx);
            break;
        }
        case 8:
        {
            memory_boundary =
                get_mem_bound_check_8bytes_reg(cc->jit_frame, mem_idx);
            break;
        }
        case 16:
        {
            memory_boundary =
                get_mem_bound_check_16bytes_reg(cc->jit_frame, mem_idx);
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }

    return memory_boundary;
fail:
    return 0;
}
#endif

#if WASM_ENABLE_SHARED_MEMORY != 0
static void
set_load_or_store_atomic(JitInsn *load_or_store_inst)
{
    load_or_store_inst->flags_u8 |= 0x1;
}
#endif

#if WASM_ENABLE_SHARED_HEAP != 0
/*
 * Resolve a memory app offset to an absolute native address.
 * Handles shared-heap translation (Memory64 unsupported with fast-jit).
 *
 * Returns the native address on success, or 0 on OOB. Returned via rax as
 * an I64/PTR (same path as os_time_thread_cputime_us); avoids exec_env
 * jit_cache, which codegen also uses as scratch.
 *
 * max_valid_start: maximum valid linear-memory start offset for this access
 * (same meaning as mem_bound_check_Nbytes); pass UINT64_MAX to skip the soft
 * linear bounds check (e.g. when OS_ENABLE_HW_BOUND_CHECK).
 */
static uintptr_t
fast_jit_resolve_mem_addr(WASMModuleInstance *module_inst, uint64 app_offset,
                          uint32 bytes, uint64 max_valid_start)
{
    WASMMemoryInstance *memory;

    if (!module_inst)
        return 0;

    if (is_app_addr_in_shared_heap((WASMModuleInstanceCommon *)module_inst,
                                   false, app_offset, (uint64)bytes)) {
        return (uintptr_t)(module_inst->e->shared_heap_base_addr_adj
                           + (uintptr_t)app_offset);
    }

    memory = wasm_get_default_memory(module_inst);
    if (!memory || !memory->memory_data)
        return 0;

    /* Soft linear bounds (skipped when max_valid_start == UINT64_MAX). */
    if (max_valid_start != UINT64_MAX && app_offset > max_valid_start)
        return 0;

    return (uintptr_t)(memory->memory_data + (uintptr_t)app_offset);
}

static uint8 *
fast_jit_get_shared_heap_maddr(WASMModuleInstance *module_inst,
                               uint64 app_offset, uint32 bytes)
{
    if (is_app_addr_in_shared_heap((WASMModuleInstanceCommon *)module_inst,
                                   false, app_offset, (uint64)bytes)) {
        return module_inst->e->shared_heap_base_addr_adj
               + (uintptr_t)app_offset;
    }
    return NULL;
}

/* Resolve bulk-memory app address: shared heap (default mem only) or linear. */
static uint8 *
fast_jit_resolve_bulk_maddr(WASMModuleInstance *inst, uint32 mem_idx,
                            uint32 app_offset, uint32 len, uint64 mem_size,
                            uint8 *memory_data)
{
    uint8 *maddr;

    /* Shared heap is only valid for the default memory (mem_idx == 0). */
    if (mem_idx == 0) {
        maddr = fast_jit_get_shared_heap_maddr(inst, app_offset, len);
        if (maddr)
            return maddr;
    }

    if (mem_size < app_offset || mem_size - app_offset < len)
        return NULL;
    return memory_data + app_offset;
}

#endif /* end of WASM_ENABLE_SHARED_HEAP != 0 */

#if UINTPTR_MAX == UINT64_MAX
static JitReg
compute_mem_offset_64(JitCompContext *cc, JitReg addr, uint32 offset)
{
    JitReg long_addr, offset1;

    long_addr = jit_cc_new_reg_I64(cc);
    GEN_INSN(U32TOI64, long_addr, addr);
    offset1 = jit_cc_new_reg_I64(cc);
    GEN_INSN(ADD, offset1, NEW_CONST(I64, offset), long_addr);
    return offset1;
}
#else
static JitReg
compute_mem_offset_32(JitCompContext *cc, JitReg addr, uint32 offset)
{
    JitReg offset1;

    offset1 = jit_cc_new_reg_I32(cc);
    GEN_INSN(ADD, offset1, NEW_CONST(I32, offset), addr);

    /* if (offset1 < addr) goto EXCEPTION (unsigned wrap) */
    GEN_INSN(CMP, cc->cmp_reg, offset1, addr);
    if (!jit_emit_exception(cc, EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, JIT_OP_BLTU,
                            cc->cmp_reg, NULL)) {
        return 0;
    }
    return offset1;
}
#endif

#ifndef OS_ENABLE_HW_BOUND_CHECK
static bool
emit_linear_mem_bounds_check(JitCompContext *cc, JitReg offset1, uint32 bytes)
{
    JitReg memory_boundary, cur_page_count;
    uint32 mem_idx = 0;

    /* shortcut if the memory size is 0 */
    if (cc->cur_wasm_module->memories != NULL
        && 0 == cc->cur_wasm_module->memories[mem_idx].init_page_count) {
        cur_page_count = get_cur_page_count_reg(cc->jit_frame, mem_idx);
        GEN_INSN(CMP, cc->cmp_reg, cur_page_count, NEW_CONST(I32, 0));
        if (!jit_emit_exception(cc, EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS,
                                JIT_OP_BEQ, cc->cmp_reg, NULL)) {
            return false;
        }
    }

    memory_boundary = get_memory_boundary(cc, mem_idx, bytes);
    if (!memory_boundary)
        return false;

    GEN_INSN(CMP, cc->cmp_reg, offset1, memory_boundary);
    if (!jit_emit_exception(cc, EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, JIT_OP_BGTU,
                            cc->cmp_reg, NULL)) {
        return false;
    }
    return true;
}
#endif

/*
 * Resolve app address to an absolute native address (maddr).
 * Call sites use LDx/STx(maddr, 0). Matches AOT's maddr model and avoids
 * relying on memory_data-relative offsets across callnative (caller-saved).
 *
 * With shared heap: gate on the chain-head start (shared_heap->start_off_*),
 * not the last-used cache in e->shared_heap_start_off (that moves under
 * multi-heap thrash). Linear hot path (offset < head_start, and the detached
 * case where shared_heap is NULL → treat as UINT32/64_MAX) stays call-free.
 * Shared-region addresses take a cold callnative helper (cache hit + chain).
 * Virt regs cannot span BBs without PHIs, so offset1 / maddr / head start
 * are spilled via exec_env->jit_cache (same pattern as call_indirect).
 *
 * Callers that hold virt regs across this call (e.g. store/atomic values)
 * must spill them first — typically PUSH before seek and POP after — because
 * this splits BBs and clear_values()'s the frame.
 */
static JitReg
check_and_seek(JitCompContext *cc, JitReg addr, uint32 offset, uint32 bytes)
{
    JitReg offset1, maddr, memory_data;
#if WASM_ENABLE_SHARED_HEAP != 0
    JitReg args[4], module_inst, module_inst_extra, start_off, shared_heap;
    JitBasicBlock *bb_load_head, *bb_no_heap, *bb_cmp;
    JitBasicBlock *bb_linear, *bb_shared, *bb_join;
    JitFrame *jit_frame = cc->jit_frame;
#endif

#if UINTPTR_MAX == UINT64_MAX
    offset1 = compute_mem_offset_64(cc, addr, offset);
#else
    offset1 = compute_mem_offset_32(cc, addr, offset);
    if (!offset1)
        goto fail;
#endif

#if WASM_ENABLE_SHARED_HEAP != 0
    /* Spill offset1 to jit_cache[0] before BB splits. */
#if UINTPTR_MAX == UINT64_MAX
    GEN_INSN(STI64, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#else
    GEN_INSN(STI32, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#endif

    bb_load_head = jit_cc_new_basic_block(cc, 0);
    bb_no_heap = jit_cc_new_basic_block(cc, 0);
    bb_cmp = jit_cc_new_basic_block(cc, 0);
    bb_linear = jit_cc_new_basic_block(cc, 0);
    bb_shared = jit_cc_new_basic_block(cc, 0);
    bb_join = jit_cc_new_basic_block(cc, 0);
    if (!bb_load_head || !bb_no_heap || !bb_cmp || !bb_linear || !bb_shared
        || !bb_join)
        goto fail;

    module_inst = get_module_inst_reg(jit_frame);
    module_inst_extra = jit_cc_new_reg_ptr(cc);
    GEN_INSN(LDPTR, module_inst_extra, module_inst,
             NEW_CONST(I32, offsetof(WASMModuleInstance, e)));
    shared_heap = jit_cc_new_reg_ptr(cc);
    GEN_INSN(LDPTR, shared_heap, module_inst_extra,
             NEW_CONST(I32, offsetof(WASMModuleInstanceExtra, shared_heap)));

    gen_commit_values(jit_frame, jit_frame->lp, jit_frame->sp);
    clear_values(jit_frame);

    /* Detached → no heap: treat head start as UINT_MAX (always linear). */
    GEN_INSN(CMP, cc->cmp_reg, shared_heap, NEW_CONST(PTR, 0));
    GEN_INSN(BEQ, cc->cmp_reg, jit_basic_block_label(bb_no_heap),
             jit_basic_block_label(bb_load_head));

    /* ---- load chain-head start_off ---- */
    cc->cur_basic_block = bb_load_head;
    {
        JitReg e_ptr = jit_cc_new_reg_ptr(cc);
        JitReg heap_ptr = jit_cc_new_reg_ptr(cc);
        GEN_INSN(LDPTR, e_ptr, get_module_inst_reg(jit_frame),
                 NEW_CONST(I32, offsetof(WASMModuleInstance, e)));
        GEN_INSN(LDPTR, heap_ptr, e_ptr,
                 NEW_CONST(I32,
                           offsetof(WASMModuleInstanceExtra, shared_heap)));
#if UINTPTR_MAX == UINT64_MAX
        start_off = jit_cc_new_reg_I64(cc);
        GEN_INSN(LDI64, start_off, heap_ptr,
                 NEW_CONST(I32, offsetof(WASMSharedHeap, start_off_mem32)));
        GEN_INSN(STI64, start_off, cc->exec_env_reg,
                 NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 8));
#else
        start_off = jit_cc_new_reg_I32(cc);
        GEN_INSN(LDI32, start_off, heap_ptr,
                 NEW_CONST(I32, offsetof(WASMSharedHeap, start_off_mem32)));
        GEN_INSN(STI32, start_off, cc->exec_env_reg,
                 NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 4));
#endif
    }
    clear_values(jit_frame);
    GEN_INSN(JMP, jit_basic_block_label(bb_cmp));

    /* ---- no heap attached ---- */
    cc->cur_basic_block = bb_no_heap;
#if UINTPTR_MAX == UINT64_MAX
    GEN_INSN(STI64, NEW_CONST(I64, UINT64_MAX), cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 8));
#else
    GEN_INSN(STI32, NEW_CONST(I32, UINT32_MAX), cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 4));
#endif
    clear_values(jit_frame);
    GEN_INSN(JMP, jit_basic_block_label(bb_cmp));

    /* ---- compare offset vs head start ---- */
    cc->cur_basic_block = bb_cmp;
#if UINTPTR_MAX == UINT64_MAX
    offset1 = jit_cc_new_reg_I64(cc);
    start_off = jit_cc_new_reg_I64(cc);
    GEN_INSN(LDI64, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
    GEN_INSN(LDI64, start_off, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 8));
#else
    offset1 = jit_cc_new_reg_I32(cc);
    start_off = jit_cc_new_reg_I32(cc);
    GEN_INSN(LDI32, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
    GEN_INSN(LDI32, start_off, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache) + 4));
#endif
    /* offset1 >= head_start → shared path; else linear. */
    GEN_INSN(CMP, cc->cmp_reg, offset1, start_off);
    GEN_INSN(BGEU, cc->cmp_reg, jit_basic_block_label(bb_shared),
             jit_basic_block_label(bb_linear));

    /* ---- linear (hot path): no callnative ---- */
    cc->cur_basic_block = bb_linear;
#if UINTPTR_MAX == UINT64_MAX
    offset1 = jit_cc_new_reg_I64(cc);
    GEN_INSN(LDI64, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#else
    offset1 = jit_cc_new_reg_I32(cc);
    GEN_INSN(LDI32, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#endif
#ifndef OS_ENABLE_HW_BOUND_CHECK
    if (!emit_linear_mem_bounds_check(cc, offset1, bytes))
        goto fail;
#endif
    memory_data = get_memory_data_reg(jit_frame, 0);
    maddr = jit_cc_new_reg_ptr(cc);
    GEN_INSN(ADD, maddr, memory_data, offset1);
    GEN_INSN(STPTR, maddr, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
    clear_values(jit_frame);
    GEN_INSN(JMP, jit_basic_block_label(bb_join));

    /* ---- shared (cold): runtime helper ---- */
    cc->cur_basic_block = bb_shared;
#if UINTPTR_MAX == UINT64_MAX
    offset1 = jit_cc_new_reg_I64(cc);
    GEN_INSN(LDI64, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#else
    offset1 = jit_cc_new_reg_I32(cc);
    GEN_INSN(LDI32, offset1, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
#endif
    module_inst = get_module_inst_reg(jit_frame);
    args[0] = module_inst;
#if UINTPTR_MAX == UINT64_MAX
    args[1] = offset1;
#else
    {
        JitReg off64 = jit_cc_new_reg_I64(cc);
        GEN_INSN(U32TOI64, off64, offset1);
        args[1] = off64;
    }
#endif
    args[2] = NEW_CONST(I32, bytes);
#ifdef OS_ENABLE_HW_BOUND_CHECK
    /* Helper may still fall back to linear for edge offsets. */
    args[3] = NEW_CONST(I64, UINT64_MAX);
#else
    {
        JitReg memory_boundary = get_memory_boundary(cc, 0, bytes);
        if (!memory_boundary)
            goto fail;
#if UINTPTR_MAX == UINT64_MAX
        args[3] = memory_boundary;
#else
        {
            JitReg bound64 = jit_cc_new_reg_I64(cc);
            GEN_INSN(U32TOI64, bound64, memory_boundary);
            args[3] = bound64;
        }
#endif
    }
#endif
    maddr = jit_cc_new_reg_ptr(cc);
    if (!jit_emit_callnative(cc, (void *)fast_jit_resolve_mem_addr, maddr, args,
                             4))
        goto fail;
    GEN_INSN(CMP, cc->cmp_reg, maddr, NEW_CONST(PTR, 0));
    if (!jit_emit_exception(cc, EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, JIT_OP_BEQ,
                            cc->cmp_reg, NULL))
        goto fail;
    GEN_INSN(STPTR, maddr, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
    clear_values(jit_frame);
    GEN_INSN(JMP, jit_basic_block_label(bb_join));

    /* ---- join ---- */
    cc->cur_basic_block = bb_join;
    maddr = jit_cc_new_reg_ptr(cc);
    GEN_INSN(LDPTR, maddr, cc->exec_env_reg,
             NEW_CONST(I32, offsetof(WASMExecEnv, jit_cache)));
    /* Shared arm may have called native; drop cached memory regs. */
    clear_memory_regs(jit_frame);
#else /* else of WASM_ENABLE_SHARED_HEAP != 0 */
#ifndef OS_ENABLE_HW_BOUND_CHECK
    if (!emit_linear_mem_bounds_check(cc, offset1, bytes))
        goto fail;
#endif
    memory_data = get_memory_data_reg(cc->jit_frame, 0);
    maddr = jit_cc_new_reg_ptr(cc);
    GEN_INSN(ADD, maddr, memory_data, offset1);
#endif /* end of WASM_ENABLE_SHARED_HEAP != 0 */

    return maddr;
fail:
    return 0;
}

#if UINTPTR_MAX == UINT64_MAX
#define CHECK_ALIGNMENT(offset1)                                       \
    do {                                                               \
        JitReg align_mask = NEW_CONST(I64, ((uint64)1 << align) - 1);  \
        JitReg AND_res = jit_cc_new_reg_I64(cc);                       \
        GEN_INSN(AND, AND_res, offset1, align_mask);                   \
        GEN_INSN(CMP, cc->cmp_reg, AND_res, NEW_CONST(I64, 0));        \
        if (!jit_emit_exception(cc, EXCE_UNALIGNED_ATOMIC, JIT_OP_BNE, \
                                cc->cmp_reg, NULL))                    \
            goto fail;                                                 \
    } while (0)
#else
#define CHECK_ALIGNMENT(offset1)                                       \
    do {                                                               \
        JitReg align_mask = NEW_CONST(I32, (1 << align) - 1);          \
        JitReg AND_res = jit_cc_new_reg_I32(cc);                       \
        GEN_INSN(AND, AND_res, offset1, align_mask);                   \
        GEN_INSN(CMP, cc->cmp_reg, AND_res, NEW_CONST(I32, 0));        \
        if (!jit_emit_exception(cc, EXCE_UNALIGNED_ATOMIC, JIT_OP_BNE, \
                                cc->cmp_reg, NULL))                    \
            goto fail;                                                 \
    } while (0)
#endif

bool
jit_compile_op_i32_load(JitCompContext *cc, uint32 align, uint32 offset,
                        uint32 bytes, bool sign, bool atomic)
{
    JitReg addr, maddr, value;
    JitInsn *load_insn = NULL;
    JitReg zero = NEW_CONST(I32, 0);

    POP_I32(addr);

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic) {
        CHECK_ALIGNMENT(maddr);
    }
#endif

    value = jit_cc_new_reg_I32(cc);
    switch (bytes) {
        case 1:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI8, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU8, value, maddr, zero);
            }
            break;
        }
        case 2:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI16, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU16, value, maddr, zero);
            }
            break;
        }
        case 4:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI32, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU32, value, maddr, zero);
            }
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }

#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic && load_insn)
        set_load_or_store_atomic(load_insn);
#else
    (void)load_insn;
#endif

    PUSH_I32(value);
    return true;
fail:
    return false;
}

bool
jit_compile_op_i64_load(JitCompContext *cc, uint32 align, uint32 offset,
                        uint32 bytes, bool sign, bool atomic)
{
    JitReg addr, maddr, value;
    JitInsn *load_insn = NULL;
    JitReg zero = NEW_CONST(I32, 0);

    POP_I32(addr);

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic) {
        CHECK_ALIGNMENT(maddr);
    }
#endif

    value = jit_cc_new_reg_I64(cc);
    switch (bytes) {
        case 1:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI8, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU8, value, maddr, zero);
            }
            break;
        }
        case 2:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI16, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU16, value, maddr, zero);
            }
            break;
        }
        case 4:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI32, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU32, value, maddr, zero);
            }
            break;
        }
        case 8:
        {
            if (sign) {
                load_insn = GEN_INSN(LDI64, value, maddr, zero);
            }
            else {
                load_insn = GEN_INSN(LDU64, value, maddr, zero);
            }
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }

#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic && load_insn)
        set_load_or_store_atomic(load_insn);
#else
    (void)load_insn;
#endif

    PUSH_I64(value);
    return true;
fail:
    return false;
}

bool
jit_compile_op_f32_load(JitCompContext *cc, uint32 align, uint32 offset)
{
    JitReg addr, maddr, value;

    POP_I32(addr);

    maddr = check_and_seek(cc, addr, offset, 4);
    if (!maddr) {
        goto fail;
    }

    value = jit_cc_new_reg_F32(cc);
    GEN_INSN(LDF32, value, maddr, NEW_CONST(I32, 0));

    PUSH_F32(value);
    return true;
fail:
    return false;
}

bool
jit_compile_op_f64_load(JitCompContext *cc, uint32 align, uint32 offset)
{
    JitReg addr, maddr, value;

    POP_I32(addr);

    maddr = check_and_seek(cc, addr, offset, 8);
    if (!maddr) {
        goto fail;
    }

    value = jit_cc_new_reg_F64(cc);
    GEN_INSN(LDF64, value, maddr, NEW_CONST(I32, 0));

    PUSH_F64(value);
    return true;
fail:
    return false;
}

bool
jit_compile_op_i32_store(JitCompContext *cc, uint32 align, uint32 offset,
                         uint32 bytes, bool atomic)
{
    JitReg value, addr, maddr;
    JitInsn *store_insn = NULL;
    JitReg zero = NEW_CONST(I32, 0);

    POP_I32(value);
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    /* Survive check_and_seek BB split (see comment there). */
    PUSH_I32(value);
#endif

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_I32(value);
#endif
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic) {
        CHECK_ALIGNMENT(maddr);
    }
#endif

    switch (bytes) {
        case 1:
        {
            store_insn = GEN_INSN(STI8, value, maddr, zero);
            break;
        }
        case 2:
        {
            store_insn = GEN_INSN(STI16, value, maddr, zero);
            break;
        }
        case 4:
        {
            store_insn = GEN_INSN(STI32, value, maddr, zero);
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic && store_insn)
        set_load_or_store_atomic(store_insn);
#else
    (void)store_insn;
#endif

    return true;
fail:
    return false;
}

bool
jit_compile_op_i64_store(JitCompContext *cc, uint32 align, uint32 offset,
                         uint32 bytes, bool atomic)
{
    JitReg value, addr, maddr;
    JitInsn *store_insn = NULL;
    JitReg zero = NEW_CONST(I32, 0);

    POP_I64(value);
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    PUSH_I64(value);
#endif

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_I64(value);
#endif
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic) {
        CHECK_ALIGNMENT(maddr);
    }
#endif

    if (jit_reg_is_const(value) && bytes < 8) {
        value = NEW_CONST(I32, (int32)jit_cc_get_const_I64(cc, value));
    }

    switch (bytes) {
        case 1:
        {
            store_insn = GEN_INSN(STI8, value, maddr, zero);
            break;
        }
        case 2:
        {
            store_insn = GEN_INSN(STI16, value, maddr, zero);
            break;
        }
        case 4:
        {
            store_insn = GEN_INSN(STI32, value, maddr, zero);
            break;
        }
        case 8:
        {
            store_insn = GEN_INSN(STI64, value, maddr, zero);
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }
#if WASM_ENABLE_SHARED_MEMORY != 0
    if (atomic && store_insn)
        set_load_or_store_atomic(store_insn);
#else
    (void)store_insn;
#endif

    return true;
fail:
    return false;
}

bool
jit_compile_op_f32_store(JitCompContext *cc, uint32 align, uint32 offset)
{
    JitReg value, addr, maddr;

    POP_F32(value);
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    PUSH_F32(value);
#endif

    maddr = check_and_seek(cc, addr, offset, 4);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_F32(value);
#endif

    GEN_INSN(STF32, value, maddr, NEW_CONST(I32, 0));

    return true;
fail:
    return false;
}

bool
jit_compile_op_f64_store(JitCompContext *cc, uint32 align, uint32 offset)
{
    JitReg value, addr, maddr;

    POP_F64(value);
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    PUSH_F64(value);
#endif

    maddr = check_and_seek(cc, addr, offset, 8);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_F64(value);
#endif

    GEN_INSN(STF64, value, maddr, NEW_CONST(I32, 0));

    return true;
fail:
    return false;
}

bool
jit_compile_op_memory_size(JitCompContext *cc, uint32 mem_idx)
{
    JitReg cur_page_count;

    cur_page_count = get_cur_page_count_reg(cc->jit_frame, mem_idx);

    PUSH_I32(cur_page_count);

    return true;
fail:
    return false;
}

bool
jit_compile_op_memory_grow(JitCompContext *cc, uint32 mem_idx)
{
    JitReg grow_res, res;
    JitReg prev_page_count, inc_page_count, args[2];

    /* Get current page count as prev_page_count */
    prev_page_count = get_cur_page_count_reg(cc->jit_frame, mem_idx);

    /* Call wasm_enlarge_memory */
    POP_I32(inc_page_count);

    grow_res = jit_cc_new_reg_I32(cc);
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = inc_page_count;

    /* TODO: multi-memory wasm_enlarge_memory_with_idx() */
    if (!jit_emit_callnative(cc, wasm_enlarge_memory, grow_res, args, 2)) {
        goto fail;
    }
    /* Convert bool to uint32 */
    GEN_INSN(AND, grow_res, grow_res, NEW_CONST(I32, 0xFF));

    /* return different values according to memory.grow result */
    res = jit_cc_new_reg_I32(cc);
    GEN_INSN(CMP, cc->cmp_reg, grow_res, NEW_CONST(I32, 0));
    GEN_INSN(SELECTNE, res, cc->cmp_reg, prev_page_count,
             NEW_CONST(I32, (int32)-1));
    PUSH_I32(res);

    /* Ensure a refresh in next get memory related registers */
    clear_memory_regs(cc->jit_frame);

    return true;
fail:
    return false;
}

#if WASM_ENABLE_BULK_MEMORY != 0
static int
wasm_init_memory(WASMModuleInstance *inst, uint32 mem_idx, uint32 seg_idx,
                 uint32 len, uint32 mem_offset, uint32 data_offset)
{
    WASMMemoryInstance *mem_inst;
    WASMDataSeg *data_segment;
    uint64 mem_size;
    uint8 *mem_addr, *data_addr;
    uint32 seg_len;

    mem_inst = inst->memories[mem_idx];
    mem_size = mem_inst->cur_page_count * (uint64)mem_inst->num_bytes_per_page;

#if WASM_ENABLE_SHARED_HEAP != 0
    mem_addr = fast_jit_resolve_bulk_maddr(inst, mem_idx, mem_offset, len,
                                           mem_size, mem_inst->memory_data);
    if (!mem_addr)
        goto out_of_bounds;
#else
    /* if d + n > the length of mem.data */
    if (mem_size < mem_offset || mem_size - mem_offset < len)
        goto out_of_bounds;
    mem_addr = mem_inst->memory_data + mem_offset;
#endif

    /* if s + n > the length of data.data */
    bh_assert(seg_idx < inst->module->data_seg_count);
    if (bh_bitmap_get_bit(inst->e->common.data_dropped, seg_idx)) {
        seg_len = 0;
        data_addr = NULL;
    }
    else {
        data_segment = inst->module->data_segments[seg_idx];
        seg_len = data_segment->data_length;
        data_addr = data_segment->data + data_offset;
    }
    if (seg_len < data_offset || seg_len - data_offset < len)
        goto out_of_bounds;

    bh_memcpy_s(mem_addr, len, data_addr, len);

    return 0;
out_of_bounds:
    wasm_set_exception(inst, "out of bounds memory access");
    return -1;
}

bool
jit_compile_op_memory_init(JitCompContext *cc, uint32 mem_idx, uint32 seg_idx)
{
    JitReg len, mem_offset, data_offset, res;
    JitReg args[6] = { 0 };

    POP_I32(len);
    POP_I32(data_offset);
    POP_I32(mem_offset);

    res = jit_cc_new_reg_I32(cc);
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = NEW_CONST(I32, mem_idx);
    args[2] = NEW_CONST(I32, seg_idx);
    args[3] = len;
    args[4] = mem_offset;
    args[5] = data_offset;

    if (!jit_emit_callnative(cc, wasm_init_memory, res, args,
                             sizeof(args) / sizeof(args[0])))
        goto fail;

    GEN_INSN(CMP, cc->cmp_reg, res, NEW_CONST(I32, 0));
    if (!jit_emit_exception(cc, EXCE_ALREADY_THROWN, JIT_OP_BLTS, cc->cmp_reg,
                            NULL))
        goto fail;

    return true;
fail:
    return false;
}

static void
wasm_data_drop(WASMModuleInstance *inst, uint32 seg_idx)
{
    bh_bitmap_set_bit(inst->e->common.data_dropped, seg_idx);
}

bool
jit_compile_op_data_drop(JitCompContext *cc, uint32 seg_idx)
{
    JitReg args[2] = { 0 };

    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = NEW_CONST(I32, seg_idx);

    return jit_emit_callnative(cc, wasm_data_drop, 0, args,
                               sizeof(args) / sizeof(args[0]));
}
#endif

#if WASM_ENABLE_BULK_MEMORY_OPT != 0
static int
wasm_copy_memory(WASMModuleInstance *inst, uint32 src_mem_idx,
                 uint32 dst_mem_idx, uint32 len, uint32 src_offset,
                 uint32 dst_offset)
{
    WASMMemoryInstance *src_mem, *dst_mem;
    uint64 src_mem_size, dst_mem_size;
    uint8 *src_addr, *dst_addr;

    src_mem = inst->memories[src_mem_idx];
    dst_mem = inst->memories[dst_mem_idx];
    src_mem_size =
        src_mem->cur_page_count * (uint64)src_mem->num_bytes_per_page;
    dst_mem_size =
        dst_mem->cur_page_count * (uint64)dst_mem->num_bytes_per_page;

#if WASM_ENABLE_SHARED_HEAP != 0
    src_addr = fast_jit_resolve_bulk_maddr(inst, src_mem_idx, src_offset, len,
                                           src_mem_size, src_mem->memory_data);
    dst_addr = fast_jit_resolve_bulk_maddr(inst, dst_mem_idx, dst_offset, len,
                                           dst_mem_size, dst_mem->memory_data);
    if (!src_addr || !dst_addr)
        goto out_of_bounds;
#else
    /* if s + n > the length of mem.data */
    if (src_mem_size < src_offset || src_mem_size - src_offset < len)
        goto out_of_bounds;

    /* if d + n > the length of mem.data */
    if (dst_mem_size < dst_offset || dst_mem_size - dst_offset < len)
        goto out_of_bounds;

    src_addr = src_mem->memory_data + src_offset;
    dst_addr = dst_mem->memory_data + dst_offset;
#endif
    /* allowing the destination and source to overlap */
    bh_memmove_s(dst_addr, len, src_addr, len);

    return 0;
out_of_bounds:
    wasm_set_exception(inst, "out of bounds memory access");
    return -1;
}

bool
jit_compile_op_memory_copy(JitCompContext *cc, uint32 src_mem_idx,
                           uint32 dst_mem_idx)
{
    JitReg len, src, dst, res;
    JitReg args[6] = { 0 };

    POP_I32(len);
    POP_I32(src);
    POP_I32(dst);

    res = jit_cc_new_reg_I32(cc);
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = NEW_CONST(I32, src_mem_idx);
    args[2] = NEW_CONST(I32, dst_mem_idx);
    args[3] = len;
    args[4] = src;
    args[5] = dst;

    if (!jit_emit_callnative(cc, wasm_copy_memory, res, args,
                             sizeof(args) / sizeof(args[0])))
        goto fail;

    GEN_INSN(CMP, cc->cmp_reg, res, NEW_CONST(I32, 0));
    if (!jit_emit_exception(cc, EXCE_ALREADY_THROWN, JIT_OP_BLTS, cc->cmp_reg,
                            NULL))
        goto fail;

    return true;
fail:
    return false;
}

static int
wasm_fill_memory(WASMModuleInstance *inst, uint32 mem_idx, uint32 len,
                 uint32 val, uint32 dst)
{
    WASMMemoryInstance *mem_inst;
    uint64 mem_size;
    uint8 *dst_addr;

    mem_inst = inst->memories[mem_idx];
    mem_size = mem_inst->cur_page_count * (uint64)mem_inst->num_bytes_per_page;

#if WASM_ENABLE_SHARED_HEAP != 0
    dst_addr = fast_jit_resolve_bulk_maddr(inst, mem_idx, dst, len, mem_size,
                                           mem_inst->memory_data);
    if (!dst_addr)
        goto out_of_bounds;
#else
    if (mem_size < dst || mem_size - dst < len)
        goto out_of_bounds;

    dst_addr = mem_inst->memory_data + dst;
#endif
    memset(dst_addr, (int)(uint8)val, len);

    return 0;
out_of_bounds:
    wasm_set_exception(inst, "out of bounds memory access");
    return -1;
}

bool
jit_compile_op_memory_fill(JitCompContext *cc, uint32 mem_idx)
{
    JitReg res, len, val, dst;
    JitReg args[5] = { 0 };

    POP_I32(len);
    POP_I32(val);
    POP_I32(dst);

    res = jit_cc_new_reg_I32(cc);
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = NEW_CONST(I32, mem_idx);
    args[2] = len;
    args[3] = val;
    args[4] = dst;

    if (!jit_emit_callnative(cc, wasm_fill_memory, res, args,
                             sizeof(args) / sizeof(args[0])))
        goto fail;

    GEN_INSN(CMP, cc->cmp_reg, res, NEW_CONST(I32, 0));
    if (!jit_emit_exception(cc, EXCE_ALREADY_THROWN, JIT_OP_BLTS, cc->cmp_reg,
                            NULL))
        goto fail;

    return true;
fail:
    return false;
}
#endif

#if WASM_ENABLE_SHARED_MEMORY != 0
#define GEN_AT_RMW_INSN(op, op_type, bytes, result, value, memory_data,       \
                        offset1)                                              \
    do {                                                                      \
        switch (bytes) {                                                      \
            case 1:                                                           \
            {                                                                 \
                insn = GEN_INSN(AT_##op##U8, result, value, memory_data,      \
                                offset1);                                     \
                break;                                                        \
            }                                                                 \
            case 2:                                                           \
            {                                                                 \
                insn = GEN_INSN(AT_##op##U16, result, value, memory_data,     \
                                offset1);                                     \
                break;                                                        \
            }                                                                 \
            case 4:                                                           \
            {                                                                 \
                if (op_type == VALUE_TYPE_I32)                                \
                    insn = GEN_INSN(AT_##op##I32, result, value, memory_data, \
                                    offset1);                                 \
                else                                                          \
                    insn = GEN_INSN(AT_##op##U32, result, value, memory_data, \
                                    offset1);                                 \
                break;                                                        \
            }                                                                 \
            case 8:                                                           \
            {                                                                 \
                insn = GEN_INSN(AT_##op##I64, result, value, memory_data,     \
                                offset1);                                     \
                break;                                                        \
            }                                                                 \
            default:                                                          \
            {                                                                 \
                bh_assert(0);                                                 \
                goto fail;                                                    \
            }                                                                 \
        }                                                                     \
    } while (0)

bool
jit_compile_op_atomic_rmw(JitCompContext *cc, uint8 atomic_op, uint8 op_type,
                          uint32 align, uint32 offset, uint32 bytes)
{
    JitReg addr, maddr, zero, value, result, eax_hreg, rax_hreg, ebx_hreg,
        rbx_hreg;
    JitInsn *insn = NULL;
    bool is_i32 = op_type == VALUE_TYPE_I32;
    bool is_logical_op = atomic_op == AtomicRMWBinOpAnd
                         || atomic_op == AtomicRMWBinOpOr
                         || atomic_op == AtomicRMWBinOpXor;

    /* currently we only implement atomic rmw on x86-64 target */
#if defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64)

    /* For atomic logical binary ops, it implicitly uses rax in cmpxchg
     * instruction and implicitly uses rbx for storing temp value in the
     * generated loop */
    eax_hreg = jit_codegen_get_hreg_by_name("eax");
    rax_hreg = jit_codegen_get_hreg_by_name("rax");
    ebx_hreg = jit_codegen_get_hreg_by_name("ebx");
    rbx_hreg = jit_codegen_get_hreg_by_name("rbx");

    bh_assert(op_type == VALUE_TYPE_I32 || op_type == VALUE_TYPE_I64);
    if (op_type == VALUE_TYPE_I32) {
        POP_I32(value);
    }
    else {
        POP_I64(value);
    }
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    if (op_type == VALUE_TYPE_I32)
        PUSH_I32(value);
    else
        PUSH_I64(value);
#endif

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    if (op_type == VALUE_TYPE_I32)
        POP_I32(value);
    else
        POP_I64(value);
#endif
    CHECK_ALIGNMENT(maddr);

    zero = NEW_CONST(I32, 0);

    if (op_type == VALUE_TYPE_I32)
        result = jit_cc_new_reg_I32(cc);
    else
        result = jit_cc_new_reg_I64(cc);

    switch (atomic_op) {
        case AtomicRMWBinOpAdd:
        {
            GEN_AT_RMW_INSN(ADD, op_type, bytes, result, value, maddr, zero);
            break;
        }
        case AtomicRMWBinOpSub:
        {
            GEN_AT_RMW_INSN(SUB, op_type, bytes, result, value, maddr, zero);
            break;
        }
        case AtomicRMWBinOpAnd:
        {
            GEN_AT_RMW_INSN(AND, op_type, bytes, result, value, maddr, zero);
            break;
        }
        case AtomicRMWBinOpOr:
        {
            GEN_AT_RMW_INSN(OR, op_type, bytes, result, value, maddr, zero);
            break;
        }
        case AtomicRMWBinOpXor:
        {
            GEN_AT_RMW_INSN(XOR, op_type, bytes, result, value, maddr, zero);
            break;
        }
        case AtomicRMWBinOpXchg:
        {
            GEN_AT_RMW_INSN(XCHG, op_type, bytes, result, value, maddr, zero);
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }

    if (is_logical_op
        && (!insn
            || !jit_lock_reg_in_insn(cc, insn, is_i32 ? eax_hreg : rax_hreg)
            || !jit_lock_reg_in_insn(cc, insn, is_i32 ? ebx_hreg : rbx_hreg))) {
        jit_set_last_error(
            cc, "generate atomic logical insn or lock ra&rb hreg failed");
        goto fail;
    }

    if (op_type == VALUE_TYPE_I32)
        PUSH_I32(result);
    else
        PUSH_I64(result);

    return true;
#endif /* defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64) */

fail:
    return false;
}

bool
jit_compile_op_atomic_cmpxchg(JitCompContext *cc, uint8 op_type, uint32 align,
                              uint32 offset, uint32 bytes)
{
    JitReg addr, maddr, zero, value, expect, result;
    bool is_i32 = op_type == VALUE_TYPE_I32;
    /* currently we only implement atomic cmpxchg on x86-64 target */
#if defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64)
    /* cmpxchg will use register al/ax/eax/rax to store parameter expected
     * value, and the read result will also be stored to al/ax/eax/rax */
    JitReg eax_hreg = jit_codegen_get_hreg_by_name("eax");
    JitReg rax_hreg = jit_codegen_get_hreg_by_name("rax");
    JitInsn *insn = NULL;

    bh_assert(op_type == VALUE_TYPE_I32 || op_type == VALUE_TYPE_I64);
    if (is_i32) {
        POP_I32(value);
        POP_I32(expect);
        result = jit_cc_new_reg_I32(cc);
    }
    else {
        POP_I64(value);
        POP_I64(expect);
        result = jit_cc_new_reg_I64(cc);
    }
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    if (is_i32) {
        PUSH_I32(expect);
        PUSH_I32(value);
    }
    else {
        PUSH_I64(expect);
        PUSH_I64(value);
    }
#endif

    maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr) {
        goto fail;
    }
#if WASM_ENABLE_SHARED_HEAP != 0
    if (is_i32) {
        POP_I32(value);
        POP_I32(expect);
    }
    else {
        POP_I64(value);
        POP_I64(expect);
    }
#endif
    CHECK_ALIGNMENT(maddr);

    zero = NEW_CONST(I32, 0);

    GEN_INSN(MOV, is_i32 ? eax_hreg : rax_hreg, expect);
    switch (bytes) {
        case 1:
        {
            insn = GEN_INSN(AT_CMPXCHGU8, value, is_i32 ? eax_hreg : rax_hreg,
                            maddr, zero);
            break;
        }
        case 2:
        {
            insn = GEN_INSN(AT_CMPXCHGU16, value, is_i32 ? eax_hreg : rax_hreg,
                            maddr, zero);
            break;
        }
        case 4:
        {
            if (op_type == VALUE_TYPE_I32)
                insn =
                    GEN_INSN(AT_CMPXCHGI32, value, is_i32 ? eax_hreg : rax_hreg,
                             maddr, zero);
            else
                insn =
                    GEN_INSN(AT_CMPXCHGU32, value, is_i32 ? eax_hreg : rax_hreg,
                             maddr, zero);
            break;
        }
        case 8:
        {
            insn = GEN_INSN(AT_CMPXCHGI64, value, is_i32 ? eax_hreg : rax_hreg,
                            maddr, zero);
            break;
        }
        default:
        {
            bh_assert(0);
            goto fail;
        }
    }

    if (!insn
        || !jit_lock_reg_in_insn(cc, insn, is_i32 ? eax_hreg : rax_hreg)) {
        jit_set_last_error(cc, "generate cmpxchg insn or lock ra hreg failed");
        goto fail;
    }

    GEN_INSN(MOV, result, is_i32 ? eax_hreg : rax_hreg);

    if (is_i32)
        PUSH_I32(result);
    else
        PUSH_I64(result);

    return true;
#endif /* defined(BUILD_TARGET_X86_64) || defined(BUILD_TARGET_AMD_64) */

fail:
    return false;
}

bool
jit_compile_op_atomic_wait(JitCompContext *cc, uint8 op_type, uint32 align,
                           uint32 offset, uint32 bytes)
{
    bh_assert(op_type == VALUE_TYPE_I32 || op_type == VALUE_TYPE_I64);

    // Pop atomic.wait arguments
    JitReg timeout, expect, expect_64, addr;
    POP_I64(timeout);
    if (op_type == VALUE_TYPE_I32) {
        POP_I32(expect);
        expect_64 = jit_cc_new_reg_I64(cc);
        GEN_INSN(I32TOI64, expect_64, expect);
    }
    else {
        POP_I64(expect_64);
    }
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    PUSH_I64(timeout);
    PUSH_I64(expect_64);
#endif

    // Get referenced absolute native address
    JitReg maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr)
        goto fail;
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_I64(expect_64);
    POP_I64(timeout);
#endif
    CHECK_ALIGNMENT(maddr);

    // Prepare `wasm_runtime_atomic_wait` arguments
    JitReg res = jit_cc_new_reg_I32(cc);
    JitReg args[5] = { 0 };
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = maddr;
    args[2] = expect_64;
    args[3] = timeout;
    args[4] = NEW_CONST(I32, false);

    if (!jit_emit_callnative(cc, wasm_runtime_atomic_wait, res, args,
                             sizeof(args) / sizeof(args[0])))
        goto fail;

    // Handle return code
    GEN_INSN(CMP, cc->cmp_reg, res, NEW_CONST(I32, -1));
    if (!jit_emit_exception(cc, EXCE_ALREADY_THROWN, JIT_OP_BEQ, cc->cmp_reg,
                            NULL))
        goto fail;

    PUSH_I32(res);

#if WASM_ENABLE_THREAD_MGR != 0
    /* Insert suspend check point */
    if (!jit_check_suspend_flags(cc))
        goto fail;
#endif
    return true;
fail:
    return false;
}

bool
jit_compiler_op_atomic_notify(JitCompContext *cc, uint32 align, uint32 offset,
                              uint32 bytes)
{
    // Pop atomic.notify arguments
    JitReg notify_count, addr;
    POP_I32(notify_count);
    POP_I32(addr);
#if WASM_ENABLE_SHARED_HEAP != 0
    PUSH_I32(notify_count);
#endif

    // Get referenced absolute native address
    JitReg maddr = check_and_seek(cc, addr, offset, bytes);
    if (!maddr)
        goto fail;
#if WASM_ENABLE_SHARED_HEAP != 0
    POP_I32(notify_count);
#endif
    CHECK_ALIGNMENT(maddr);

    // Prepare `wasm_runtime_atomic_notify` arguments
    JitReg res = jit_cc_new_reg_I32(cc);
    JitReg args[3] = { 0 };
    args[0] = get_module_inst_reg(cc->jit_frame);
    args[1] = maddr;
    args[2] = notify_count;

    if (!jit_emit_callnative(cc, wasm_runtime_atomic_notify, res, args,
                             sizeof(args) / sizeof(args[0])))
        goto fail;

    // Handle return code
    GEN_INSN(CMP, cc->cmp_reg, res, NEW_CONST(I32, 0));
    if (!jit_emit_exception(cc, EXCE_ALREADY_THROWN, JIT_OP_BLTS, cc->cmp_reg,
                            NULL))
        goto fail;

    PUSH_I32(res);
    return true;
fail:
    return false;
}

bool
jit_compiler_op_atomic_fence(JitCompContext *cc)
{
    GEN_INSN(FENCE);
    return true;
}
#endif

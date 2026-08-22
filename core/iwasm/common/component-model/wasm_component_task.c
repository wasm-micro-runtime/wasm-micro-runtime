/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_component_task.h"
#include "wasm_component_resource.h"

Task *
task_create(CanonicalOptions *opts, WASMComponentInstance *inst,
            WASMComponentFuncTypeInstance *ft, Supertask *supertask)
{
    Task *task = wasm_runtime_malloc(sizeof(Task));
    if (!task)
        return NULL;

    task->state = TASK_STATE_INITIAL;
    task->opts = opts;
    task->inst = inst;
    task->ft = ft;
    task->supertask = supertask;
    task->num_borrows = 0;

    return task;
}

bool
task_return(Task *task)
{
    if (!task)
        return false;

    if (task->state == TASK_STATE_RESOLVED) {
        // Already returned - trap
        return false;
    }

    if (task->num_borrows > 0) {
        // Borrows still outstanding - trap
        return false;
    }

    task->state = TASK_STATE_RESOLVED;
    return true;
}

void
task_destroy(Task *task)
{
    if (!task)
        return;

    // For sync, just free
    // For async, would need to check threads are done or something else
    wasm_runtime_free(task);
}

Subtask *
subtask_create()
{
    Subtask *subtask = wasm_runtime_malloc(sizeof(Subtask));
    if (!subtask)
        return NULL;

    subtask->state = SUBTASK_STATE_STARTING;
    subtask->lenders = NULL;
    subtask->lenders_count = 0;
    subtask->lenders_capacity = 0;

    return subtask;
}

bool
subtask_add_lender(Subtask *subtask, WASMResourceHandle *handle)
{
    if (!subtask)
        return false;
    if (!handle)
        return false;

    // Grow array if needed
    if (subtask->lenders_count >= subtask->lenders_capacity) {
        uint32_t new_cap =
            subtask->lenders_capacity == 0 ? 4 : subtask->lenders_capacity * 2;
        WASMResourceHandle **new_arr =
            (WASMResourceHandle **)wasm_runtime_malloc(
                new_cap * sizeof(WASMResourceHandle *));
        if (!new_arr)
            return false;

        if (subtask->lenders) {
            memcpy((void *)new_arr, (const void *)subtask->lenders,
                   subtask->lenders_count * sizeof(WASMResourceHandle *));
            wasm_runtime_free((void *)subtask->lenders);
        }
        subtask->lenders = new_arr;
        subtask->lenders_capacity = new_cap;
    }

    handle->num_lends++;
    subtask->lenders[subtask->lenders_count++] = handle;
    return true;
}

void
subtask_deliver_resolve(Subtask *subtask)
{
    if (!subtask)
        return;

    // Decrement all lend counts
    for (uint32_t i = 0; i < subtask->lenders_count; i++) {
        subtask->lenders[i]->num_lends--;
    }

    // Clear the list (marks as delivered)
    if (subtask->lenders) {
        wasm_runtime_free((void *)subtask->lenders);
        subtask->lenders = NULL;
    }

    subtask->lenders_count = 0;
}

void
subtask_destroy(Subtask *subtask)
{
    if (!subtask)
        return;

    if (subtask->lenders) {
        wasm_runtime_free((void *)subtask->lenders);
    }
    wasm_runtime_free(subtask);
}

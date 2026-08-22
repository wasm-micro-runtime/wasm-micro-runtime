/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASM_COMPONENT_TASK_H
#define WASM_COMPONENT_TASK_H

#include "wasm_component_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WASMComponentInstance WASMComponentInstance;
typedef struct CanonicalOptions CanonicalOptions;
typedef struct WASMComponentFuncTypeInstance WASMComponentFuncTypeInstance;
typedef struct WASMResourceHandle WASMResourceHandle;

typedef enum TaskState {
    TASK_STATE_INITIAL = 1,
    TASK_STATE_PENDING_CANCEL = 2,   // For async
    TASK_STATE_CANCEL_DELIVERED = 3, // For async
    TASK_STATE_RESOLVED = 4,
} TaskState;

typedef enum SubtaskState {
    SUBTASK_STATE_STARTING = 0,
    SUBTASK_STATE_STARTED = 1,
    SUBTASK_STATE_RETURNED = 2,
    SUBTASK_STATE_CANCELLED_BEFORE_STARTED = 3,  // For async
    SUBTASK_STATE_CANCELLED_BEFORE_RETURNED = 4, // For async
} SubtaskState;

typedef struct Supertask {
    WASMComponentInstance *inst;
    struct Supertask *supertask;
} Supertask;

typedef struct Task {
    Supertask *supertask;
    TaskState state;
    CanonicalOptions *opts;
    WASMComponentInstance *inst;
    WASMComponentFuncTypeInstance *ft;
    uint32_t num_borrows; // Borrowed handles created in this task
} Task;

typedef struct Subtask {
    SubtaskState state;
    WASMResourceHandle **lenders; // Lender tracking
    uint32_t lenders_count;
    uint32_t lenders_capacity;
    bool cancellation_requested;
} Subtask;

// Task lifecycle
Task *
task_create(CanonicalOptions *opts, WASMComponentInstance *inst,
            WASMComponentFuncTypeInstance *ft, Supertask *supertask);
Subtask *
subtask_create();

bool
task_return(Task *task);
bool
subtask_add_lender(Subtask *subtask, WASMResourceHandle *handle);

void
task_destroy(Task *task);
void
subtask_destroy(Subtask *subtask);
void
subtask_deliver_resolve(Subtask *subtask);

#ifdef __cplusplus
}
#endif

#endif

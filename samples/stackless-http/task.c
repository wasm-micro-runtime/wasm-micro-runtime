/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "vm.h"
#include <stdio.h>
#include <string.h>

void
p3_task_init(p3_task *task, p3_vm *vm, const char *callback)
{
    memset(task, 0, sizeof(*task));
    task->vm = vm;
    task->callback = p3_vm_export(vm, callback);
}
static bool
task_call(p3_task *task, const p3_export *entry, const int32_t *args,
          size_t argc)
{
    int32_t packed;
    bool result;
    if (task->vm->current)
        return false;
    task->vm->current = task;
    bool ok = p3_vm_invoke(task->vm, entry, args, argc, &packed, &result);
    task->vm->current = NULL;
    if (!ok || !result || ((uint32_t)packed & 15U) > 2)
        goto fail;
    unsigned code = (uint32_t)packed & 15U;
    task->set = code == 2 ? (uint32_t)packed >> 4 : 0;
    task->yielded = code == 1;
    task->exited = code == 0;
    if (task->exited && !task->returned && !task->cancelled)
        goto fail;
    if (code == 2 && !p3_waiter(&task->vm->async, task->set, true))
        goto fail;
    return true;
fail:
    if (ok)
        snprintf(task->vm->error, sizeof(task->vm->error),
                 "invalid stackless callback outcome");
    task->failed = task->exited = true;
    return false;
}
bool
p3_task_start(p3_task *task, const char *start, const int32_t *args,
              size_t argc)
{
    if (task->started || task->exited)
        return false;
    task->started = true;
    if (task->cancel_requested) {
        task->cancelled = task->exited = true;
        return true;
    }
    return task_call(task, p3_vm_export(task->vm, start), args, argc);
}
bool
p3_task_ready(p3_task *task)
{
    return task->started && !task->exited
           && (task->yielded
               || (task->cancel_requested && !task->cancel_delivered)
               || (task->set && p3_set_ready(&task->vm->async, task->set)));
}
bool
p3_task_step(p3_task *task)
{
    if (!p3_task_ready(task))
        return false;
    if (task->set && !p3_waiter(&task->vm->async, task->set, false))
        return false;
    p3_event event = { 0, 0, 0 };
    if (task->cancel_requested && !task->cancel_delivered) {
        event.code = 6;
        task->cancel_delivered = true;
    }
    else if (task->set && p3_set_ready(&task->vm->async, task->set)) {
        if (!p3_take_event(&task->vm->async, task->set, &event))
            return false;
    }
    int32_t args[] = { (int32_t)event.code, (int32_t)event.index,
                       (int32_t)event.payload };
    ++task->callbacks;
    return task_call(task, task->callback, args, 3);
}
bool
p3_task_request_cancel(p3_task *task)
{
    if (task->exited || task->returned || task->cancelled
        || task->cancel_requested)
        return false;
    task->cancel_requested = true;
    return true;
}

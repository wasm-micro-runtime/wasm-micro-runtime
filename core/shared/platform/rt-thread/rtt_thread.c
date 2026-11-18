/*
 * Copyright 2024 Sony Semiconductor Solutions Corporation.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

korp_tid
os_self_thread(void)
{
    return rt_thread_self();
}

uint8 *
os_thread_get_stack_boundary(void)
{
    rt_thread_t thread = rt_thread_self();
    return thread ? (uint8 *)thread->stack_addr : NULL;
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

int
os_mutex_init(korp_mutex *mutex)
{
    return rt_mutex_init(mutex, "wamr", RT_IPC_FLAG_PRIO) == RT_EOK ? BHT_OK
                                                                    : BHT_ERROR;
}

int
os_recursive_mutex_init(korp_mutex *mutex)
{
    /* RT mutexes support recursive acquisition by their owner. */
    return os_mutex_init(mutex);
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    return rt_mutex_detach(mutex) == RT_EOK ? BHT_OK : BHT_ERROR;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    return rt_mutex_take(mutex, RT_WAITING_FOREVER) == RT_EOK ? BHT_OK
                                                              : BHT_ERROR;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    return rt_mutex_release(mutex) == RT_EOK ? BHT_OK : BHT_ERROR;
}

/* The legacy runtime_timer utility is linked with shared utils. Its optional
   timer contexts must fail explicitly rather than use a fake condition. */
int
os_cond_init(korp_cond *cond)
{
    (void)cond;
    return BHT_ERROR;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_ERROR;
}

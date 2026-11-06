/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef TEST_RTTHREAD_H
#define TEST_RTTHREAD_H
#include <stddef.h>
#include <stdint.h>
#define RT_USING_HEAP
#define RT_USING_MUTEX
#define RT_EOK 0
#define RT_IPC_FLAG_PRIO 1
#define RT_WAITING_FOREVER (-1)
#define RT_SPINLOCK_INIT { 0 }
#define RT_TIMER_FLAG_PERIODIC 1
#define RT_TICK_PER_SECOND 1000
typedef uint32_t rt_tick_t;
typedef size_t rt_size_t;
typedef intptr_t rt_base_t;
struct rt_thread {
    void *stack_addr;
};
typedef struct rt_thread *rt_thread_t;
struct rt_mutex {
    int initialized;
    int held;
};
struct rt_semaphore {
    int unused;
};
struct rt_spinlock {
    int unused;
};
struct rt_timer {
    void (*callback)(void *);
    void *argument;
};
void *
rt_malloc(rt_size_t size);
void
rt_free(void *ptr);
rt_tick_t
rt_tick_get(void);
rt_base_t
rt_spin_lock_irqsave(struct rt_spinlock *lock);
void
rt_spin_unlock_irqrestore(struct rt_spinlock *lock, rt_base_t level);
void
rt_timer_init(struct rt_timer *timer, const char *name,
              void (*callback)(void *), void *argument, rt_tick_t ticks,
              unsigned flags);
int
rt_timer_start(struct rt_timer *timer);
int
rt_timer_detach(struct rt_timer *timer);
void
rt_kputs(const char *str);
int
rt_mutex_init(struct rt_mutex *mutex, const char *name, unsigned flags);
int
rt_mutex_take(struct rt_mutex *mutex, int timeout);
int
rt_mutex_release(struct rt_mutex *mutex);
int
rt_mutex_detach(struct rt_mutex *mutex);
#endif

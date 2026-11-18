/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "platform_api_vmcore.h"

static rt_tick_t tick;
static unsigned live, attempts, releases, starts, detaches;
static unsigned spin_depth, mutex_inits, mutex_detaches;
static bool fail_allocation, fail_timer, fail_mutex, fail_mutex_take;
static struct rt_timer *active_timer;
static struct rt_mutex *active_mutex;
static size_t printed;

typedef struct {
    char kind;
    int operation;
    uintptr_t address;
    size_t size;
    bool mutex_held;
} cache_event;

static cache_event events[16];
static unsigned event_count;

void *
rt_malloc(rt_size_t size)
{
    void *p;
    attempts++;
    if (fail_allocation)
        return NULL;
    p = malloc(size);
    if (p)
        live++;
    return p;
}

void
rt_free(void *p)
{
    if (p) {
        assert(live);
        live--;
        releases++;
        free(p);
    }
}

rt_tick_t
rt_tick_get(void)
{
    return tick;
}

rt_base_t
rt_spin_lock_irqsave(struct rt_spinlock *p)
{
    rt_base_t previous = spin_depth;
    (void)p;
    spin_depth++;
    return previous;
}

void
rt_spin_unlock_irqrestore(struct rt_spinlock *p, rt_base_t previous)
{
    (void)p;
    assert(spin_depth == (unsigned)previous + 1);
    spin_depth--;
}

int
rt_mutex_init(struct rt_mutex *mutex, const char *name, unsigned flags)
{
    (void)name;
    assert(spin_depth == 0 && flags == RT_IPC_FLAG_PRIO);
    mutex_inits++;
    if (fail_mutex)
        return -1;
    assert(active_mutex == NULL);
    mutex->initialized = 1;
    mutex->held = 0;
    active_mutex = mutex;
    return RT_EOK;
}

int
rt_mutex_take(struct rt_mutex *mutex, int timeout)
{
    assert(spin_depth == 0 && timeout == RT_WAITING_FOREVER);
    assert(mutex == active_mutex && mutex->initialized && !mutex->held);
    if (fail_mutex_take)
        return -1;
    mutex->held = 1;
    return RT_EOK;
}

int
rt_mutex_release(struct rt_mutex *mutex)
{
    assert(spin_depth == 0 && mutex == active_mutex && mutex->held);
    mutex->held = 0;
    return RT_EOK;
}

int
rt_mutex_detach(struct rt_mutex *mutex)
{
    assert(spin_depth == 0 && mutex == active_mutex);
    assert(mutex->initialized && !mutex->held);
    mutex->initialized = 0;
    active_mutex = NULL;
    mutex_detaches++;
    return RT_EOK;
}

void
rt_kputs(const char *s)
{
    printed = strlen(s);
}

void
rt_timer_init(struct rt_timer *timer, const char *name, void (*cb)(void *),
              void *arg, rt_tick_t ticks, unsigned flags)
{
    (void)name;
    assert(active_timer == NULL);
    assert(ticks == RT_TICK_PER_SECOND && flags == RT_TIMER_FLAG_PERIODIC);
    timer->callback = cb;
    timer->argument = arg;
    active_timer = timer;
}

int
rt_timer_start(struct rt_timer *timer)
{
    assert(timer == active_timer);
    starts++;
    return fail_timer ? -1 : RT_EOK;
}

int
rt_timer_detach(struct rt_timer *timer)
{
    assert(timer == active_timer);
    active_timer = NULL;
    detaches++;
    return RT_EOK;
}

#ifdef RT_USING_CACHE
static void
record_cache(char kind, int operation, void *address, int size)
{
    cache_event *event;
    assert(spin_depth == 0);
    assert(size > 0 && (size_t)size <= INT_MAX);
    assert(((uintptr_t)address & (RT_CPU_CACHE_LINE_SZ - 1)) == 0);
    assert(((unsigned)size & (RT_CPU_CACHE_LINE_SZ - 1)) == 0);
    assert(event_count < sizeof(events) / sizeof(events[0]));
    event = &events[event_count++];
    event->kind = kind;
    event->operation = operation;
    event->address = (uintptr_t)address;
    event->size = (size_t)size;
    event->mutex_held = active_mutex && active_mutex->held;
}

void
rt_hw_cpu_dcache_ops(int operation, void *address, int size)
{
    assert(operation == RT_HW_CACHE_FLUSH);
    record_cache('D', operation, address, size);
}

void
rt_hw_cpu_icache_ops(int operation, void *address, int size)
{
    assert(operation == RT_HW_CACHE_INVALIDATE);
    record_cache('I', operation, address, size);
}
#endif

static void
reset_events(void)
{
    event_count = 0;
    memset(events, 0, sizeof(events));
}

static void
expect_mapping_flush(void *first, size_t first_size, void *second,
                     size_t second_size)
{
    reset_events();
    os_dcache_flush();
#ifdef RT_USING_CACHE
    void *addresses[] = { first, second };
    size_t sizes[] = { first_size, second_size };
    unsigned i, j, count = (first != NULL) + (second != NULL);
    const uintptr_t mask = RT_CPU_CACHE_LINE_SZ - 1;
    assert(event_count == count);
    for (i = 0; i < event_count; i++) {
        assert(events[i].kind == 'D'
               && events[i].operation == RT_HW_CACHE_FLUSH);
        assert(events[i].mutex_held);
    }
    for (i = 0; i < 2; i++) {
        uintptr_t start, end;
        bool found = false;
        if (!addresses[i])
            continue;
        start = (uintptr_t)addresses[i] & ~mask;
        end = ((uintptr_t)addresses[i] + sizes[i] + mask) & ~mask;
        for (j = 0; j < event_count; j++)
            if (events[j].address == start && events[j].size == end - start)
                found = true;
        assert(found);
    }
#else
    (void)first;
    (void)first_size;
    (void)second;
    (void)second_size;
    assert(event_count == 0);
#endif
}

static void
test_allocation(void)
{
    uint8_t *p, *q;
    unsigned calls;
    fail_allocation = true;
    assert(os_malloc(32) == NULL && live == 0);
    assert(os_realloc(NULL, 32) == NULL && live == 0);
    fail_allocation = false;
    p = os_malloc(37);
    assert(p && !((uintptr_t)p & 7));
    memset(p, 0x5a, 37);
    fail_allocation = true;
    assert(os_realloc(p, 99) == NULL && live == 1 && p[36] == 0x5a);
    fail_allocation = false;
    q = os_realloc(p, 11);
    assert(q && q[10] == 0x5a && live == 1);
    assert(os_realloc(q, 0) == NULL && live == 0);
    calls = attempts;
    assert(os_malloc(UINT_MAX) == NULL && attempts == calls);
}

static void
test_mappings(void)
{
    uint8_t *p, *q, *other;
    unsigned i, calls = attempts;
    assert(os_mmap(NULL, 37, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1) == NULL);
    assert(attempts == calls);
    assert(bh_platform_init() == BHT_OK);
    assert(bh_platform_init() != BHT_OK); /* Do not reset an active registry. */
    fail_allocation = true;
    assert(os_mmap(NULL, 37, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1) == NULL);
    fail_allocation = false;
    fail_mutex_take = true;
    assert(os_mmap(NULL, 37, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1) == NULL);
    assert(live == 0);
    fail_mutex_take = false;

    p = os_mmap(NULL, 37, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1);
    other = os_mmap(NULL, 77, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1);
    assert(p && other && live == 2);
    for (i = 0; i < 37; i++)
        assert(p[i] == 0);
    p[36] = 0x7a;
    expect_mapping_flush(p, 37, other, 77);
    fail_allocation = true;
    assert(os_mremap(p, 37, 97) == NULL && p[36] == 0x7a && live == 2);
    fail_allocation = false;
    expect_mapping_flush(p, 37, other, 77);

    assert(os_mprotect(p, 37, MMAP_PROT_READ | MMAP_PROT_WRITE) == BHT_OK);
    assert(os_mprotect(p + 3, 11, MMAP_PROT_READ | MMAP_PROT_WRITE) == BHT_OK);
    assert(os_mprotect(p + 37, 1, MMAP_PROT_READ | MMAP_PROT_WRITE) != BHT_OK);
    assert(os_mprotect(p, 38, MMAP_PROT_READ | MMAP_PROT_WRITE) != BHT_OK);
    assert(os_mprotect(NULL, 1, MMAP_PROT_READ | MMAP_PROT_WRITE) != BHT_OK);
    assert(os_mprotect((void *)(UINTPTR_MAX - 3), 8,
                       MMAP_PROT_READ | MMAP_PROT_WRITE)
           != BHT_OK);
    assert(os_mprotect(p, 37, MMAP_PROT_EXEC) != BHT_OK);
    assert(os_mprotect(p, 37, MMAP_PROT_READ) != BHT_OK);
    assert(os_mprotect(p, 37, 0x80) != BHT_OK);

    q = os_mremap(p, 37, 97);
    assert(q && q[36] == 0x7a && live == 2);
    for (i = 37; i < 97; i++)
        assert(q[i] == 0);
    expect_mapping_flush(q, 97, other, 77);
    os_munmap(other, 77);
    expect_mapping_flush(q, 97, NULL, 0);
    os_munmap(q, 97);
    expect_mapping_flush(NULL, 0, NULL, 0);
    assert(live == 0);
    assert(os_mmap(NULL, 8, MMAP_PROT_EXEC, 0, -1) == NULL);
    assert(os_mmap(NULL, 8, MMAP_PROT_READ, MMAP_MAP_FIXED, -1) == NULL);
    assert(os_mmap(NULL, 8, MMAP_PROT_READ, 0, 1) == NULL);
    assert(os_mmap(NULL, 8, 0x80, 0, -1) == NULL);

    assert(os_mmap(NULL, 17, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1));
    assert(os_mmap(NULL, 19, MMAP_PROT_READ | MMAP_PROT_WRITE, 0, -1));
    assert(live == 2);
    bh_platform_destroy();
    assert(live == 0 && active_mutex == NULL && active_timer == NULL);
    expect_mapping_flush(NULL, 0, NULL, 0);
}

static void
test_cache_ranges(void)
{
    reset_events();
    os_icache_flush((void *)(uintptr_t)0x1003, 33);
#ifdef RT_USING_CACHE
    assert(event_count == 2);
    assert(events[0].kind == 'D' && events[1].kind == 'I');
    assert(events[0].address == 0x1000 && events[0].size == 64);
    assert(events[1].address == 0x1000 && events[1].size == 64);
    assert(!events[0].mutex_held && !events[1].mutex_held);
#else
    assert(event_count == 0);
#endif
    reset_events();
    os_icache_flush((void *)(uintptr_t)0x1003, 0);
    os_icache_flush((void *)(UINTPTR_MAX - 7), 16);
    os_icache_flush((void *)(UINTPTR_MAX - 31), 16);
    assert(event_count == 0);

    reset_events();
    os_icache_flush((void *)(uintptr_t)0x1000, (size_t)INT_MAX + 65);
#ifdef RT_USING_CACHE
    const size_t chunk = (size_t)INT_MAX & ~(size_t)(RT_CPU_CACHE_LINE_SZ - 1);
    const size_t last = (size_t)INT_MAX + 65 - chunk;
    assert(event_count == 4);
    assert(events[0].kind == 'D' && events[1].kind == 'D');
    assert(events[2].kind == 'I' && events[3].kind == 'I');
    assert(events[0].address == 0x1000 && events[0].size == chunk);
    assert(events[1].address == 0x1000 + chunk && events[1].size == last);
    assert(events[2].address == events[0].address && events[2].size == chunk);
    assert(events[3].address == events[1].address && events[3].size == last);
#else
    assert(event_count == 0);
#endif
}

static void
test_clock_and_rollback(void)
{
    uint64 before;
    unsigned old_inits, old_starts, old_detaches, old_mutex_detaches;
    tick = UINT32_MAX - 5;
    assert(bh_platform_init() == BHT_OK);
    before = os_time_get_boot_us();
    tick = 3;
    active_timer->callback(active_timer->argument);
    assert(os_time_get_boot_us() - before == 9000);
    tick += 1000;
    assert(os_time_get_boot_us() - before == 1009000);
    bh_platform_destroy();

    old_inits = mutex_inits;
    old_starts = starts;
    old_detaches = detaches;
    old_mutex_detaches = mutex_detaches;
    fail_mutex = true;
    assert(bh_platform_init() != BHT_OK);
    assert(mutex_inits == old_inits + 1
           && mutex_detaches == old_mutex_detaches);
    assert(starts == old_starts && detaches == old_detaches);
    assert(active_mutex == NULL && active_timer == NULL);
    fail_mutex = false;
    fail_timer = true;
    assert(bh_platform_init() != BHT_OK);
    assert(starts == old_starts + 1 && detaches == old_detaches + 1);
    assert(mutex_detaches == old_mutex_detaches + 1);
    assert(active_mutex == NULL && active_timer == NULL);
    fail_timer = false;
    assert(bh_platform_init() == BHT_OK);
    bh_platform_destroy();
    old_detaches = detaches;
    old_mutex_detaches = mutex_detaches;
    bh_platform_destroy();
    assert(detaches == old_detaches && mutex_detaches == old_mutex_detaches);
}

int
main(void)
{
    char text[2048];
    test_allocation();
    test_mappings();
    test_cache_ranges();
    test_clock_and_rollback();
    assert(os_getpagesize() == 4096);
    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    assert(os_printf("%s", text) == sizeof(text) - 1 && printed == 255);
    assert(live == 0 && releases != 0 && spin_depth == 0);
#ifdef RT_USING_CACHE
    puts("WAMR_OSAL_HOST_PASS cache=on allocation-failure remap-registry "
         "cache-ranges tick-wrap init-rollback log-truncation");
#else
    puts("WAMR_OSAL_HOST_PASS cache=off allocation-failure remap-registry "
         "tick-wrap init-rollback log-truncation");
#endif
    return 0;
}

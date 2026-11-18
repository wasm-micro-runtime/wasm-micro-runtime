/*
 * Copyright (c) 2021, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#if defined(RT_USING_CACHE)                                         \
    && (RT_CPU_CACHE_LINE_SZ <= 0                                   \
        || (RT_CPU_CACHE_LINE_SZ & (RT_CPU_CACHE_LINE_SZ - 1)) != 0 \
        || RT_CPU_CACHE_LINE_SZ > INT_MAX)
#error "RT_CPU_CACHE_LINE_SZ must be a positive power of two below INT_MAX"
#endif

typedef struct {
    void *allocation;
    size_t size;
} allocation_header;

typedef struct mapping_record {
    struct mapping_record *next;
    size_t size;
} mapping_record;

#define MAPPING_HEADER_SIZE ((sizeof(mapping_record) + 7) & ~(size_t)7)

static struct rt_mutex mapping_lock;
static mapping_record *mappings;
static bool platform_initialized;
static struct rt_spinlock clock_lock = RT_SPINLOCK_INIT;
static struct rt_timer clock_timer;
static rt_tick_t last_tick;
static uint64 boot_ticks;

/* Sample once a second so callers need not query the clock every tick wrap. */
static void
update_clock(void *unused)
{
    rt_base_t level = rt_spin_lock_irqsave(&clock_lock);
    rt_tick_t now = rt_tick_get();
    (void)unused;
    boot_ticks += (rt_tick_t)(now - last_tick);
    last_tick = now;
    rt_spin_unlock_irqrestore(&clock_lock, level);
}

int
bh_platform_init(void)
{
    if (platform_initialized)
        return BHT_ERROR;
    if (rt_mutex_init(&mapping_lock, "wamrmap", RT_IPC_FLAG_PRIO) != RT_EOK)
        return BHT_ERROR;
    last_tick = rt_tick_get();
    boot_ticks = last_tick;
    rt_timer_init(&clock_timer, "wamrclk", update_clock, NULL,
                  RT_TICK_PER_SECOND, RT_TIMER_FLAG_PERIODIC);
    if (rt_timer_start(&clock_timer) != RT_EOK) {
        rt_timer_detach(&clock_timer);
        rt_mutex_detach(&mapping_lock);
        return BHT_ERROR;
    }
    platform_initialized = true;
    return BHT_OK;
}

void
bh_platform_destroy(void)
{
    mapping_record *record, *next;
    if (!platform_initialized)
        return;
    rt_timer_detach(&clock_timer);
    rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER);
    /* No VM thread may use the platform after runtime teardown begins. */
    for (record = mappings; record; record = next) {
        next = record->next;
        os_free(record);
    }
    mappings = NULL;
    rt_mutex_release(&mapping_lock);
    rt_mutex_detach(&mapping_lock);
    platform_initialized = false;
}

void *
os_malloc(unsigned size)
{
    const size_t overhead = sizeof(allocation_header) + 7;
    void *allocation;
    uintptr_t aligned;
    allocation_header *header;

    if (!size || size > UINT_MAX - overhead)
        return NULL;
    allocation = rt_malloc((rt_size_t)size + overhead);
    if (!allocation)
        return NULL;
    aligned = ((uintptr_t)allocation + overhead) & ~(uintptr_t)7;
    header = (allocation_header *)aligned - 1;
    header->allocation = allocation;
    header->size = size;
    return (void *)aligned;
}

void
os_free(void *ptr)
{
    if (ptr)
        rt_free(((allocation_header *)ptr - 1)->allocation);
}

void *
os_realloc(void *ptr, unsigned size)
{
    void *replacement;
    size_t old_size;
    if (!ptr)
        return os_malloc(size);
    if (!size) {
        os_free(ptr);
        return NULL;
    }
    replacement = os_malloc(size);
    if (!replacement)
        return NULL;
    old_size = ((allocation_header *)ptr - 1)->size;
    memcpy(replacement, ptr, old_size < size ? old_size : size);
    os_free(ptr);
    return replacement;
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return BHT_ERROR;
}

int
os_vprintf(const char *format, va_list ap)
{
    /* Caller-owned bounded buffer, without heap allocation. */
    char buffer[256];
    int length = vsnprintf(buffer, sizeof(buffer), format, ap);
    if (length < 0)
        return length;
    buffer[sizeof(buffer) - 1] = '\0';
    rt_kputs(buffer);
    return length;
}

int
os_printf(const char *format, ...)
{
    int result;
    va_list ap;
    va_start(ap, format);
    result = os_vprintf(format, ap);
    va_end(ap);
    return result;
}

uint64
os_time_get_boot_us(void)
{
    uint64 ticks;
    rt_base_t level;
    update_clock(NULL);
    level = rt_spin_lock_irqsave(&clock_lock);
    ticks = boot_ticks;
    rt_spin_unlock_irqrestore(&clock_lock, level);
    return ticks / RT_TICK_PER_SECOND * 1000000
           + ticks % RT_TICK_PER_SECOND * 1000000 / RT_TICK_PER_SECOND;
}

uint64
os_time_thread_cputime_us(void)
{
    /* No portable RT thread CPU accounting in this configuration. */
    return 0;
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    mapping_record *record;
    void *memory;
    (void)hint;
    if (!platform_initialized || !size || size > UINT_MAX - MAPPING_HEADER_SIZE
        || flags != MMAP_MAP_NONE || file != os_get_invalid_handle()
        || (prot & ~(MMAP_PROT_READ | MMAP_PROT_WRITE))) {
        errno = ENOTSUP;
        return NULL;
    }
    record = os_malloc((unsigned)(size + MAPPING_HEADER_SIZE));
    if (!record)
        return NULL;
    memory = (uint8 *)record + MAPPING_HEADER_SIZE;
    memset(memory, 0, size);
    record->size = size;
    if (rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER) != RT_EOK) {
        os_free(record);
        return NULL;
    }
    record->next = mappings;
    mappings = record;
    rt_mutex_release(&mapping_lock);
    return memory;
}

void
os_munmap(void *addr, size_t size)
{
    mapping_record **link, *record = NULL;
    (void)size;
    if (!addr || !platform_initialized)
        return;
    if (rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER) != RT_EOK)
        return;
    for (link = &mappings; *link; link = &(*link)->next) {
        if ((uint8 *)*link + MAPPING_HEADER_SIZE == addr) {
            record = *link;
            *link = record->next;
            break;
        }
    }
    rt_mutex_release(&mapping_lock);
    os_free(record);
}

void *
os_mremap(void *addr, size_t old_size, size_t new_size)
{
    mapping_record *record;
    bool valid = !addr && old_size == 0;
    void *replacement;
    if (!platform_initialized)
        return NULL;
    if (rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER) != RT_EOK)
        return NULL;
    for (record = mappings; record; record = record->next) {
        if ((uint8 *)record + MAPPING_HEADER_SIZE == addr) {
            valid = old_size <= record->size;
            break;
        }
    }
    rt_mutex_release(&mapping_lock);
    if (!valid)
        return NULL;
    replacement = os_mmap(NULL, new_size, MMAP_PROT_READ | MMAP_PROT_WRITE,
                          MMAP_MAP_NONE, os_get_invalid_handle());
    if (!replacement)
        return NULL;
    if (addr)
        memcpy(replacement, addr, old_size < new_size ? old_size : new_size);
    os_munmap(addr, old_size);
    return replacement;
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    mapping_record *record;
    uintptr_t start = (uintptr_t)addr;
    bool valid = false;
    /* Only the existing read/write heap mapping is supported. No hardware
       protection or executable-memory policy is implied by this operation. */
    if (!platform_initialized || !size || size > UINTPTR_MAX - start
        || prot != (MMAP_PROT_READ | MMAP_PROT_WRITE))
        return BHT_ERROR;
    if (rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER) != RT_EOK)
        return BHT_ERROR;
    for (record = mappings; record; record = record->next) {
        uintptr_t base = (uintptr_t)record + MAPPING_HEADER_SIZE;
        if (start >= base && start - base <= record->size
            && size <= record->size - (start - base)) {
            valid = true;
            break;
        }
    }
    rt_mutex_release(&mapping_lock);
    return valid ? BHT_OK : BHT_ERROR;
}

int
os_getpagesize(void)
{
    /* Logical allocation page, also used as the pool aligned_alloc limit.
       This does not advertise MMU protection or page-aligned malloc. */
    return 4096;
}

static void
cache_range(void *address, size_t length, bool instruction)
{
#if defined(RT_USING_CACHE)
    const uintptr_t mask = RT_CPU_CACHE_LINE_SZ - 1;
    const size_t max_chunk = (size_t)INT_MAX & ~(size_t)mask;
    uintptr_t start = (uintptr_t)address, end;

    if (!length)
        return;
    if (length > UINTPTR_MAX - start || start + length > UINTPTR_MAX - mask) {
        os_printf("WAMR: cache range overflow\n");
        return;
    }
    end = (start + length + mask) & ~mask;
    start &= ~mask;
    while (start < end) {
        size_t length_left = end - start;
        int chunk = (int)(length_left > max_chunk ? max_chunk : length_left);
        if (instruction)
            rt_hw_cpu_icache_ops(RT_HW_CACHE_INVALIDATE, (void *)start, chunk);
        else
            rt_hw_cpu_dcache_ops(RT_HW_CACHE_FLUSH, (void *)start, chunk);
        start += (size_t)chunk;
    }
#else
    (void)address;
    (void)length;
    (void)instruction;
#endif
}

void
os_dcache_flush(void)
{
    mapping_record *record;
    if (!platform_initialized)
        return;
    /* WAMR supplies no range here. Clean its live mappings, not the entire
       system cache. A mutex prevents concurrent unmap without disabling IRQs
       for a potentially long cache operation. */
    if (rt_mutex_take(&mapping_lock, RT_WAITING_FOREVER) != RT_EOK)
        return;
    for (record = mappings; record; record = record->next)
        cache_range((uint8 *)record + MAPPING_HEADER_SIZE, record->size, false);
    rt_mutex_release(&mapping_lock);
}

void
os_icache_flush(void *start, size_t len)
{
    /* Also handles caller-owned code buffers outside the mapping registry. */
    cache_range(start, len, false);
    cache_range(start, len, true);
}

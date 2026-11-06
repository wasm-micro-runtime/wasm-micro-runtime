/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"
#ifndef P3_QEMU
#include "pico/stdlib.h"
#endif
#include "wasm_export.h"

/* Override the SDK's weak newlib hook so incidental libc allocations cannot
 * grow beyond the heap reservation included in the ELF's RAM budget. */
static int libc_used, libc_peak;
void *
_sbrk(int increment)
{
    extern char __end__;
    if (increment > PICO_HEAP_SIZE - libc_used || increment < -libc_used) {
        errno = ENOMEM;
        return (void *)-1;
    }
    char *previous = &__end__ + libc_used;
    libc_used += increment;
    if (libc_used > libc_peak)
        libc_peak = libc_used;
    return previous;
}
int
pico_libc_peak(void)
{
    return libc_peak;
}

int
bh_platform_init(void)
{
    return 0;
}
void
bh_platform_destroy(void)
{
}
void *
os_malloc(unsigned size)
{
    (void)size;
    return NULL;
}
void *
os_realloc(void *p, unsigned size)
{
    (void)p;
    (void)size;
    return NULL;
}
void
os_free(void *p)
{
    (void)p;
}
uint64
os_time_get_boot_us(void)
{
#ifdef P3_QEMU
    return 0; /* Timing is obtained from the emulator instruction trace. */
#else
    return time_us_64();
#endif
}
uint64
os_time_thread_cputime_us(void)
{
    return 0;
} /* No thread CPU clock. */
korp_tid
os_self_thread(void)
{
    return 1;
}
uint8 *
os_thread_get_stack_boundary(void)
{
    extern uint8 __StackBottom;
    return &__StackBottom;
}
int
os_mutex_init(korp_mutex *m)
{
    *m = 0;
    return 0;
}
int
os_mutex_destroy(korp_mutex *m)
{
    (void)m;
    return 0;
}
int
os_mutex_lock(korp_mutex *m)
{
    (void)m;
    return 0;
}
int
os_mutex_unlock(korp_mutex *m)
{
    (void)m;
    return 0;
}
int
os_thread_env_init(void)
{
    return 0;
}
void
os_thread_env_destroy(void)
{
}
bool
os_thread_env_inited(void)
{
    return true;
}
void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
    /* Charge committed linear memory to the same fixed pool as metadata. */
    if (size > UINT32_MAX)
        return NULL;
    void *p = wasm_runtime_malloc((uint32)size);
    if (p)
        memset(p, 0, size);
    return p;
}
void
os_munmap(void *p, size_t n)
{
    (void)n;
    wasm_runtime_free(p);
}
void *
os_mremap(void *p, size_t old_n, size_t n)
{
    return os_mremap_slow(p, old_n, n);
}
int
os_mprotect(void *p, size_t n, int prot)
{
    (void)p;
    (void)n;
    (void)prot;
    return 0;
}
os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

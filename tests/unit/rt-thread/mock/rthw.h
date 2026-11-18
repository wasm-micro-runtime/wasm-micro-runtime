/*
 * Copyright (C) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef TEST_RTHW_H
#define TEST_RTHW_H

#define RT_CPU_CACHE_LINE_SZ 32
enum RT_HW_CACHE_OPS {
    RT_HW_CACHE_FLUSH = 0x01,
    RT_HW_CACHE_INVALIDATE = 0x02
};

#ifdef RT_USING_CACHE
void
rt_hw_cpu_dcache_ops(int ops, void *addr, int size);
void
rt_hw_cpu_icache_ops(int ops, void *addr, int size);
#else
#define rt_hw_cpu_dcache_ops(...)
#define rt_hw_cpu_icache_ops(...)
#endif

#endif

/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"

uint64
os_time_get_boot_us()
{
#if defined(__MINGW32__)
    FILETIME file_time;
    ULARGE_INTEGER time;

    GetSystemTimeAsFileTime(&file_time);
    time.LowPart = file_time.dwLowDateTime;
    time.HighPart = file_time.dwHighDateTime;

    /* FILETIME is in 100-nanosecond intervals since January 1, 1601. */
    return (time.QuadPart - 116444736000000000ULL) / 10;
#else
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return ((uint64)ts.tv_sec) * 1000 * 1000 + ((uint64)ts.tv_nsec) / 1000;
#endif
}

uint64
os_time_thread_cputime_us(void)
{
    /* FIXME if u know the right api */
    return os_time_get_boot_us();
}

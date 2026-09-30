/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "platform_api_vmcore.h"

uint64
os_time_get_boot_us()
{
    FILETIME file_time;
    ULARGE_INTEGER time;

    GetSystemTimeAsFileTime(&file_time);
    time.LowPart = file_time.dwLowDateTime;
    time.HighPart = file_time.dwHighDateTime;

    /* FILETIME is in 100-nanosecond intervals since January 1, 1601. */
    return (time.QuadPart - 116444736000000000ULL) / 10;
}

uint64
os_time_thread_cputime_us(void)
{
    /* FIXME if u know the right api */
    return os_time_get_boot_us();
}

/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_CLOCKS_H
#define WASI_CLOCKS_H

#include <stdint.h>
#include <stdbool.h>

#include "wasi_p2_types.h"

#ifdef __cplusplus
extern "C" {
#endif

wasi_datetime_t
wasi_wall_clock_now(void);
wasi_datetime_t
wasi_wall_clock_resolution(void);
wasi_instant_t
wasi_monotonic_clock_now(void);
wasi_duration_t
wasi_monotonic_clock_resolution(void);
wasi_pollable_context_t
wasi_monotonic_clock_subscribe_instant(wasi_instant_t when);
wasi_pollable_context_t
wasi_monotonic_clock_subscribe_duration(wasi_duration_t when);

#ifdef __cplusplus
}
#endif

#endif /* end of _WASI_CLOCKS_H */

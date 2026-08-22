/*
 * Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WASI_P2_RANDOM_H
#define WASI_P2_RANDOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "wasi_p2_types.h"

void
wasi_random_get_random_bytes(uint64_t len, wasi_list_u8_t *ret);
uint64_t
wasi_random_get_random_u64(void);
void
wasi_random_get_insecure_random_bytes(uint64_t len, wasi_list_u8_t *ret);
uint64_t
wasi_random_get_insecure_random_u64(void);
void
wasi_random_get_insecure_seed(uint64_t *seed1, uint64_t *seed2);

#ifdef __cplusplus
}
#endif

#endif /* WASI_RANDOM_H */

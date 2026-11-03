/*
 * Copyright (C) 2026 Matt Hargett. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#ifndef STACKLESS_NGHTTP2_NEWLIB_H
#define STACKLESS_NGHTTP2_NEWLIB_H
#include <stdint.h>

/* The AN505 build uses little-endian newlib without socket headers. */
static inline uint16_t
htons(uint16_t value)
{
    return __builtin_bswap16(value);
}
static inline uint16_t
ntohs(uint16_t value)
{
    return __builtin_bswap16(value);
}
static inline uint32_t
htonl(uint32_t value)
{
    return __builtin_bswap32(value);
}
static inline uint32_t
ntohl(uint32_t value)
{
    return __builtin_bswap32(value);
}
#endif

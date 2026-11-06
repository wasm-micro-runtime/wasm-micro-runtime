/*
 * Copyright (C) 2026 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef WAMR_ZEPHYR_VERSION_H
#define WAMR_ZEPHYR_VERSION_H

/* Detect the generated-header namespace before version macros are available,
 * so older installations also receive the explicit minimum-version error.
 */
#ifdef __has_include
#if __has_include(<zephyr/autoconf.h>)
#include <zephyr/autoconf.h>
#include <zephyr/version.h>
#else
#include <autoconf.h>
#include <version.h>
#endif
#else
#include <autoconf.h>
#include <version.h>
#endif

/* This is the supported baseline, not k_thread_join()'s introduction version.
 */
#if KERNEL_VERSION_NUMBER < 0x030700
#error "WAMR's Zephyr port requires Zephyr 3.7.0 or newer"
#endif

#endif /* WAMR_ZEPHYR_VERSION_H */

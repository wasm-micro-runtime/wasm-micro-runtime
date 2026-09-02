# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_platform_api_tests)
target_sources(app PRIVATE src/test_memory.c src/test_sync.c src/test_thread.c
                           src/test_time.c)

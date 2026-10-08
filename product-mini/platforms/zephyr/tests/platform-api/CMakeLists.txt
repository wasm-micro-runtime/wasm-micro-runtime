# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_platform_api_tests)
target_sources(app PRIVATE src/test_common.c src/test_memory.c src/test_sync.c
                           src/test_thread.c src/test_time.c)

if(CONFIG_WAMR_TEST_USER_MODE)
  set(WAMR_ZEPHYR_APP_PARTITION ztest_mem_partition)
  set(WAMR_BUILD_TARGET ARC)
  set(WAMR_BUILD_INTERP 1)
  set(WAMR_BUILD_AOT 0)
  set(WAMR_BUILD_THREAD_MGR 1)
  set(WAMR_BUILD_GLOBAL_HEAP_POOL 1)
  add_subdirectory(../../user-mode/lib-wamr-zephyr lib-wamr-zephyr)
  target_link_libraries(app PRIVATE wamr_lib)
endif()

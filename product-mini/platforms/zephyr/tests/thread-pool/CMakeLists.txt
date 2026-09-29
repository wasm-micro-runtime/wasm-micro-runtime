# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_thread_pool_tests)

target_include_directories(app PRIVATE ../common)
target_sources(app PRIVATE src/test_thread_pool.c
                           ../common/runtime_user_workflow.c)

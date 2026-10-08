# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_runtime_tests)

target_include_directories(app PRIVATE src ../common)
target_sources(app PRIVATE ../common/runtime_user_workflow.c)

if(CONFIG_WAMR_TEST_USER_MODE)
  target_sources(app PRIVATE src/test_runtime_usermode.c)
else()
  target_sources(app PRIVATE src/test_runtime.c)
endif()

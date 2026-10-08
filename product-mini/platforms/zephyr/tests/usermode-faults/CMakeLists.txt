# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_usermode_fault_tests)

target_include_directories(app PRIVATE src ../common)
target_sources(app PRIVATE
               ../common/runtime_user_workflow.c
               src/expected_fault.c
               src/fault_fixture.c
               src/test_guest_boundaries.c
               src/test_harness.c
               src/test_wamr_domain.c)

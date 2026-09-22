# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_usermode_fault_tests)

set(WAMR_BUILD_TARGET "ARC")
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_GLOBAL_HEAP_POOL 1)
add_subdirectory(../../user-mode/lib-wamr-zephyr lib-wamr-zephyr)

get_property(WAMR_USER_INCLUDE_DIRS
             DIRECTORY ../../user-mode/lib-wamr-zephyr
             PROPERTY INCLUDE_DIRECTORIES)
get_property(WAMR_USER_COMPILE_DEFS
             DIRECTORY ../../user-mode/lib-wamr-zephyr
             PROPERTY COMPILE_DEFINITIONS)

target_include_directories(app PRIVATE src ../common
                           ${WAMR_USER_INCLUDE_DIRS})
target_compile_definitions(app PRIVATE ${WAMR_USER_COMPILE_DEFS})
target_link_libraries(app PRIVATE wamr_lib)
target_sources(app PRIVATE
               ../common/runtime_user_workflow.c
               src/expected_fault.c
               src/fault_fixture.c
               src/test_guest_boundaries.c
               src/test_harness.c
               src/test_wamr_domain.c)

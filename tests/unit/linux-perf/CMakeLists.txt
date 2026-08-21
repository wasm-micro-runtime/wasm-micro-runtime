# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(NOT WAMR_BUILD_AOT AND NOT WAMR_BUILD_JIT)
  message(STATUS "Skipping linux-perf: AOT and LLVM JIT only")
  return()
endif()

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_LIBC_BUILTIN 0)
set (WAMR_BUILD_MULTI_MODULE 0)
set (WAMR_BUILD_LINUX_PERF 1)

add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)

# Feature to test
set (WAMR_BUILD_DUMP_CALL_STACK 1)

include (../unit_common.cmake)

include (${IWASM_DIR}/compilation/iwasm_compl.cmake)

add_executable (linux_perf_test test_sort_func_ptrs.cc)
target_compile_options(linux_perf_test PUBLIC -fpermissive)
target_link_libraries(linux_perf_test ${LLVM_AVAILABLE_LIBS} gtest_main )
gtest_discover_tests(linux_perf_test)

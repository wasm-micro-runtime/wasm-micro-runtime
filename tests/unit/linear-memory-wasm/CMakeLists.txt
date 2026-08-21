# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping linear-memory-wasm: classic interpreter only")
  return()
endif()

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping linear-memory-wasm: classic interpreter only")
  return()
endif()

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)
set (WAMR_BUILD_MEMORY_PROFILING 1)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
    ${UNIT_SOURCE}
    ${WAMR_RUNTIME_LIB_SOURCE}
    ${UNCOMMON_SHARED_SOURCE}
)

# Test case: .wasm file with hardware bound check.
add_executable (linear_memory_test_wasm ${unit_test_sources})
target_link_libraries (linear_memory_test_wasm gtest_main)
gtest_discover_tests(linear_memory_test_wasm)
target_compile_definitions(linear_memory_test_wasm PRIVATE WAMR_DISABLE_HW_BOUND_CHECK=0)

add_custom_command(TARGET linear_memory_test_wasm POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  ${CMAKE_CURRENT_LIST_DIR}/wasm_files/*
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Copy wasm files to the directory: build/linear-memory-wasm."
)

# Test case: .wasm file with no hardware bound check.
add_executable (linear_memory_test_wasm_no_hw_bound ${unit_test_sources})
target_link_libraries (linear_memory_test_wasm_no_hw_bound gtest_main)
gtest_discover_tests(linear_memory_test_wasm_no_hw_bound)
target_compile_definitions(linear_memory_test_wasm_no_hw_bound PRIVATE WAMR_DISABLE_HW_BOUND_CHECK=1)

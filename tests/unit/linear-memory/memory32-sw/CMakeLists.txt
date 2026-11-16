# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(linear-memory-memory32-sw MODES classic-interp aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()
set(WAMR_BUILD_MEMORY64 0)
set(WAMR_BUILD_SHARED_HEAP 0)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)

include(../../unit_common.cmake)

set(LINEAR_MEMORY_TEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../linear_memory_test.cc)

set(unit_test_sources
  ${LINEAR_MEMORY_TEST_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(linear_memory_test_memory32-sw ${unit_test_sources})
target_link_libraries(linear_memory_test_memory32-sw gtest_main)
gtest_discover_tests(linear_memory_test_memory32-sw TEST_PREFIX linear_memory_test_memory32-sw.)

wamr_unit_test_compile_wat_to_wasm(
  TARGET linear_memory_test_memory32-sw
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-apps/access_mem32.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/access_mem32.wasm
)

if(WAMR_BUILD_AOT EQUAL 1)
  wamr_unit_test_compile_wasm_to_aot(
    TARGET linear_memory_test_memory32-sw
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/access_mem32.wasm
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/access_mem32.aot
    FLAGS --bounds-checks=1
  )
endif()

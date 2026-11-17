# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(wasm-memory MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Shared heap is part of wasm_memory.c and is exercised directly.
set(WAMR_BUILD_SHARED_HEAP 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(wasm_memory_test ${unit_test_sources})
target_link_libraries(wasm_memory_test gtest_main)

wamr_unit_test_compile_wat_to_wasm(
  TARGET wasm_memory_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/memory.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/memory.wasm
)

gtest_discover_tests(wasm_memory_test)

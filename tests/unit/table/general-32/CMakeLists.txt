# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(table/general-32 MODES classic-interp aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Only the 32-bit build runs this suite. It is the control for general-64: on
# a 32-bit table element, the uninitialized value and NULL_REF are the same, so
# these cases must pass both before and after the NULL_REF fix.
if(NOT WAMR_BUILD_TARGET STREQUAL "X86_32")
  return()
endif()

set(WAMR_BUILD_REF_TYPES 1)

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(table_general_32_test ${unit_test_sources})
target_link_libraries(table_general_32_test gtest_main)
gtest_discover_tests(table_general_32_test)

wamr_unit_test_compile_wat_to_wasm(
  TARGET table_general_32_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/uninit_elem.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/uninit_elem.wasm
)

if(WAMR_BUILD_AOT EQUAL 1)
  wamr_unit_test_compile_wasm_to_aot(
    TARGET table_general_32_test
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/uninit_elem.wasm
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/uninit_elem.aot
    FLAGS --target=i386
  )
endif()

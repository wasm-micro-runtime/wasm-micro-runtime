# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(memory64/base MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Feature under test.  The atomic cases need WAMR_BUILD_SHARED_MEMORY and live
# in tests/unit/memory64/atomic.
set(WAMR_BUILD_MEMORY64 1)

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(memory64_test ${unit_test_sources})
target_link_libraries(memory64_test ${LLVM_AVAILABLE_LIBS} gtest_main)

wamr_unit_test_compile_c_to_wasm(
  TARGET memory64_test
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-apps
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

set(_generated_wasm_fixtures
  8GB_memory
  page_exceed_u32
  page_exceed_u32_2
  page_u32_max
)
foreach(_fixture IN LISTS _generated_wasm_fixtures)
  wamr_unit_test_compile_wat_to_wasm(
    TARGET memory64_test
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-apps/${_fixture}.wat
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_fixture}.wasm
  )
endforeach()
unset(_generated_wasm_fixtures)
unset(_fixture)

gtest_discover_tests(memory64_test)

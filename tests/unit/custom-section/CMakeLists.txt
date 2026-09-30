# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(custom-section MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)

# Feature under test.
set(WAMR_BUILD_LOAD_CUSTOM_SECTION 1)

include(../unit_common.cmake)

include(${IWASM_DIR}/compilation/iwasm_compl.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${IWASM_COMPL_SOURCE}
)

add_executable(custom_section_test ${unit_test_sources})
target_link_libraries(custom_section_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# The cases read wasm-apps/app.wasm relative to the test working directory.
wamr_unit_test_compile_c_to_wasm(
  TARGET custom_section_test
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
)

gtest_discover_tests(custom_section_test)

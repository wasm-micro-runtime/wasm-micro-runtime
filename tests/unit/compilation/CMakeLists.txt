# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(compilation MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)

# The cases drive the in-process AOT compiler.
include(../unit_common.cmake)

include(${IWASM_DIR}/compilation/iwasm_compl.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${IWASM_COMPL_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(compilation_test ${unit_test_sources})
target_link_libraries(compilation_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# main.wasm has no .wat/.c source to compile it from.
wamr_unit_test_copy_wasm_files(compilation_test
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/main.wasm
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

gtest_discover_tests(compilation_test)

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(gc MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_GC 1)
set (WAMR_BUILD_APP_FRAMEWORK 0)
set (WAMR_BUILD_SANITIZER "asan")

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}   
)

add_executable (gc_test ${unit_test_sources})
target_link_libraries (gc_test gtest_main)

add_custom_command(TARGET gc_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
  ${CMAKE_CURRENT_LIST_DIR}/wasm-apps/*.was*
  ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Copy wasm files to directory ${CMAKE_CURRENT_BINARY_DIR}"
)

gtest_discover_tests(gc_test)

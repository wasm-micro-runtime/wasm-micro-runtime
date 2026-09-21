# Copyright (C) 2024 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(exception-handling MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The cases exercise the exception-handling proposal instructions.
set(WAMR_BUILD_EXCE_HANDLING 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${IWASM_DIR}/interpreter)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(exception_handling_test ${unit_test_sources})
target_link_libraries(exception_handling_test gtest_main)
gtest_discover_tests(exception_handling_test)

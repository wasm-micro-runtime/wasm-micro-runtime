# Copyright (C) 2024 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(exception-handling MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_EXCE_HANDLING 1)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})
include_directories (${IWASM_DIR}/interpreter)

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (exception_handling_test ${unit_test_sources})

target_link_libraries (exception_handling_test gtest_main)

gtest_discover_tests(exception_handling_test)

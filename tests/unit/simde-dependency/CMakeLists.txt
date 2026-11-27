# Copyright (C) 2026 Ray Wong.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(simde_dependency_test C)

if(DEFINED SIMDE_TEST_EXPECTED)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../core/iwasm/libraries/simde/simde.cmake)
  add_executable(simde_header_test check.c)
  target_compile_definitions(simde_header_test PRIVATE EXPECTED_SOURCE=${SIMDE_TEST_EXPECTED})
  return()
endif()

# Also runnable independently: cmake -S tests/unit/simde-dependency -B build
# and ctest --test-dir build --output-on-failure.
enable_testing()
foreach(test_case local manual override cached_fetch old_local)
  add_test(
    NAME simde_dependency_${test_case}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_CASE=${test_case}
      -DTEST_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/${test_case}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/run_test.cmake
  )
endforeach()

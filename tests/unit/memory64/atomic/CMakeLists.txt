# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(memory64/atomic MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The atomic opcodes run on a shared memory, so this half needs
# WAMR_BUILD_SHARED_MEMORY; the plain memory64 cases pass without it and live
# in tests/unit/memory64/base, which is why the two are separate suites.
set(WAMR_BUILD_MEMORY64 1)
set(WAMR_BUILD_SHARED_MEMORY 1)

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(memory64_atomic_test ${unit_test_sources})
target_link_libraries(memory64_atomic_test ${LLVM_AVAILABLE_LIBS} gtest_main)

wamr_unit_test_compile_wat_to_wasm(
  TARGET memory64_atomic_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-apps/atomic_opcodes.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/atomic_opcodes.wasm
)

gtest_discover_tests(memory64_atomic_test)

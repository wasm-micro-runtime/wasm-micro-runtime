# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(mem-alloc MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Test-specific feature configuration

include(../unit_common.cmake)

# Test source files
set(TEST_SOURCES
  test_runner.c
  ${WAMR_RUNTIME_LIB_SOURCE}
)

#
# Create test executable
#

## Normal test executable
add_executable(mem-alloc-test ${TEST_SOURCES})

# Add include directories for mem-alloc internals
target_include_directories(mem-alloc-test PRIVATE
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc/ems
)

## GC test executable
add_executable(mem-alloc-gc-test ${TEST_SOURCES})

target_include_directories(mem-alloc-gc-test PRIVATE
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc/ems
)

target_compile_options(mem-alloc-gc-test PRIVATE -DWAMR_BUILD_GC=1 -DWAMR_BUILD_GC_VERIFY=1)

# Link dependencies
target_link_libraries(mem-alloc-test cmocka::cmocka m)
target_link_libraries(mem-alloc-gc-test cmocka::cmocka m)

# Add to ctest
add_test(NAME mem-alloc-test COMMAND mem-alloc-test)
set_tests_properties(mem-alloc-test PROPERTIES TIMEOUT 60)

add_test(NAME mem-alloc-gc-test COMMAND mem-alloc-gc-test)
set_tests_properties(mem-alloc-gc-test PROPERTIES TIMEOUT 60)

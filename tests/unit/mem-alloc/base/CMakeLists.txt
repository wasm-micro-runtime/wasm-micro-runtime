# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(mem-alloc/base MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

include(../../unit_common.cmake)

set(TEST_SOURCES
  ../test_runner.c
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(mem-alloc-test ${TEST_SOURCES})

# Add include directories for mem-alloc internals
target_include_directories(mem-alloc-test PRIVATE
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc/ems
)

target_link_libraries(mem-alloc-test cmocka::cmocka m)

add_test(NAME mem-alloc-test COMMAND mem-alloc-test)
set_tests_properties(mem-alloc-test PROPERTIES TIMEOUT 60)

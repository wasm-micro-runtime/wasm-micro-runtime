# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(mem-alloc/gc MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The allocator is exercised with the GC heap.  WAMR_BUILD_GC_HEAP_VERIFY is
# deliberately not enabled: BH_ENABLE_GC_VERIFY makes
# wasm_runtime_aligned_alloc(0, ...) exit(-1), which the shared cases trip.
set(WAMR_BUILD_GC 1)

include(../../unit_common.cmake)

set(TEST_SOURCES
  ../test_runner.c
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(mem-alloc-gc-test ${TEST_SOURCES})

# Add include directories for mem-alloc internals
target_include_directories(mem-alloc-gc-test PRIVATE
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc
  ${WAMR_ROOT_DIR}/core/shared/mem-alloc/ems
)

target_link_libraries(mem-alloc-gc-test cmocka::cmocka m)

add_test(NAME mem-alloc-gc-test COMMAND mem-alloc-gc-test)
set_tests_properties(mem-alloc-gc-test PROPERTIES TIMEOUT 60)

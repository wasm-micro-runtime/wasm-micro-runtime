# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(runtime-common MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_MULTI_MODULE 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${IWASM_DIR}/aot)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(runtime_common_test ${unit_test_sources})
target_link_libraries(runtime_common_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# main.wasm has no .wat/.c source to compile it from; main.aot is generated
# from it with wamrc at build time, because an AOT file is tied to both the
# target arch and the AOT format version, so it cannot be checked in.  Both
# are read relative to the test working directory.
wamr_unit_test_copy_wasm_files(runtime_common_test
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/main.wasm
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

wamr_unit_test_compile_wasm_to_aot(
  TARGET runtime_common_test
  INPUT ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/main.wasm
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.aot
)

gtest_discover_tests(runtime_common_test)

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(aot-stack-frame MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_DISABLE_HW_BOUND_CHECK 1)
set(WAMR_DISABLE_WRITE_GS_BASE 1)
set(WAMR_BUILD_GC 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_definitions(-DAOT_STACK_FRAME_DEBUG)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(UNIT_SOURCE ${source_all})

set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

# TODO: wasm-apps/ rebuilds wamrc and test-tools/binarydump-tool at build time
# and writes test_aot.h into the source tree.  It should instead use
# wamr_unit_test_compile_wat_to_wasm() + wamr_unit_test_compile_wasm_to_aot()
# for the test.wast -> test.aot step, plus one custom command for the
# binarydump header, with both outputs in the build directory.
#
# Automatically build wasm-apps for this test
add_subdirectory(wasm-apps)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(aot_stack_frame_test ${unit_test_sources})

add_dependencies(aot_stack_frame_test aot-stack-frame-test-wasm)

target_link_libraries(aot_stack_frame_test ${LLVM_AVAILABLE_LIBS} gtest_main )

gtest_discover_tests(aot_stack_frame_test)

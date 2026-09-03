# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(aot-stack-frame MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_SIMD 1)
set (WAMR_BUILD_REF_TYPES 1)
set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_LIBC_BUILTIN 0)
set (WAMR_BUILD_MULTI_MODULE 0)
set (WAMR_DISABLE_HW_BOUND_CHECK 1)
set (WAMR_DISABLE_WRITE_GS_BASE 1)
set (WAMR_BUILD_GC 1)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

add_definitions (-DWASM_ENABLE_AOT_STACK_FRAME=1)
add_definitions (-DAOT_STACK_FRAME_DEBUG)
#add_definitions (-DWASM_ENABLE_DUMP_CALL_STACK=1)

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  # TODO: IWASM_COMPL_SOURCE is only populated when iwasm_compl.cmake is
  # included, which runtime_lib.cmake does only for WAMR_BUILD_JIT.  This
  # suite runs in aot mode (WAMR_BUILD_JIT=0), so the AOT compiler sources
  # are currently NOT compiled into this test.  If the test is meant to use
  # the in-process AOT compiler, include ${IWASM_DIR}/compilation/
  # iwasm_compl.cmake here; otherwise drop this reference.
  ${IWASM_COMPL_SOURCE}
)

# Automatically build wasm-apps for this test
add_subdirectory(wasm-apps)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (aot_stack_frame_test ${unit_test_sources})

add_dependencies (aot_stack_frame_test aot-stack-frame-test-wasm)

target_link_libraries (aot_stack_frame_test ${LLVM_AVAILABLE_LIBS} gtest_main )

gtest_discover_tests(aot_stack_frame_test)

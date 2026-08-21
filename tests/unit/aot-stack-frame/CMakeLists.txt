# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(NOT WAMR_BUILD_AOT OR WAMR_BUILD_INTERP OR WAMR_BUILD_FAST_INTERP OR
   WAMR_BUILD_JIT OR WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping aot-stack-frame: AOT only")
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
  ${IWASM_COMPL_SOURCE}
)

# Automatically build wasm-apps for this test
add_subdirectory(wasm-apps)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (aot_stack_frame_test ${unit_test_sources})

add_dependencies (aot_stack_frame_test aot-stack-frame-test-wasm)

target_link_libraries (aot_stack_frame_test ${LLVM_AVAILABLE_LIBS} gtest_main )

gtest_discover_tests(aot_stack_frame_test)

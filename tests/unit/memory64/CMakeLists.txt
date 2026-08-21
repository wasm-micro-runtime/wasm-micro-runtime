# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(test-memory64)

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping memory64: classic interpreter only")
  return()
endif()

set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_APP_FRAMEWORK 0)
# TODO: Currently only support classic interpreter mode
set(WAMR_BUILD_MEMORY64 1)
set(WAMR_BUILD_SHARED_MEMORY 1)

# if only load this CMake other than load it as subdirectory
# include(GoogleTest)
include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(UNIT_SOURCE ${source_all})

set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(memory64_test ${unit_test_sources})

target_link_libraries(memory64_test ${LLVM_AVAILABLE_LIBS} gtest_main)

add_custom_command(TARGET memory64_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/*.wasm
        ${CMAKE_CURRENT_BINARY_DIR}/
        COMMENT "Copy test wasm files to the directory of google test"
        )

gtest_discover_tests(memory64_test)

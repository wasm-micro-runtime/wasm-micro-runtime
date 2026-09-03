# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(test-memory64)

wamr_unit_test_suite_run_modes(memory64 MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
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

gtest_discover_tests(memory64_test)

wamr_unit_test_compile_c_to_wasm(
        TARGET memory64_test
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
        DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

# Generated WASM fixture declarations

# Generate tracked WASM fixtures from WAT/WAST sources.
set(_generated_wasm_fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/8GB_memory.wat|${CMAKE_CURRENT_BINARY_DIR}/8GB_memory.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/atomic_opcodes.wat|${CMAKE_CURRENT_BINARY_DIR}/atomic_opcodes.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/page_exceed_u32.wat|${CMAKE_CURRENT_BINARY_DIR}/page_exceed_u32.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/page_exceed_u32_2.wat|${CMAKE_CURRENT_BINARY_DIR}/page_exceed_u32_2.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/page_u32_max.wat|${CMAKE_CURRENT_BINARY_DIR}/page_u32_max.wasm
)
foreach(_fixture IN LISTS _generated_wasm_fixtures)
    string(REPLACE "|" ";" _fixture_parts "${_fixture}")
    list(GET _fixture_parts 0 _fixture_source)
    list(GET _fixture_parts 1 _fixture_output)
    wamr_unit_test_compile_wat_to_wasm(
        TARGET memory64_test
        SOURCE ${_fixture_source}
        OUTPUT ${_fixture_output}
    )
endforeach()
unset(_generated_wasm_fixtures)
unset(_fixture_parts)
unset(_fixture_source)
unset(_fixture_output)

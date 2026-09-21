# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(linear-memory-wasm MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(linear_memory_test_wasm ${unit_test_sources})
target_link_libraries(linear_memory_test_wasm gtest_main)
gtest_discover_tests(linear_memory_test_wasm)

# TODO: the second target only differs from the first by
# target_compile_definitions(WAMR_DISABLE_HW_BOUND_CHECK=...), which the test
# never reads (it checks WASM_DISABLE_HW_BOUND_CHECK, set by
# build-scripts/config_common.cmake), so today both targets run the same
# cases.  Either drop it, or move the no-hw-bound half into its own suite with
# set(WAMR_DISABLE_HW_BOUND_CHECK 1) before ../unit_common.cmake.
add_executable(linear_memory_test_wasm_no_hw_bound ${unit_test_sources})
target_link_libraries(linear_memory_test_wasm_no_hw_bound gtest_main)
gtest_discover_tests(linear_memory_test_wasm_no_hw_bound)

# Generated WASM fixture declarations

# Generate tracked WASM fixtures from WAT/WAST sources.
set(_generated_wasm_fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_grow_out_of_bounds_01.wast|${CMAKE_CURRENT_BINARY_DIR}/mem_grow_out_of_bounds_01.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_grow_out_of_bounds_02.wast|${CMAKE_CURRENT_BINARY_DIR}/mem_grow_out_of_bounds_02.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/out_of_bounds.wast|${CMAKE_CURRENT_BINARY_DIR}/out_of_bounds.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_01.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_01.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_02.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_02.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_03.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_03.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_05.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_05.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_07.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_07.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_08.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_08.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_09.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_09.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_10.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_10.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_12.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_12.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_14.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_14.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_16.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_16.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/mem_page_20.wast|${CMAKE_CURRENT_BINARY_DIR}/wasm_mem_page_20.wasm
)
foreach(_fixture IN LISTS _generated_wasm_fixtures)
    string(REPLACE "|" ";" _fixture_parts "${_fixture}")
    list(GET _fixture_parts 0 _fixture_source)
    list(GET _fixture_parts 1 _fixture_output)
    foreach(_fixture_target
            linear_memory_test_wasm
            linear_memory_test_wasm_no_hw_bound)
        wamr_unit_test_compile_wat_to_wasm(
            TARGET ${_fixture_target}
            SOURCE ${_fixture_source}
            OUTPUT ${_fixture_output}
        )
    endforeach()
endforeach()
unset(_generated_wasm_fixtures)
unset(_fixture_target)
unset(_fixture_parts)
unset(_fixture_source)
unset(_fixture_output)

# The .wasm files in wasm_files/ have no .wast source next to them.
wamr_unit_test_copy_wasm_files(linear_memory_test_wasm
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_app_heap_02.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_04.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_06.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_11.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_13.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_15.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_17.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_18.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm_files/wasm_mem_page_19.wasm
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

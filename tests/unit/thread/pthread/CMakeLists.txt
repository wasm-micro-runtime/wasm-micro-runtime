# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(memory64 MODES classic-interp aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_LIB_PTHREAD 1)
# destructor_slot.wat replaces a table slot with table.get and table.set
set(WAMR_BUILD_REF_TYPES 1)

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(pthread_test ${unit_test_sources})

target_link_libraries(pthread_test ${LLVM_AVAILABLE_LIBS} gtest_main)

gtest_discover_tests(pthread_test)

target_compile_definitions(pthread_test PRIVATE
  THREAD_TEST_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}")

wamr_unit_test_get_current_run_mode(_run_mode)
if(_run_mode STREQUAL "aot")
  target_compile_definitions(pthread_test PRIVATE THREAD_TEST_AOT=1)
else()
  target_compile_definitions(pthread_test PRIVATE THREAD_TEST_AOT=0)
endif()

file(GLOB pthread_wat_files ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/*.wat)
foreach(wat_file IN LISTS pthread_wat_files)
  get_filename_component(fixture_name ${wat_file} NAME_WE)
  set(wasm_file ${CMAKE_CURRENT_BINARY_DIR}/${fixture_name}.wasm)
  wamr_unit_test_compile_wat_to_wasm(
    TARGET pthread_test
    SOURCE ${wat_file}
    OUTPUT ${wasm_file}
  )
  if(_run_mode STREQUAL "aot")
    set(_aot_flags --enable-multi-thread)
    if(WAMR_BUILD_TARGET STREQUAL "X86_32")
      list(APPEND _aot_flags --target=i386)
    endif()
    wamr_unit_test_compile_wasm_to_aot(
      TARGET pthread_test
      INPUT ${wasm_file}
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fixture_name}.aot
      FLAGS ${_aot_flags}
    )
  endif()
endforeach()

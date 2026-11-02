# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
wamr_unit_test_suite_run_modes(c-api-continuations MODES fast-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_C_API_CONTINUATIONS 1)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
include(../unit_common.cmake)

set(fixture_dir ${WAMR_ROOT_DIR}/samples/shared-continuations)
configure_file(${WAMR_ROOT_DIR}/core/iwasm/include/wasm_c_api.h
               ${CMAKE_CURRENT_BINARY_DIR}/wasm.h COPYONLY)
add_executable(c_api_continuations_test
               ${fixture_dir}/fixture.c ${WAMR_RUNTIME_LIB_SOURCE})
target_compile_definitions(c_api_continuations_test PRIVATE TEST_CONTINUATIONS)
target_include_directories(c_api_continuations_test BEFORE PRIVATE
                           ${CMAKE_CURRENT_BINARY_DIR})
find_package(Threads REQUIRED)
target_link_libraries(c_api_continuations_test Threads::Threads ${CMAKE_DL_LIBS} m)
wamr_unit_test_compile_wat_to_wasm(
  TARGET c_api_continuations_test
  SOURCE ${fixture_dir}/fixture.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fixture.wasm
  FLAGS --debug-names)
add_test(NAME c-api-continuations
         COMMAND c_api_continuations_test ${CMAKE_CURRENT_BINARY_DIR}/fixture.wasm)
set_tests_properties(c-api-continuations PROPERTIES
                     ENVIRONMENT WASM_EXT_TEST_ONLY=1 TIMEOUT 30)

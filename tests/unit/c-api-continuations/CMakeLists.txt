# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
wamr_unit_test_suite_run_modes(c-api-continuations MODES fast-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_C_API_CONTINUATIONS 1)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
set(WAMR_BUILD_GC 1)
set(WAMR_BUILD_STRINGREF 0)
include(../unit_common.cmake)

set(fixture_dir ${WAMR_ROOT_DIR}/samples/shared-continuations)
configure_file(${WAMR_ROOT_DIR}/core/iwasm/include/wasm_c_api.h
               ${CMAKE_CURRENT_BINARY_DIR}/wasm.h COPYONLY)
add_library(c_api_continuations_runtime OBJECT ${WAMR_RUNTIME_LIB_SOURCE})
find_package(Threads REQUIRED)
foreach(_fixture fixture edges features)
  add_executable(c_api_continuations_${_fixture}_test
                 ${fixture_dir}/${_fixture}.c
                 $<TARGET_OBJECTS:c_api_continuations_runtime>)
  target_compile_definitions(c_api_continuations_${_fixture}_test
                             PRIVATE TEST_CONTINUATIONS TEST_WAMR)
  target_include_directories(c_api_continuations_${_fixture}_test BEFORE PRIVATE
                             ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(c_api_continuations_${_fixture}_test
                        Threads::Threads ${CMAKE_DL_LIBS} m)
endforeach()
wamr_unit_test_compile_wat_to_wasm(
  TARGET c_api_continuations_fixture_test
  SOURCE ${fixture_dir}/fixture.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fixture.wasm
  FLAGS --debug-names)
add_test(NAME c-api-continuations
         COMMAND c_api_continuations_fixture_test ${CMAKE_CURRENT_BINARY_DIR}/fixture.wasm)
set_tests_properties(c-api-continuations PROPERTIES
                     ENVIRONMENT WASM_EXT_TEST_ONLY=1 TIMEOUT 30)

add_test(NAME c-api-continuation-edges COMMAND c_api_continuations_edges_test)
set_tests_properties(c-api-continuation-edges PROPERTIES TIMEOUT 30)

# WABT cannot parse the GC text format. Regenerate with wasm-tools as documented
# in the shared fixture README; keep the numeric host boundary identical.
configure_file(${fixture_dir}/gc.wasm ${CMAKE_CURRENT_BINARY_DIR}/gc.wasm COPYONLY)
add_test(NAME c-api-continuation-gc
         COMMAND c_api_continuations_features_test ${CMAKE_CURRENT_BINARY_DIR}/gc.wasm)
set_tests_properties(c-api-continuation-gc PROPERTIES TIMEOUT 30)

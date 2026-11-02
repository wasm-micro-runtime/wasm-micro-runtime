# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(wasm-vm MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

#TODO: split wasm_vm.cc into two separate files for better modularity
# The cases call the multi-module APIs unconditionally.
set(WAMR_BUILD_MULTI_MODULE 1)

# The wasm-apps fixtures (app1/app2/app3) import env.malloc/calloc/free/
# memcpy/strdup from libc-builtin, so the suite unconditionally needs
# LIBC_BUILTIN to resolve these imports.
set(WAMR_BUILD_LIBC_BUILTIN 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(wasm_vm_test ${unit_test_sources})
target_link_libraries(wasm_vm_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# Generated WASM fixture declarations

set(_generated_wasm_fixtures m1 m2 m3)
foreach(_fixture IN LISTS _generated_wasm_fixtures)
    wamr_unit_test_compile_wat_to_wasm(
        TARGET wasm_vm_test
        SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/app4/${_fixture}.wat
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_fixture}.wasm
    )
endforeach()
unset(_generated_wasm_fixtures)
unset(_fixture)

gtest_discover_tests(wasm_vm_test)

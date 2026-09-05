# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(wasm-vm MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

if(NOT WAMR_BUILD_MULTI_MODULE)
  set(WAMR_BUILD_MULTI_MODULE 0)
endif()

# The wasm-apps fixtures (app1/app2/app3) import env.malloc/calloc/free/
# memcpy/strdup from libc-builtin, so the suite unconditionally needs
# LIBC_BUILTIN to resolve these imports.
set(WAMR_BUILD_LIBC_BUILTIN 1)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(wasm_vm_test ${unit_test_sources})

target_link_libraries(wasm_vm_test ${LLVM_AVAILABLE_LIBS} gtest_main)

gtest_discover_tests(wasm_vm_test)

# Generated WASM fixture declarations

# Generate tracked WASM fixtures from WAT/WAST sources.
set(_generated_wasm_fixtures
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/app4/m1.wat|${CMAKE_CURRENT_BINARY_DIR}/m1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/app4/m2.wat|${CMAKE_CURRENT_BINARY_DIR}/m2.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/app4/m3.wat|${CMAKE_CURRENT_BINARY_DIR}/m3.wasm
)
foreach(_fixture IN LISTS _generated_wasm_fixtures)
    string(REPLACE "|" ";" _fixture_parts "${_fixture}")
    list(GET _fixture_parts 0 _fixture_source)
    list(GET _fixture_parts 1 _fixture_output)
    wamr_unit_test_compile_wat_to_wasm(
        TARGET wasm_vm_test
        SOURCE ${_fixture_source}
        OUTPUT ${_fixture_output}
    )
endforeach()
unset(_generated_wasm_fixtures)
unset(_fixture_parts)
unset(_fixture_source)
unset(_fixture_output)

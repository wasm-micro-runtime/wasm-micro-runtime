# Copyright (C) 2026 WAMR Community.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(multi-module MODES classic-interp fast-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_MULTI_MODULE 1)
# The call stack of a trap in another module instance is covered
set (WAMR_BUILD_DUMP_CALL_STACK 1)

include (../unit_common.cmake)

set (unit_test_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/multi_module_test.cc
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable (multi_module_test ${unit_test_sources})
target_link_libraries (multi_module_test gtest_main)

foreach (fixture callee caller)
  wamr_unit_test_compile_wat_to_wasm (
    TARGET multi_module_test
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/${fixture}.wat
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${fixture}.wasm
  )
endforeach ()

gtest_discover_tests (multi_module_test)

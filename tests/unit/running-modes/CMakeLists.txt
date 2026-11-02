# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(running-modes
  MODES classic-interp fast-interp llvm-jit
)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# Fast-interp and fast-jit runtime behavior is only exercised on X86_64
# unit-test builds (see .github/workflows/unit_test.yml); skip the suite
# on other targets for these modes to avoid untested combinations.
if(NOT WAMR_BUILD_TARGET STREQUAL "X86_64")
  if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_FAST_JIT)
    return()
  endif()
endif()

# The wasm-apps fixtures (mytest.c/hello.c) call printf/malloc/snprintf/
# free, which resolve to the libc-builtin native functions, so the suite
# needs LIBC_BUILTIN (dropping it fails
# wasm_runtime_set_and_get_running_mode_complex).
set(WAMR_BUILD_LIBC_BUILTIN 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
  ${source_all}
)

add_executable(wasm_running_modes_test ${unit_test_sources})
target_link_libraries(wasm_running_modes_test ${LLVM_AVAILABLE_LIBS} gtest_main)

wamr_unit_test_compile_c_to_wasm(
  TARGET wasm_running_modes_test
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

gtest_discover_tests(wasm_running_modes_test)

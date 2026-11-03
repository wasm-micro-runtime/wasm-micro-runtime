# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(wasm-c-api/base
  MODES classic-interp fast-interp aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# No WAMR_BUILD_* switch is set: the suite covers wasm_c_api.c with the default
# feature set (no GC, ref types, SIMD, memory64 or libc-wasi).  The variants
# that need one of them belong to separate suites.

# wamrc compiles for the host target by default, so the AOT variant is limited
# to the targets the other AOT unit suites cover, plus X86_32 which the CI
# unit-test matrix builds.
if(WAMR_BUILD_AOT EQUAL 1
   AND NOT (WAMR_BUILD_TARGET STREQUAL "X86_64"
            OR WAMR_BUILD_TARGET STREQUAL "X86_32"
            OR WAMR_BUILD_TARGET STREQUAL "AARCH64"))
  return()
endif()

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(wasm_c_api_base_test ${unit_test_sources})
target_link_libraries(wasm_c_api_base_test gtest_main)

# The cases load a fixture through one file name; the extension selects the
# format of the current run mode.
if(WAMR_BUILD_AOT EQUAL 1)
  target_compile_definitions(wasm_c_api_base_test PRIVATE
    WASM_C_API_FIXTURE_EXT=".aot")
else()
  target_compile_definitions(wasm_c_api_base_test PRIVATE
    WASM_C_API_FIXTURE_EXT=".wasm")
endif()

# The fixtures a case loads are addressed by name only; the binary sits next to
# the generated .wasm/.aot files in ${CMAKE_CURRENT_BINARY_DIR}.  Both sources
# stay inside the instruction set of the default feature set, so the helper's
# default --enable-all is enough.
#
# The X86_32 unit tests are 32-bit binaries on a 64-bit host, so wamrc has to
# be told to emit i386 code explicitly.
set(_wasm_c_api_aot_flags)
if(WAMR_BUILD_TARGET STREQUAL "X86_32")
  set(_wasm_c_api_aot_flags --target=i386)
endif()

foreach(_fixture simple empty)
  wamr_unit_test_compile_wat_to_wasm(
    TARGET wasm_c_api_base_test
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/${_fixture}.wat
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_fixture}.wasm
  )
  if(WAMR_BUILD_AOT EQUAL 1)
    wamr_unit_test_compile_wasm_to_aot(
      TARGET wasm_c_api_base_test
      INPUT ${CMAKE_CURRENT_BINARY_DIR}/${_fixture}.wasm
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_fixture}.aot
      FLAGS ${_wasm_c_api_aot_flags}
    )
  endif()
endforeach()
unset(_fixture)
unset(_wasm_c_api_aot_flags)

gtest_discover_tests(wasm_c_api_base_test)

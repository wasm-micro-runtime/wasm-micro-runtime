# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(wasm-c-api/dump-call-stack MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The trap frames and the wasm_frame_* family only exist with call-stack
# dumping, which the default feature set leaves off.  This is the one switch
# the suite needs; the base suite keeps the negative side of the same cases.
set(WAMR_BUILD_DUMP_CALL_STACK 1)

include(../../unit_common.cmake)

# The shared fixture header lives with the suite whose fixtures these cases
# reuse, so the include path points at base/ as well.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/../base)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable(wasm_c_api_dump_call_stack_test ${unit_test_sources})
target_link_libraries(wasm_c_api_dump_call_stack_test gtest_main)

# The trap fixture is generated in the suite the cases are built from; sharing
# the target name with wasm-c-api/base is intentional (CMake's
# wamr_unit_test_compile_wat_to_wasm ignores a fixture target it already
# created), so the .wasm is built once.
wamr_unit_test_compile_wat_to_wasm(
  TARGET wasm_c_api_dump_call_stack_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../base/wasm-apps/simple.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/simple.wasm
)

gtest_discover_tests(wasm_c_api_dump_call_stack_test)

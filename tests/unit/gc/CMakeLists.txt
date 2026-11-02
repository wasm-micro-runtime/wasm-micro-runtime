# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(gc MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_GC 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(gc_test ${unit_test_sources})
target_link_libraries(gc_test gtest_main)

# The .wast files next to these fixtures use the GC text format (struct.new,
# sub, packed field types), which WABT 1.0.37's wat2wasm cannot parse yet, so
# the .wasm files cannot be regenerated during the build and are copied as-is.
wamr_unit_test_copy_wasm_files(gc_test
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/func1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/func2.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/global1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/nested_array_elem_any.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/nested_struct_field_any.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/struct1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/struct2.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/struct3.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/table1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test1.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test2.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test3.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test4.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test5.wasm
    ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test6.wasm
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

gtest_discover_tests(gc_test)

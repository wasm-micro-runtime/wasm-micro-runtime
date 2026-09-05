# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(runtime-common MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()
# Tests call the multi-module registration APIs unconditionally
# (wasm_runtime_register_module_internal/_unregister_module/...).
set (WAMR_BUILD_MULTI_MODULE 1)
set (WAMR_BUILD_LIBC_BUILTIN 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${IWASM_DIR}/aot)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(UNIT_SOURCE ${source_all})

set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(runtime_common_test ${unit_test_sources})

target_link_libraries(runtime_common_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# Ensure that aot compiled is completed before linear_memory_test_aot is built
add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/wasm-apps/main.aot
  COMMAND ./build_aot.sh
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/build_aot.sh
  COMMENT "Executing script to compile aot files"
  VERBATIM
)

add_custom_target(
  BuildAot ALL
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/wasm-apps/main.aot
)

add_dependencies(runtime_common_test BuildAot)

add_custom_command(TARGET runtime_common_test POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_LIST_DIR}/wasm-apps/main.wasm ${CMAKE_CURRENT_LIST_DIR}/wasm-apps/main.aot
    ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Copy main.wasm and main.aot to the directory: build/runtime-common."
)

gtest_discover_tests(runtime_common_test)

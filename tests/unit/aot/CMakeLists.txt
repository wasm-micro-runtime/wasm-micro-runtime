# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(aot MODES aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

add_definitions (-Dattr_container_malloc=malloc)
add_definitions (-Dattr_container_free=free)
add_definitions (-DWASM_ENABLE_WAMR_COMPILER=1)
add_definitions (-DWASM_ENABLE_DUMP_CALL_STACK=1)
add_definitions (-DWASM_ENABLE_AOT_STACK_FRAME=1)

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)

include (../unit_common.cmake)

include (${IWASM_DIR}/compilation/iwasm_compl.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
     ${UNIT_SOURCE}
     ${WAMR_RUNTIME_LIB_SOURCE}
     ${IWASM_COMPL_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (aot_test ${unit_test_sources})

target_link_libraries (aot_test ${LLVM_AVAILABLE_LIBS} gtest_main )

gtest_discover_tests(aot_test)

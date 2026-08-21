# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping interpreter: classic interpreter only")
  return()
endif()

add_definitions (-Dattr_container_malloc=malloc)
add_definitions (-Dattr_container_free=free)
# add_definitions (-DWASM_ENABLE_WAMR_COMPILER=1)

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 1)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (interpreter_test ${unit_test_sources})

target_link_libraries (interpreter_test gtest_main )

gtest_discover_tests(interpreter_test)

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping shared-utils: classic interpreter only")
  return()
endif()

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_APP_FRAMEWORK 0)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable (shared_utils_test ${unit_test_sources})

target_link_libraries (shared_utils_test gtest_main)

gtest_discover_tests(shared_utils_test)

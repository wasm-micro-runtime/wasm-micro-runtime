# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping wasm-c-api: classic interpreter only")
  return()
endif()

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping wasm-c-api: classic interpreter only")
  return()
endif()

# WAMR features switch
if (NOT DEFINED WAMR_BUILD_TARGET)
  set(WAMR_BUILD_TARGET "X86_64")
endif()
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)

include (../unit_common.cmake)

add_executable(wasm_c_api_test basic.cc ${WAMR_RUNTIME_LIB_SOURCE})
target_link_libraries(wasm_c_api_test gtest_main)
gtest_discover_tests(wasm_c_api_test)

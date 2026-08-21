# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_AOT OR
   WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping wasm-vm: classic interpreter only")
  return()
endif()

set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_MULTI_MODULE 1)

add_definitions (-Dattr_container_malloc=malloc)
add_definitions (-Dattr_container_free=free)

include (../unit_common.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

set (unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(wasm_vm_test ${unit_test_sources})

target_link_libraries(wasm_vm_test ${LLVM_AVAILABLE_LIBS} gtest_main)

add_custom_command(TARGET wasm_vm_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/app4/*.wasm
                ${CMAKE_CURRENT_BINARY_DIR}/
    COMMENT "Copy wasm files to the output directory"
)

gtest_discover_tests(wasm_vm_test)

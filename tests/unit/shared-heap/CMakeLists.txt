# Copyright (C) 2024 Xiaomi Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(test-shared-heap)

add_definitions(-DRUN_ON_LINUX)

set(WAMR_BUILD_APP_FRAMEWORK 0)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_MEMORY64 0)
set(WAMR_BUILD_SHARED_HEAP 1)

# Compile wasm modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-scripts")
find_package(WASISDK REQUIRED)
set(WASI_SDK_DIR "${WASISDK_HOME}")

include(ExternalProject)
ExternalProject_Add(
  shared_heap_wasm_apps
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_ALWAYS YES
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                      -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}
)

ExternalProject_Add(
  shared_heap_wasm_apps_bulk_memory
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/bulk-memory
  BUILD_ALWAYS YES
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/bulk-memory -B build
                      -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}
)

# if only load this CMake other than load it as subdirectory
include(${CMAKE_CURRENT_SOURCE_DIR}/../unit_common.cmake)

include(${IWASM_DIR}/compilation/iwasm_compl.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(UNIT_SOURCE ${source_all})

set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(shared_heap_test ${unit_test_sources})

target_link_libraries(shared_heap_test gtest_main)

# Copy wasm/aot files from ExternalProject build dirs to the test working directory
set(WASM_APPS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/shared_heap_wasm_apps-prefix/src/shared_heap_wasm_apps-build)
add_custom_command(TARGET shared_heap_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${WASM_APPS_BUILD_DIR}/test.wasm
            ${WASM_APPS_BUILD_DIR}/test.aot
            ${WASM_APPS_BUILD_DIR}/test_chain.aot
            ${WASM_APPS_BUILD_DIR}/test_addr_conv.wasm
            ${WASM_APPS_BUILD_DIR}/test_addr_conv.aot
            ${WASM_APPS_BUILD_DIR}/test_addr_conv_chain.aot
            ${WASM_APPS_BUILD_DIR}/test_sandbox.wasm
            ${WASM_APPS_BUILD_DIR}/test_runtime.wasm
            ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Copying wasm/aot test files to test working directory"
)

gtest_discover_tests(shared_heap_test)

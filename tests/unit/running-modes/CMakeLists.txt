# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(test-running-modes)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-scripts")
find_package(WASISDK REQUIRED)
set(WASI_SDK_DIR "${WASISDK_HOME}")

include(ExternalProject)
ExternalProject_Add(
  test-running-modes-wasm-apps
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_ALWAYS YES
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                      -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}
)

add_definitions(-DRUN_ON_LINUX)

set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_MULTI_MODULE 0)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_APP_FRAMEWORK 0)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_INTERP 1)
if(WAMR_BUILD_TARGET STREQUAL "x86_64")
  set(WAMR_BUILD_JIT 1)
  sset(WAMR_BUILD_FAST_JIT 1)
endif()
set(WAMR_BUILD_REF_TYPES 1)

# if only load this CMake other than load it as subdirectory
include(../unit_common.cmake)

find_package(LLVM REQUIRED CONFIG)
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC_LIST)

set(unit_test_sources
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
  ${SRC_LIST}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(wasm_running_modes_test ${unit_test_sources})
target_link_libraries(wasm_running_modes_test ${LLVM_AVAILABLE_LIBS} gtest_main)
add_dependencies(wasm_running_modes_test test-running-modes-wasm-apps)

gtest_discover_tests(wasm_running_modes_test)

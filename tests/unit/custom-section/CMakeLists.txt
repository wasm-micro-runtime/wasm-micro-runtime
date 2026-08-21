# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project (test-custom-section)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-scripts")
find_package(WASISDK REQUIRED)
set(WASI_SDK_DIR "${WASISDK_HOME}")

if(NOT WAMR_BUILD_AOT OR WAMR_BUILD_INTERP OR WAMR_BUILD_FAST_INTERP OR
   WAMR_BUILD_JIT OR WAMR_BUILD_FAST_JIT)
  message(STATUS "Skipping custom-section: classic interpreter and AOT only")
  return()
endif()

include(ExternalProject)
ExternalProject_Add(
  custom-section-test-wasm-apps
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_ALWAYS YES
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                      -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
)

set (WAMR_BUILD_LIBC_WASI 0)
set (WAMR_BUILD_LIBC_BUILTIN 0)

add_definitions(-DWASM_ENABLE_WAMR_COMPILER=1)
add_definitions (-DWASM_ENABLE_DUMP_CALL_STACK=1)
add_definitions (-DWASM_ENABLE_AOT_STACK_FRAME=1)

# Feature to test
set (WAMR_BUILD_LOAD_CUSTOM_SECTION 1)

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
add_executable (custom_section_test ${unit_test_sources})

target_link_libraries (custom_section_test ${LLVM_AVAILABLE_LIBS} gtest_main )

gtest_discover_tests(custom_section_test)

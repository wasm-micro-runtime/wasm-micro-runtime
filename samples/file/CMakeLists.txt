# Copyright (C) 2022 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(file)

################ wasm application ###############
add_subdirectory(src)

# Build the wasm application through the standalone wasm-apps CMake project
# (ExternalProject, so WAMR host flags don't leak into the wasm build).
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../build-scripts)
find_package(WASISDK REQUIRED)
samples_build_wasm_app(file_wasm_apps wasm-apps
  CONFIGURE_ARGS
    -DWASI_SDK_PREFIX=${WASISDK_HOME}
    -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN})

################ tests ###############
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  add_dependencies(iwasm file_wasm_apps)

  samples_add_test(file
    "All the tests passed!"
    $<TARGET_FILE:iwasm> -f wasm-apps/file.wasm -d .)
endif()

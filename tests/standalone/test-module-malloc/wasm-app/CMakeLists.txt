# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(wasm-app)

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
list (APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WASISDK REQUIRED)

if (APPLE)
    set (HAVE_FLAG_SEARCH_PATHS_FIRST 0)
    set (CMAKE_C_LINK_FLAGS "")
    set (CMAKE_CXX_LINK_FLAGS "")
endif ()

set (CMAKE_SYSTEM_PROCESSOR wasm32)

set (CMAKE_C_COMPILER_TARGET        "wasm32")
set (CMAKE_C_COMPILER               "${WASISDK_CC_COMMAND}")

set (CMAKE_EXE_LINKER_FLAGS
    "-Wl,--max-memory=131072 -z stack-size=8192   \
     -Wl,--export=malloc                          \
     -Wl,--export=free                            \
     -Wl,--allow-undefined"
)

add_executable(test.wasm main.c)
target_link_libraries(test.wasm)

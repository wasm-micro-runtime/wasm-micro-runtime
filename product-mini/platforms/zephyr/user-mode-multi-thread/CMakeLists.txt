# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_user_mode_multi_thread LANGUAGES C)

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_LIB_PTHREAD 1)
set(WAMR_ZEPHYR_RUNTIME_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/wamr_runtime.c)
add_compile_definitions(WASM_UINT64_IS_ATOMIC=0)
add_compile_definitions(APP_THREAD_STACK_SIZE_DEFAULT=4096)
add_compile_definitions(APP_THREAD_STACK_SIZE_MIN=4096)

set(WAMR_ZEPHYR_WASM_APP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-app)

add_subdirectory(../user-mode/lib-wamr-zephyr lib-wamr-zephyr)

target_link_libraries(app PRIVATE wamr_lib)
target_sources(app PRIVATE src/main.c)

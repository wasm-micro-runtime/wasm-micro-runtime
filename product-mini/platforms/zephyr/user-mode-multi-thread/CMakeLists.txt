# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_user_mode_multi_thread LANGUAGES C)

# Apply native stack/atomic settings to both app and the runtime module.
zephyr_compile_definitions(WASM_UINT64_IS_ATOMIC=0
                           APP_THREAD_STACK_SIZE_DEFAULT=4096
                           APP_THREAD_STACK_SIZE_MIN=4096)

target_sources(app PRIVATE src/main.c src/wamr_runtime.c)
include(../cmake/wasm_app.cmake)

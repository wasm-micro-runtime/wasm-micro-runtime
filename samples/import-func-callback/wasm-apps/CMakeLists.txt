# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the import-func-callback sample's wasm
# application. Built by the parent project via samples_build_wasm_app()
# (ExternalProject) with the wasi-sdk toolchain; libc-builtin expressed
# through compile/link options.
#
# Tools: wasi-sdk (REQUIRED - missing wasi-sdk makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(import_func_callback_wasm_apps LANGUAGES C)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(CMAKE_SYSROOT "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot")

add_executable(test.wasm test.c)

target_compile_options(test.wasm PRIVATE -nostdlib -Qunused-arguments)
target_link_options(test.wasm PRIVATE
  -nostdlib
  -Wl,--no-entry
  "LINKER:--export=test"
  -Wl,--allow-undefined)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/test.wasm" DESTINATION wasm-apps)

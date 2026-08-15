# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the inst-context sample's wasm application.
# Built by the parent project via samples_build_wasm_app() (ExternalProject)
# with the wasi-sdk toolchain; libc-builtin expressed through compile/link
# options.
#
# Tools: wasi-sdk (REQUIRED - missing wasi-sdk makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(inst_context_wasm_apps LANGUAGES C)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(CMAKE_SYSROOT "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot")

add_executable(testapp.wasm testapp.c)

target_compile_options(testapp.wasm PRIVATE
  -nostdlib -Qunused-arguments -O0 -z stack-size=4096)
target_link_options(testapp.wasm PRIVATE
  -nostdlib
  -Wl,--initial-memory=131072
  -Wl,--no-entry,--strip-all
  -Wl,--allow-undefined-file=${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot/share/defined-symbols.txt
  "LINKER:--export=generate_float"
  "LINKER:--export=float_to_string"
  "LINKER:--export=calculate"
  -Wl,--allow-undefined)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm" DESTINATION wasm-apps)

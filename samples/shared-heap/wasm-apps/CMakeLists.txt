# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the shared-heap sample's wasm applications.
# Built by the parent project via samples_build_wasm_app() (ExternalProject)
# with the wasi-sdk toolchain; libc-builtin expressed through compile/link
# options.
#
# Tools: wasi-sdk (REQUIRED - missing wasi-sdk makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(shared_heap_wasm_apps LANGUAGES C)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(CMAKE_SYSROOT "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot")

function(build_wasm NAME)
  add_executable(${NAME}.wasm ${NAME}.c)
  target_compile_options(${NAME}.wasm PRIVATE
    -nostdlib -Qunused-arguments -O0 -z stack-size=32768)
  target_link_options(${NAME}.wasm PRIVATE
    -nostdlib
    -Wl,--initial-memory=131072
    -Wl,--no-entry,--strip-all
    "LINKER:--export=__heap_base"
    "LINKER:--export=__data_end"
    "LINKER:--export=__wasm_call_ctors"
    "LINKER:--export-all"
    -Wl,--allow-undefined)
endfunction()

build_wasm(test1)
build_wasm(test2)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/test1.wasm
              ${CMAKE_CURRENT_BINARY_DIR}/test2.wasm
        DESTINATION wasm-apps)

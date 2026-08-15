# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the multi-thread sample's wasm applications.
# Built by the parent project via samples_build_wasm_app() (ExternalProject)
# with the wasi-sdk toolchain; libc-builtin expressed through compile/link
# options (pthread model provided by the WAMR lib-pthread).
#
# Tools: wasi-sdk (REQUIRED - missing wasi-sdk makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(multi_thread_wasm_apps LANGUAGES C)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

set(CMAKE_SYSROOT "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot")

function(build_wasm TARGET SOURCE)
  add_executable(${TARGET}.wasm ${SOURCE}.c)
  target_compile_options(${TARGET}.wasm PRIVATE
    -nostdlib -pthread -Qunused-arguments -z stack-size=32768)
  target_link_options(${TARGET}.wasm PRIVATE
    -nostdlib
    -Wl,--shared-memory,--max-memory=131072
    -Wl,--no-entry,--strip-all
    "LINKER:--export=__heap_base"
    "LINKER:--export=__data_end"
    "LINKER:--export=__wasm_call_ctors"
    "LINKER:--export=main"
    "LINKER:--export=__main_argc_argv"
    -Wl,--allow-undefined)
endfunction()

build_wasm(test main)
build_wasm(main_thread_exception main_thread_exception)
build_wasm(main_global_atomic main_global_atomic)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/test.wasm DESTINATION wasm-apps)

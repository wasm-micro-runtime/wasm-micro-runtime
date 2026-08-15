# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the inst-context-threads sample's wasm
# application. Built by the parent project via samples_build_wasm_app()
# (ExternalProject) with the wasi-sdk pthread toolchain (wasi-libc pthread
# model for wasm32-wasi-threads).
#
# Tools: wasi-sdk (REQUIRED - missing wasi-sdk makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(inst_context_threads_wasm_apps LANGUAGES C)

add_executable(testapp.wasm testapp.c)

target_compile_options(testapp.wasm PRIVATE
  -pthread -ftls-model=local-exec)
target_link_options(testapp.wasm PRIVATE
  -pthread
  -z stack-size=32768
  LINKER:--import-memory
  LINKER:--export-memory
  LINKER:--shared-memory,--max-memory=655360
  LINKER:--export=__heap_base
  LINKER:--export=__data_end
)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm" DESTINATION wasm-apps)

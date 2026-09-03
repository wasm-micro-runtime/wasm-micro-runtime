# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(memory64-wasm-apps)

add_executable(mem64.wasm mem64.c)
target_compile_options(mem64.wasm PUBLIC -nostdlib)
target_link_options(mem64.wasm PRIVATE
  -nostdlib
  LINKER:--allow-undefined
  LINKER:--export-all
  LINKER:--no-entry
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/mem64.wasm DESTINATION .)

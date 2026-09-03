# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Standalone project for the WebAssembly module of the sample. It is configured
# by the parent CMakeLists.txt through the wasi-sdk toolchain file, never by the
# Zephyr build directly.

cmake_minimum_required(VERSION 3.14)

project(zephyr_user_mode_wasm C)

add_executable(test_wasm main.c)
set_target_properties(test_wasm PROPERTIES SUFFIX .wasm)

target_compile_options(test_wasm PRIVATE -O3)

# The module is loaded by a libc-builtin runtime: no WASI, no entry point, and
# the linear memory starts at 0 so that the whole 64 KB is usable.
target_link_options(test_wasm PRIVATE
  -nostdlib
  -z stack-size=128
  -Wl,--initial-memory=65536
  -Wl,--global-base=0
  -Wl,--export=main
  -Wl,--export=__main_argc_argv
  -Wl,--export=__data_end
  -Wl,--export=__heap_base
  -Wl,--strip-all
  -Wl,--no-entry
  -Wl,--allow-undefined
)

install(FILES $<TARGET_FILE:test_wasm> DESTINATION .)

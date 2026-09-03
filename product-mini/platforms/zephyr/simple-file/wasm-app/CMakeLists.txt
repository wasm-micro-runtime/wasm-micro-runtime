# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Standalone project for the WebAssembly module of the sample. It is configured
# by the parent CMakeLists.txt through the wasi-sdk toolchain file, never by the
# Zephyr build directly.

cmake_minimum_required(VERSION 3.14)

project(zephyr_simple_file_wasm C)

add_executable(file file.c)
set_target_properties(file PROPERTIES SUFFIX .wasm)

target_compile_options(file PRIVATE -Os)

# The module uses the WASI file system API, so it links against wasi-libc from
# the sysroot the toolchain file already points at.
target_link_options(file PRIVATE
  -nodefaultlibs -lc
  -z stack-size=8192
  -Wl,--initial-memory=65536
  -Wl,--export=__heap_base
  -Wl,--export=__data_end
)

install(FILES $<TARGET_FILE:file> DESTINATION .)

# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Standalone project for the WebAssembly module of the sample. It is configured
# by the parent CMakeLists.txt through the wasi-sdk toolchain file, never by the
# Zephyr build directly.

cmake_minimum_required(VERSION 3.14)

project(zephyr_simple_http_wasm C)

# The socket API the module calls is not part of wasi-libc: it comes from
# lib-socket, whose wasi_socket_ext.c is compiled into the module directly.
set(LIB_SOCKET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core/iwasm/libraries/lib-socket)

add_executable(http_get
  http_get.c
  ${LIB_SOCKET_DIR}/src/wasi/wasi_socket_ext.c
)
set_target_properties(http_get PROPERTIES SUFFIX .wasm)

target_include_directories(http_get PRIVATE ${LIB_SOCKET_DIR}/inc)

target_link_options(http_get PRIVATE
  -nodefaultlibs -lc
  -z stack-size=8192
  -Wl,--initial-memory=65536
  -Wl,--export=__heap_base
  -Wl,--export=__data_end
  -Wl,--allow-undefined
)

install(FILES $<TARGET_FILE:http_get> DESTINATION .)

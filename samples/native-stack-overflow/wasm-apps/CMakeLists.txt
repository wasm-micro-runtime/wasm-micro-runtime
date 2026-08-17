# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the native-stack-overflow sample's wasm
# application and its AOT variants. Built by the parent project via
# samples_build_wasm_app() (ExternalProject) with the wasi-sdk toolchain;
# libc-builtin expressed through compile/link options (reactor model).
#
# Tools: wasi-sdk + wamrc (REQUIRED - missing tools make configure fail).

cmake_minimum_required(VERSION 3.14)
project(native_stack_overflow_wasm_apps LANGUAGES C)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
find_package(WAMRC REQUIRED)

set(CMAKE_SYSROOT "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot")

add_executable(testapp.wasm testapp.c)

target_compile_options(testapp.wasm PRIVATE
  -nostdlib -Qunused-arguments -Os -z stack-size=4096)
target_link_options(testapp.wasm PRIVATE
  -nostdlib
  -Wl,--initial-memory=131072
  -Wl,--no-entry
  -Wl,--allow-undefined
  -mexec-model=reactor)

# AOT variants (plain, and with the signature native library).
# SIGNATURE_SO is passed by the parent project (DEPENDS signature).
if(NOT SIGNATURE_SO)
  message(FATAL_ERROR "Require SIGNATURE_SO (the built signature.so path)")
endif()
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot"
  COMMAND ${WAMRC_BIN} --size-level=0
          -o "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot"
          "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
  DEPENDS testapp.wasm
  COMMENT "Building testapp.wasm.aot"
  VERBATIM
)
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot.signature"
  COMMAND ${WAMRC_BIN} --size-level=0 --native-lib=${SIGNATURE_SO}
          -o "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot.signature"
          "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
  DEPENDS testapp.wasm
  COMMENT "Building testapp.wasm.aot.signature"
  VERBATIM
)
add_custom_target(native_stack_overflow_wasm ALL
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
          "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot"
          "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot.signature")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
              "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot"
              "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm.aot.signature"
        DESTINATION wasm-apps)

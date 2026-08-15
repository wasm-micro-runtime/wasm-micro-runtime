# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the shared-module sample's wasm application.
# Built by the parent project via samples_build_wasm_app() (ExternalProject).
#
# Tools: wabt (REQUIRED - missing wabt makes configure fail).

cmake_minimum_required(VERSION 3.14)
project(shared_module_wasm_apps LANGUAGES NONE)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
find_package(WABT REQUIRED COMPONENTS wat2wasm)

add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
  COMMAND ${WAT2WASM} -o "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm"
          "${CMAKE_CURRENT_SOURCE_DIR}/testapp.wat"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/testapp.wat"
  COMMENT "Building testapp.wasm"
  VERBATIM
)
add_custom_target(shared_module_wasm ALL
  DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/testapp.wasm" DESTINATION wasm-apps)

# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project for the custom-section sample's wasm application
# and its AOT variant. Built by the parent project via
# samples_build_wasm_app() (ExternalProject).
#
# Tools: wasi-sdk + wamrc (REQUIRED - missing tools make configure fail).

cmake_minimum_required(VERSION 3.14)
project(custom_section_wasm_apps LANGUAGES NONE)

set(WAMR_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
find_package(WASISDK REQUIRED)
find_package(WAMRC REQUIRED)

set(WASM_OUT "${CMAKE_CURRENT_BINARY_DIR}/custom_section.wasm")
set(WASM_AOT "${CMAKE_CURRENT_BINARY_DIR}/custom_section.aot")

# Compile the .c and the payload .s in a single clang invocation (the .s
# incbins custom_section_payload.bin with a relative path, so run in the
# source directory).
add_custom_command(
  OUTPUT ${WASM_OUT}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
  COMMAND ${WASISDK_CC_COMMAND}
          --target=wasm32
          -O0
          -nostdlib
          -Wl,--strip-all,--no-entry
          -Wl,--allow-undefined
          -Wl,--export=run_demo
          -o ${WASM_OUT}
          custom_section.c
          custom_section_payload.s
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  DEPENDS custom_section.c custom_section_payload.s custom_section_payload.bin
  COMMENT "Building custom_section.wasm"
  VERBATIM
)

add_custom_command(
  OUTPUT ${WASM_AOT}
  COMMAND ${WAMRC_BIN} --emit-custom-sections=demo
          -o ${WASM_AOT} ${WASM_OUT}
  DEPENDS ${WASM_OUT}
  COMMENT "Building custom_section.aot"
  VERBATIM
)
add_custom_target(custom_section_wasm ALL
  DEPENDS ${WASM_OUT} ${WASM_AOT})

install(FILES ${WASM_OUT} ${WASM_AOT} DESTINATION wasm-apps)

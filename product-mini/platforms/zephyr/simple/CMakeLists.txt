# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr)

# The runtime is built by the wasm-micro-runtime Zephyr module and configured
# through the CONFIG_WAMR_* options in prj.conf. The build target is derived
# from the board architecture, override it with -DWAMR_BUILD_TARGET=<target>.
target_sources(app PRIVATE src/main.c)

################  WebAssembly module  ################
# wasm-app/ is compiled with the wasi-sdk toolchain in a nested build, and the
# resulting test_wasm.wasm is turned into the byte array src/main.c embeds.
# Zephyr defines ZEPHYR_<MODULE>_MODULE_DIR for every module it found, so the
# runtime is located through the module system rather than through a relative
# path reaching outside this directory. See ../README.md, "Which manifest".
set(WAMR_ROOT_DIR ${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR})
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WASISDK REQUIRED)

include(ExternalProject)
ExternalProject_Add(wasm_app
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/wasm-app
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-app -B build
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-app
  BUILD_ALWAYS      TRUE
)

set(WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/test_wasm.h)
add_custom_command(
  OUTPUT  ${WASM_HEADER}
  COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-app/test_wasm.wasm
            -DOUTPUT=${WASM_HEADER}
            -DSYMBOL=wasm_test_file
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/bin_to_header.cmake
  DEPENDS wasm_app
  COMMENT "Generating test_wasm.h from test_wasm.wasm"
  VERBATIM
)
add_custom_target(gen_test_wasm_h DEPENDS ${WASM_HEADER})

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies(app gen_test_wasm_h)

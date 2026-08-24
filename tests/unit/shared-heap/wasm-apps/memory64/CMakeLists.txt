# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(wasm-apps-wasm64)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WAMRC REQUIRED)

# Set WAMRC flags for memory64
set(WAMRC_SHARED_HEAP_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-heap)
set(WAMRC_SHARED_HEAP_CHAIN_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-chain)

#
# C -> Wasm
#
add_executable(test64.wasm ../test.c)
target_compile_options(test64.wasm PUBLIC -nostdlib -O0 -pthread --target=wasm64)
target_link_options(test64.wasm PRIVATE
  -nostdlib
  LINKER:--no-entry
  LINKER:--initial-memory=65536
  LINKER:--export-all
  LINKER:--allow-undefined
)

# Compile AOT files (combined target)
add_custom_target(compile_aot ALL
  COMMAND ${WAMRC_BIN} ${WAMRC_SHARED_HEAP_FLAGS} -o test64.aot test64.wasm
  COMMAND ${WAMRC_BIN} ${WAMRC_SHARED_HEAP_CHAIN_FLAGS} -o test64_chain.aot test64.wasm
  DEPENDS test64.wasm
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Install WASM file
set(WASM_FILES
  ${CMAKE_CURRENT_BINARY_DIR}/test64.wasm
)
install(FILES ${WASM_FILES} DESTINATION .)

# Install AOT files
set(AOT_FILES
  ${CMAKE_CURRENT_BINARY_DIR}/test64.aot
  ${CMAKE_CURRENT_BINARY_DIR}/test64_chain.aot
)
install(FILES ${AOT_FILES} DESTINATION .)

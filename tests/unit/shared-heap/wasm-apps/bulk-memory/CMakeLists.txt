# Copyright (C) 2024 Xiaomi Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(wasm-apps-bulk-memory)

# Find WAMRC
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WAMRC REQUIRED)

# Set architecture-specific WAMRC flags
if (WAMR_BUILD_TARGET STREQUAL "X86_32")
  set(WAMRC_SHARED_HEAP_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-heap --target=i386)
  set(WAMRC_SHARED_HEAP_CHAIN_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-chain --target=i386)
else ()
  set(WAMRC_SHARED_HEAP_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-heap)
  set(WAMRC_SHARED_HEAP_CHAIN_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-chain)
endif ()

# Compile AOT files (combined target)
add_custom_target(compile_aot ALL
  COMMAND ${WAMRC_BIN} ${WAMRC_SHARED_HEAP_FLAGS} -o test_bulk_memory.aot ${CMAKE_CURRENT_SOURCE_DIR}/test_bulk_memory.wasm
  COMMAND ${WAMRC_BIN} ${WAMRC_SHARED_HEAP_CHAIN_FLAGS} -o test_bulk_memory_chain.aot ${CMAKE_CURRENT_SOURCE_DIR}/test_bulk_memory.wasm
  DEPENDS test_bulk_memory.wasm
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Install WASM file
set(WASM_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/test_bulk_memory.wasm
)
install(FILES ${WASM_FILES} DESTINATION .)

# Install AOT files
set(AOT_FILES
  ${CMAKE_CURRENT_BINARY_DIR}/test_bulk_memory.aot
  ${CMAKE_CURRENT_BINARY_DIR}/test_bulk_memory_chain.aot
)
install(FILES ${AOT_FILES} DESTINATION .)

# Copyright (C) 2024 Xiaomi Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
project(wasm-apps)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../..)
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WAMRC REQUIRED)
find_package(WASISDK REQUIRED)

set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_SYSROOT ${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot)

set(CMAKE_C_FLAGS "-nostdlib -Qunused-arguments")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -z stack-size=8192 -nostdlib -O0")
set(CMAKE_C_COMPILER_TARGET "wasm32")
set(CMAKE_C_COMPILER "${WASISDK_CC_COMMAND}")

set(DEFINED_SYMBOLS
        "${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot/share/defined-symbols.txt")

set(CMAKE_EXE_LINKER_FLAGS
        "-Wl,--no-entry           \
      -Wl,--initial-memory=65536  \
      -Wl,--export-all            \
      -Wl,--allow-undefined"
        )

if (WAMR_BUILD_TARGET STREQUAL "X86_32")
  set (WAMR_COMPILER_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-heap --target=i386)
  set (WAMR_COMPILER_CHAIN_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-chain --target=i386)
else ()
  set (WAMR_COMPILER_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-heap)
  set (WAMR_COMPILER_CHAIN_FLAGS --opt-level=3 --bounds-checks=1 --enable-shared-chain)
endif ()

function(copy_wasm TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}
                ${CMAKE_CURRENT_BINARY_DIR}/../
        COMMENT "Copy ${TARGET_NAME} to the same directory of google test"
    )
endfunction()

function(compile_and_copy_aot_from TARGET_NAME)
    string(REPLACE ".wasm" ".aot" AOT_TARGET ${TARGET_NAME})
    string(REPLACE ".wasm" "_chain.aot" AOT_CHAIN_TARGET ${TARGET_NAME})

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${WAMRC_BIN} ${WAMR_COMPILER_FLAGS}
                -o ${AOT_TARGET}
                ${TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_BINARY_DIR}/${AOT_TARGET}
                ${CMAKE_CURRENT_BINARY_DIR}/../
        COMMAND ${WAMRC_BIN} ${WAMR_COMPILER_CHAIN_FLAGS}
                -o ${AOT_CHAIN_TARGET}
                ${TARGET_NAME}
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_CURRENT_BINARY_DIR}/${AOT_CHAIN_TARGET}
                ${CMAKE_CURRENT_BINARY_DIR}/../
        COMMENT "Compile and copy ${AOT_TARGET} to the same directory of google test"
    )
endfunction()

add_executable(test.wasm test.c)
target_link_libraries(test.wasm)
copy_wasm(test.wasm)
compile_and_copy_aot_from(test.wasm)

add_executable(test_addr_conv.wasm test_addr_conv.c)
target_link_libraries(test_addr_conv.wasm)
copy_wasm(test_addr_conv.wasm)
compile_and_copy_aot_from(test_addr_conv.wasm)

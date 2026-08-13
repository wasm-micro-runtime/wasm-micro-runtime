# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.8.2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr)

enable_language (ASM)

set (WAMR_BUILD_PLATFORM "zephyr")

# WAMR Configuration:
set (WAMR_BUILD_TARGET "THUMB")
set (WAMR_BUILD_INTERP 1)
set (WAMR_BUILD_FAST_INTERP 0)
set (WAMR_BUILD_AOT 1)
set (WAMR_BUILD_LIBC_BUILTIN 1) # printf
set (WAMR_BUILD_LIBC_WASI 1)
set (WAMR_BUILD_LIB_PTHREAD 0)
set (WAMR_BUILD_GLOBAL_HEAP_POOL 1)
set (WAMR_BUILD_GLOBAL_HEAP_SIZE 131072) # 128 KB
# set (WAMR_BUILD_GLOBAL_HEAP_SIZE 65536) # 64 KB

# Environment variables:

# Check if WAMR_ROOT_DIR is set 
if(DEFINED ENV{WAMR_ROOT_DIR})
  set(WAMR_ROOT_DIR $ENV{WAMR_ROOT_DIR})
else()
  message(FATAL_ERROR "'WAMR_ROOT_DIR' need to be specified")
endif()
message("wasi-sdk was found at ${WAMR_ROOT_DIR}")

# Discover WASI-SDK through the shared module.
list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
set(WASISDK_ROOT "$ENV{WASI_SDK_PATH}")
find_package(WASISDK REQUIRED)
set(WASI_SDK_PATH "${WASISDK_HOME}")
message("wasi-sdk was found at ${WASI_SDK_PATH}")

# Check if WAMR_APP_FRAMEWORK_DIR is set
if (DEFINED ENV{WAMR_APP_FRAMEWORK_DIR})
  set(WAMR_APP_FRAMEWORK_DIR $ENV{WAMR_APP_FRAMEWORK_DIR})
else()
  message(FATAL_ERROR "'wamr-app-framework' not found, please ensure they are installed.\
                       You can download and install them from\
                       https://github.com/bytecodealliance/wamr-app-framework")
endif()
message("wamr-app-framework was found at ${WAMR_APP_FRAMEWORK_DIR}")

# set the WAMR_SDK_DIR with the path specified in the environment variable
set(WAMR_SDK_DIR
    ${WAMR_APP_FRAMEWORK_DIR}/wamr-sdk
)

# set the WAMR_LIBC_BUILTIN_DIR
set(WAMR_LIBC_BUILTIN_DIR
    ${WAMR_SDK_DIR}/wamr-sdk/app/libc-builtin-sysroot
)

# set the WAMR_SDK_PACKAGE_OUT_DIR 
set(WAMR_SDK_PACKAGE_OUT_DIR
    ${CMAKE_CURRENT_BINARY_DIR}/wamr-sdk/app-sdk/wamr-app-framework
)

# # Reset linker flags
# set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
# set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
# include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake) # in socket-api sample

# Build the WAMR runtime 
target_sources(app PRIVATE
               ${WAMR_RUNTIME_LIB_SOURCE}
               src/main.c)

# Link libraries like in samples.
set(WASI_LIBM "${WASI_SDK_PATH}/share/wasi-sysroot/lib/wasm32-wasi/libm.a")
set(WASI_LIBDL "${WASI_SDK_PATH}/share/wasi-sysroot/lib/wasm32-wasi/libdl.a")

target_link_libraries(app PUBLIC ${WASI_LIBM} ${WASI_LIBDL})

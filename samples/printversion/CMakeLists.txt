# Copyright (C) 2025 Midokura Japan KK.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.16)

project(printversion LANGUAGES C)

################  runtime settings  ################
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if(APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif()

# Build the runtime library (vmlib) from source instead of depending on an
# installed WAMR package, so the sample builds standalone like the others.
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_LIBC_BUILTIN 0)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

add_executable(printversion printversion.c)

target_link_libraries(printversion vmlib -lm -lpthread)

################  tests  ################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  samples_add_test(printversion
    "wasm-micro-runtime [0-9]+\\.[0-9]+\\.[0-9]+"
    $<TARGET_FILE:printversion>)
endif()

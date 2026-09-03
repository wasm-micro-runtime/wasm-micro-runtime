# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)
include(ExternalProject)

project (import-func-callback)

set (CMAKE_CXX_STANDARD 17)

################  runtime settings  ################
string (TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif ()

# Reset default linker flags
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# WAMR features switch

# Set WAMR_BUILD_TARGET, currently values supported:
# "X86_64", "AMD_64", "X86_32", "AARCH64[sub]", "ARM[sub]", "THUMB[sub]",
# "MIPS", "XTENSA", "RISCV64[sub]", "RISCV32[sub]"
if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set (WAMR_BUILD_TARGET "AARCH64")
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set (WAMR_BUILD_TARGET "RISCV64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Build as X86_64 by default in 64-bit platform
    set (WAMR_BUILD_TARGET "X86_64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Build as X86_32 by default in 32-bit platform
    set (WAMR_BUILD_TARGET "X86_32")
  else ()
    message(SEND_ERROR "Unsupported build target platform!")
  endif ()
endif ()

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Debug)
endif ()

set (WAMR_BUILD_INTERP 1)

if (NOT MSVC)
  # linker flags
  if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
  endif ()
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -Wformat-security")
endif ()

# build out vmlib
set (WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
list (APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WASISDK REQUIRED)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

################ wasm application ################
ExternalProject_Add(wasm_app
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${CMAKE_COMMAND} -E env
        ${WASISDK_CC_COMMAND}
        -nostdlib
        --target=wasm32
        -Wl,--no-entry
        -Wl,--export=test
        -Wl,--allow-undefined
        -o ${CMAKE_CURRENT_BINARY_DIR}/wasm-apps/test.wasm
        ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/test.c
    BUILD_ALWAYS TRUE
    INSTALL_COMMAND ""
)

################  application related  ################
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

add_executable (import-func-callback src/main.c ${UNCOMMON_SHARED_SOURCE})

add_dependencies(import-func-callback wasm_app)

check_pie_supported()
set_target_properties (import-func-callback PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (APPLE)
  target_link_libraries (import-func-callback vmlib -lm -ldl -lpthread ${LLVM_AVAILABLE_LIBS})
else ()
  target_link_libraries (import-func-callback vmlib -lm -ldl -lpthread -lrt ${LLVM_AVAILABLE_LIBS})
endif ()

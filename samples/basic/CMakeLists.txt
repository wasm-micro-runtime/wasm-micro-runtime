# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)

if (NOT WAMR_BUILD_PLATFORM STREQUAL "windows")
  project (basic)
else()
  project (basic C ASM)
endif()

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
  set (CMAKE_BUILD_TYPE Release)
endif ()

set (WAMR_BUILD_INTERP 1)
set (WAMR_BUILD_AOT 1)
set (WAMR_BUILD_JIT 0)
set (WAMR_BUILD_LIBC_BUILTIN 1)
# free_buffer_early exercises wasm_runtime_is_underlying_binary_freeable(),
# which requires the fast interpreter.
set (WAMR_BUILD_FAST_INTERP 1)

if (NOT MSVC)
  set (WAMR_BUILD_LIBC_WASI 1)
endif ()

if (NOT MSVC)
  # linker flags
  if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
  endif ()
endif ()

# build out vmlib
set (WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

################  wasm application  ################
# Build wasm-apps/testapp.c through the standalone wasm-apps CMake project
# (ExternalProject, wasi-sdk toolchain). Tools are required: missing wasi-sdk
# makes configure fail.
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)
find_package(WASISDK REQUIRED)
samples_build_wasm_app(basic_wasm_apps wasm-apps
  CONFIGURE_ARGS
    -DWASI_SDK_PREFIX=${WASISDK_HOME}
    -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN})

################  application related  ################
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

add_executable (basic src/main.c src/native_impl.c ${UNCOMMON_SHARED_SOURCE})
add_executable (free_buffer_early src/free_buffer_early.c ${UNCOMMON_SHARED_SOURCE})

check_pie_supported()
set_target_properties (basic PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties (free_buffer_early PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (APPLE)
  target_link_libraries (basic vmlib -lm -ldl -lpthread)
  target_link_libraries (free_buffer_early vmlib -lm -ldl -lpthread)
else ()
  target_link_libraries (basic vmlib -lm -ldl -lpthread -lrt)
  target_link_libraries (free_buffer_early vmlib -lm -ldl -lpthread -lrt)
endif ()

################  tests  ################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  add_dependencies(basic basic_wasm_apps)
  add_dependencies(free_buffer_early basic_wasm_apps)

  samples_add_test(basic_main
    "Native finished calling wasm function: calculate, return: 120"
    $<TARGET_FILE:basic> -f wasm-apps/testapp.wasm)
  samples_add_test(basic_free_buffer_early
    "Able to free wasm binary buffer."
    $<TARGET_FILE:free_buffer_early> -f wasm-apps/testapp.wasm)
endif()

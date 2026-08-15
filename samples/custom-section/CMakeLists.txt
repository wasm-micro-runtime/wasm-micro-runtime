# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

include(CheckPIESupported)

if(NOT WAMR_BUILD_PLATFORM STREQUAL "windows")
  project(custom_section)
else()
  project(custom_section C ASM)
endif()

# ###############  runtime settings  ################
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)

if(APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif()

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

if(NOT DEFINED WAMR_BUILD_TARGET)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set(WAMR_BUILD_TARGET "AARCH64")
  elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set(WAMR_BUILD_TARGET "RISCV64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WAMR_BUILD_TARGET "X86_64")
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(WAMR_BUILD_TARGET "X86_32")
  else()
    message(SEND_ERROR "Unsupported build target platform!")
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LOAD_CUSTOM_SECTION 1)

if(NOT MSVC)
  set(WAMR_BUILD_LIBC_WASI 1)
endif()

if(NOT MSVC)
  if(NOT(CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
  endif()
endif()

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

################  wasm application  ################
# Build wasm-apps/custom_section.wasm (and its AOT variant) through the
# standalone wasm-apps CMake project (ExternalProject). Tools are required:
# missing wasi-sdk/wamrc makes configure fail.
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)
samples_build_wasm_app(custom_section_wasm_apps wasm-apps)

# ###############  application related  ################
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
include(${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

add_executable(custom_section
  src/main.c
  src/native_impl.c
  ${UNCOMMON_SHARED_SOURCE}
)

check_pie_supported()
set_target_properties(custom_section PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  target_link_libraries(custom_section vmlib -lm -ldl -lpthread)
else()
  target_link_libraries(custom_section vmlib -lm -ldl -lpthread -lrt)
endif()

################  tests  ################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  add_dependencies(custom_section custom_section_wasm_apps)

  samples_add_test(custom_section
    "Wasm returned custom section handle: 0"
    $<TARGET_FILE:custom_section> -f wasm-apps/custom_section.wasm)
  samples_add_test(custom_section_aot
    "Wasm returned custom section handle: 0"
    $<TARGET_FILE:custom_section> -f wasm-apps/custom_section.aot)
endif()

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)

project(multi_module)

################  runtime settings  ################
string (TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif ()

# Resetdefault linker flags
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

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

set(WAMR_BUILD_INTERP 1)
# AOT is enabled by default; wamrc is a required tool (missing wamrc makes
# configure fail). Disable with -DWAMR_BUILD_AOT=0.
if (NOT DEFINED WAMR_BUILD_AOT)
  set(WAMR_BUILD_AOT 1)
endif ()
if (NOT DEFINED WAMR_BUILD_JIT)
  set(WAMR_BUILD_JIT 0)
endif ()
if (NOT DEFINED WAMR_BUILD_DUMP_CALL_STACK)
  set(WAMR_BUILD_DUMP_CALL_STACK 0)
endif ()
if (NOT DEFINED WAMR_BUILD_GC)
  set(WAMR_BUILD_GC 0)
endif ()
set(WAMR_BUILD_SIMD 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_MULTI_MODULE 1)

# compiling and linking flags
if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif ()

# build out vmlib
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
################################################

################  application related  ################

################ WASM MODULES
# Build the wasm modules through the standalone wasm-apps CMake project
# (ExternalProject, so WAMR host flags don't leak into the wasm build).
find_package(WASISDK REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)
samples_build_wasm_app(WASM_MODULE wasm-apps
  CONFIGURE_ARGS
    -DWASI_SDK_PREFIX=${WASISDK_HOME}
    -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
    -DCMAKE_SYSROOT=${WASISDK_SYSROOT})

################  WASM MODULES TO AOT
if (WAMR_BUILD_AOT EQUAL 1)
  find_package(WAMRC REQUIRED)
  set(WAMR_COMPILER ${WAMRC_BIN})

  if (WAMR_BUILD_DUMP_CALL_STACK EQUAL 1)
    list(APPEND WAMR_AOT_COMPILE_OPTIONS "--enable-dump-call-stack")
  endif ()
  if (WAMR_BUILD_GC EQUAL 1)
    list(APPEND WAMR_AOT_COMPILE_OPTIONS "--enable-gc")
  endif ()

  add_custom_target(
    wasm_to_aot
    ALL
    DEPENDS
    WASM_MODULE ${WAMR_COMPILER}
    COMMAND
    ${WAMR_COMPILER} ${WAMR_AOT_COMPILE_OPTIONS} -o mA.aot ./mA.wasm
    COMMAND
    ${WAMR_COMPILER} ${WAMR_AOT_COMPILE_OPTIONS} -o mB.aot ./mB.wasm
    COMMAND
    ${WAMR_COMPILER} ${WAMR_AOT_COMPILE_OPTIONS} -o mC.aot ./mC.wasm
    WORKING_DIRECTORY
    ${CMAKE_BINARY_DIR}
  )
endif()

################ NATIVE
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

add_executable(multi_module src/main.c ${UNCOMMON_SHARED_SOURCE})

add_dependencies(multi_module vmlib WASM_MODULE)

check_pie_supported()
set_target_properties (multi_module PROPERTIES POSITION_INDEPENDENT_CODE ON)

# libraries
target_link_libraries(multi_module PRIVATE vmlib -lpthread -lm)

################  tests  ################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  add_dependencies(multi_module WASM_MODULE)
  if(WAMR_BUILD_AOT EQUAL 1)
    add_dependencies(multi_module wasm_to_aot)
  endif()

  set(MULTI_MODULE_PASS
    "call \"C4\", it will call B2\\(\\) of mB and call A1\\(\\) of mA and return 0xb:i32, ===> 0xb:i32")
  samples_add_test(multi_module_interp "${MULTI_MODULE_PASS}"
    $<TARGET_FILE:multi_module> mC.wasm)
  if(WAMR_BUILD_AOT EQUAL 1)
    samples_add_test(multi_module_aot "${MULTI_MODULE_PASS}"
      $<TARGET_FILE:multi_module> mC.aot)
  endif()
endif()

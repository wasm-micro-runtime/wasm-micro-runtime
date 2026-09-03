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
if (NOT DEFINED WAMR_BUILD_AOT)
  set(WAMR_BUILD_AOT 0)
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
include(ExternalProject)

find_package(WASISDK REQUIRED)
set(WASI_SDK_DIR ${WASISDK_HOME})
set(WASI_TOOLCHAIN_FILE ${WASISDK_TOOLCHAIN})
set(WASI_SYS_ROOT ${WASISDK_SYSROOT})

# .c -> .wasm
ExternalProject_Add(WASM_MODULE
  SOURCE_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_ALWAYS TRUE
  UPDATE_COMMAND     ""
  PATCH_COMMAND      ""
  CONFIGURE_COMMAND  ${CMAKE_COMMAND}
                       -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                       -DCMAKE_TOOLCHAIN_FILE=${WASI_TOOLCHAIN_FILE}
                       -DCMAKE_SYSROOT=${WASI_SYS_ROOT}
                       -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                       -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_COMMAND      ${CMAKE_COMMAND} --build .
  INSTALL_COMMAND    ${CMAKE_COMMAND} -E copy
                       ./mA.wasm ${CMAKE_BINARY_DIR}
                       ./mB.wasm ${CMAKE_BINARY_DIR}
                       ./mC.wasm ${CMAKE_BINARY_DIR}
                       ./mD.wasm ${CMAKE_BINARY_DIR}
                       ./mE.wasm ${CMAKE_BINARY_DIR}
)

################ WASM MODULES TO AOT
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

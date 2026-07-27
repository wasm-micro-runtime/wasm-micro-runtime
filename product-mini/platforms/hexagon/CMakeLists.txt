# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)

project (iwasm)

option(BUILD_SHARED_LIBS "Build using shared libraries" OFF)

set (CMAKE_VERBOSE_MAKEFILE OFF)

# Hexagon uses the Linux kernel
set (WAMR_BUILD_PLATFORM "linux")
set (WAMR_BUILD_TARGET "HEXAGON")

# Reset default linker flags
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

set (CMAKE_C_STANDARD 99)
set (CMAKE_CXX_STANDARD 17)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif ()

if (NOT DEFINED WAMR_BUILD_INTERP)
  # Enable Interpreter by default
  set (WAMR_BUILD_INTERP 1)
endif ()

if (NOT DEFINED WAMR_BUILD_AOT)
  # Enable AOT by default
  set (WAMR_BUILD_AOT 1)
endif ()

if (NOT DEFINED WAMR_BUILD_JIT)
  # Disable JIT by default
  set (WAMR_BUILD_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_JIT)
  # Fast JIT not supported on Hexagon
  set (WAMR_BUILD_FAST_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_BUILTIN)
  set (WAMR_BUILD_LIBC_BUILTIN 1)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_WASI)
  set (WAMR_BUILD_LIBC_WASI 1)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_INTERP)
  set (WAMR_BUILD_FAST_INTERP 1)
endif ()

if (NOT DEFINED WAMR_BUILD_MULTI_MODULE)
  set (WAMR_BUILD_MULTI_MODULE 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIB_PTHREAD)
  set (WAMR_BUILD_LIB_PTHREAD 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIB_WASI_THREADS)
  set (WAMR_BUILD_LIB_WASI_THREADS 0)
endif()

if (NOT DEFINED WAMR_BUILD_MINI_LOADER)
  set (WAMR_BUILD_MINI_LOADER 0)
endif ()

if (NOT DEFINED WAMR_BUILD_SIMD)
  set (WAMR_BUILD_SIMD 1)
endif ()

if (NOT DEFINED WAMR_BUILD_REF_TYPES)
  set (WAMR_BUILD_REF_TYPES 1)
endif ()

if (NOT DEFINED WAMR_BUILD_DEBUG_INTERP)
  set (WAMR_BUILD_DEBUG_INTERP 0)
endif ()

if (WAMR_BUILD_DEBUG_INTERP EQUAL 1)
  set (WAMR_BUILD_FAST_INTERP 0)
  set (WAMR_BUILD_MINI_LOADER 0)
  set (WAMR_BUILD_SIMD 0)
endif ()

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

check_pie_supported()

set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

# Share main.c with Linux platform since Hexagon runs Linux kernel
add_executable (iwasm
  ${CMAKE_CURRENT_SOURCE_DIR}/../linux/main.c
  ${UNCOMMON_SHARED_SOURCE}
)

set_version_info (iwasm)

target_link_libraries(iwasm vmlib)

install (TARGETS iwasm DESTINATION bin)

add_library (vmlib ${WAMR_RUNTIME_LIB_SOURCE})

set_version_info (vmlib)

target_include_directories(vmlib INTERFACE
  $<INSTALL_INTERFACE:include>
)

set (WAMR_PUBLIC_HEADERS
  ${WAMR_ROOT_DIR}/core/iwasm/include/wasm_c_api.h
  ${WAMR_ROOT_DIR}/core/iwasm/include/wasm_export.h
  ${WAMR_ROOT_DIR}/core/iwasm/include/lib_export.h
)

set_target_properties (vmlib PROPERTIES
  OUTPUT_NAME iwasm
  PUBLIC_HEADER "${WAMR_PUBLIC_HEADERS}"
)

target_link_libraries (vmlib ${LLVM_AVAILABLE_LIBS} ${UV_A_LIBS} -lm -lpthread)

install (TARGETS vmlib
  EXPORT iwasmTargets
  DESTINATION lib
  PUBLIC_HEADER DESTINATION include
)

install_iwasm_package ()

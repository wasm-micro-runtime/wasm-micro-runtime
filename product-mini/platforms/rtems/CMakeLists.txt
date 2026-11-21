# Copyright (C) 2026 KylinSoft.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

project (iwasm)

if (NOT DEFINED RTEMS_BSP_PATH OR RTEMS_BSP_PATH STREQUAL "")
    message (FATAL_ERROR "RTEMS_BSP_PATH must be set to the installed BSP path")
endif ()

set (WAMR_BUILD_PLATFORM "rtems")

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

if (NOT DEFINED WAMR_BUILD_INTERP)
    set (WAMR_BUILD_INTERP 1)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_INTERP)
    set (WAMR_BUILD_FAST_INTERP 1)
endif ()

if (NOT DEFINED WAMR_BUILD_AOT)
    set (WAMR_BUILD_AOT 1)
endif ()

if (NOT DEFINED WAMR_BUILD_JIT)
    set (WAMR_BUILD_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_JIT)
    set (WAMR_BUILD_FAST_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_BUILTIN)
    set (WAMR_BUILD_LIBC_BUILTIN 1)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_WASI)
    set (WAMR_BUILD_LIBC_WASI 1)
endif ()

if (NOT DEFINED WAMR_BUILD_MULTI_MODULE)
    set (WAMR_BUILD_MULTI_MODULE 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIB_PTHREAD)
    set (WAMR_BUILD_LIB_PTHREAD 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIB_WASI_THREADS)
    set (WAMR_BUILD_LIB_WASI_THREADS 0)
endif ()

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

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set (SHARED_DIR ${WAMR_ROOT_DIR}/core/shared)

include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

include_directories (${RTEMS_BSP_PATH}/lib/include)

add_executable (iwasm main.c ${UNCOMMON_SHARED_SOURCE})

target_link_libraries (iwasm vmlib -lm)

add_library (vmlib ${WAMR_RUNTIME_LIB_SOURCE})

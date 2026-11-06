#
# Copyright (c) 2021, RT-Thread Development Team
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

set(CMAKE_ASM_COMPILER_WORKS 1)
set(WAMR_BUILD_PLATFORM "rt-thread")

macro(wamr_rtthread_default name value)
    if(NOT DEFINED ${name})
        set(${name} ${value})
    endif()
endmacro()

# Product defaults remain overridable by the embedding build.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
wamr_rtthread_default(WAMR_BUILD_TARGET ARM)
wamr_rtthread_default(WAMR_BUILD_INTERP 1)
wamr_rtthread_default(WAMR_BUILD_FAST_INTERP 1)
wamr_rtthread_default(WAMR_BUILD_AOT 0)
wamr_rtthread_default(WAMR_BUILD_JIT 0)
wamr_rtthread_default(WAMR_BUILD_FAST_JIT 0)
wamr_rtthread_default(WAMR_BUILD_LIBC_BUILTIN 1)
wamr_rtthread_default(WAMR_BUILD_LIBC_WASI 0)
wamr_rtthread_default(WAMR_BUILD_MULTI_MODULE 0)
wamr_rtthread_default(WAMR_BUILD_LIB_PTHREAD 0)
wamr_rtthread_default(WAMR_BUILD_LIB_WASI_THREADS 0)
wamr_rtthread_default(WAMR_BUILD_MINI_LOADER 0)
wamr_rtthread_default(WAMR_BUILD_SIMD 0)

option(WAMR_RTT_BUILD_RUNNER "Build the optional RT buffer runner" OFF)
option(WAMR_RTT_BUILD_IWASM "Build the legacy DFS/FinSH iwasm command" OFF)

if(NOT DEFINED WAMR_ROOT_DIR)
    set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)
endif()
# WAMR_DISABLE_* and other core options must be supplied before this include.
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

set(wamr_entry_src "")
if(WAMR_RTT_BUILD_RUNNER)
    list(APPEND wamr_entry_src ${CMAKE_CURRENT_LIST_DIR}/rt_wamr.c)
endif()
if(WAMR_RTT_BUILD_IWASM)
    list(APPEND wamr_entry_src ${CMAKE_CURRENT_LIST_DIR}/iwasm.c)
endif()
set(WAMR_SOURCE ${wamr_entry_src} ${WAMR_RUNTIME_LIB_SOURCE})
include_directories(${CMAKE_CURRENT_LIST_DIR})



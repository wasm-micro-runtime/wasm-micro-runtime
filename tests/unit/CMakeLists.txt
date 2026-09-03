# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(unit-test)
option(FULL_TEST "Build all unit tests including llm-enhanced-test" OFF)

# By default, disable non-classic runtime modes. Users can enable one runtime
# mode by setting the corresponding option.
foreach(_unit_option WAMR_BUILD_FAST_INTERP WAMR_BUILD_JIT
                     WAMR_BUILD_AOT WAMR_BUILD_FAST_JIT)
  if(NOT DEFINED ${_unit_option})
    set(${_unit_option} 0 CACHE BOOL "Unit test runtime mode")
  endif()
endforeach()

if(NOT DEFINED WAMR_BUILD_INTERP)
  set(WAMR_BUILD_INTERP 1 CACHE BOOL "Build classic interpreter")
endif()

unset(_unit_option)

# Yes. To solve the compatibility issue with CMAKE (>= 4.0), we need to update
# our `cmake_minimum_required()` to 3.5. However, there are CMakeLists.txt
# from 3rd parties that we should not alter. Therefore, in addition to
# changing the `cmake_minimum_required()`, we should also add a configuration
# here that is compatible with earlier versions.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

SET(CMAKE_BUILD_TYPE Debug)

set(CMAKE_CXX_STANDARD 17)

#TODO: modularization me
# Detect Hosting target info
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
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

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

if(WAMR_BUILD_TARGET STREQUAL "X86_32")
  # 1) Force -m32
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32" CACHE STRING "" FORCE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32" CACHE STRING "" FORCE)
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32" CACHE STRING "" FORCE)

  # 2) Make CMake prefer i386 libraries
  set(CMAKE_SYSTEM_PROCESSOR i386 CACHE STRING "" FORCE)
  set(CMAKE_LIBRARY_ARCHITECTURE "i386-linux-gnu" CACHE STRING "" FORCE)
endif()

# Fetch Google test
include (FetchContent)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
  FetchContent_Declare (
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )
else()
  FetchContent_Declare (
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  )
endif()

# Prevent overriding the parent project's compiler/linker
# settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch CMocka for C unit tests
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
  FetchContent_Declare(
    cmocka
    URL https://gitlab.com/cmocka/cmocka/-/archive/cmocka-2.0.1/cmocka-cmocka-2.0.1.tar.gz
    URL_HASH SHA256=44a229d3f599a593606d895b175504a642078bec8e8d8a9c30ca49c6605b621d
    DOWNLOAD_EXTRACT_TIMESTAMP ON
  )
else()
  FetchContent_Declare(
    cmocka
    URL https://gitlab.com/cmocka/cmocka/-/archive/cmocka-2.0.1/cmocka-cmocka-2.0.1.tar.gz
    URL_HASH SHA256=44a229d3f599a593606d895b175504a642078bec8e8d8a9c30ca49c6605b621d
  )
endif()
FetchContent_MakeAvailable(cmocka)

include(GoogleTest)
enable_testing()

set(WAMR_UNIT_COMMON_HELPERS_ONLY TRUE)
include(${CMAKE_CURRENT_LIST_DIR}/unit_common.cmake)
unset(WAMR_UNIT_COMMON_HELPERS_ONLY)

# Validate and report the single runtime mode before configuring any suite.
wamr_unit_test_get_current_run_mode(_unit_test_run_mode)
message(STATUS "Unit test runtime mode: ${_unit_test_run_mode}")
unset(_unit_test_run_mode)

set(UNIT_TEST_SUITES
  wasm-vm
  interpreter
  wasm-c-api
  libc-builtin
  shared-utils
  linear-memory-wasm
  linear-memory-aot
  linux-perf
  gc
  exception-handling
  running-modes
  mem-alloc
)
foreach(unit_test_suite IN LISTS UNIT_TEST_SUITES)
  add_subdirectory(${unit_test_suite})
endforeach()

if(FULL_TEST)
  message(STATUS "FULL_TEST=ON: include llm-enhanced-test")
  add_subdirectory(llm-enhanced-test)
else()
  message(STATUS "FULL_TEST=OFF: exclude llm-enhanced-test")
endif()

if(WAMR_BUILD_TARGET STREQUAL "X86_64" OR
   WAMR_BUILD_TARGET STREQUAL "AARCH64")
  add_subdirectory(aot-stack-frame)

  # should enable 32-bit llvm when X86_32
  add_subdirectory (aot)
  add_subdirectory (custom-section)
  add_subdirectory (compilation)

  add_subdirectory (memory64)
  add_subdirectory (shared-heap)

  add_subdirectory (runtime-common)
endif ()

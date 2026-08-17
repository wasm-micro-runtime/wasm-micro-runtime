# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

include(CheckPIESupported)

project(debug_tools_optimized_sample)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/../../build-scripts)

# WAMR features switch
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif ()

set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set (WAMR_BUILD_TARGET "AARCH64")
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set (WAMR_BUILD_TARGET "RISCV64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set (WAMR_BUILD_TARGET "X86_64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set (WAMR_BUILD_TARGET "X86_32")
  else ()
    message(SEND_ERROR "Unsupported build target platform!")
  endif ()
endif ()

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release)
endif ()

option(USE_FAST_INTERP "Build iwasm with fast interpreter instead of classic interpreter" OFF)

set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_LIBC_WASI 1)
if (USE_FAST_INTERP)
  set(WAMR_BUILD_FAST_INTERP 1)
  message(STATUS "Building iwasm with fast interpreter (USE_FAST_INTERP=ON)")
else ()
  set(WAMR_BUILD_FAST_INTERP 0)
  message(STATUS "Building iwasm with classic interpreter (default; pass -DUSE_FAST_INTERP=ON for fast-interp)")
endif ()
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_DUMP_CALL_STACK 1)
# Disable hardware bound checks so OOB memory traps go through the
# interpreter's exception path (which calls SYNC_ALL_TO_FRAME and captures
# frame_ip), instead of the SIGSEGV signal-handler-and-longjmp path
# (which doesn't update frame->ip). Capturing frame_ip is what lets
# addr2line.py recover inline frames for the OOB sample.
set(WAMR_DISABLE_HW_BOUND_CHECK 1)

if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif ()

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

################ wasm application ################
include(ExternalProject)

ExternalProject_Add(wasm-apps
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps"
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}
)

################ wamr runtime ################
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

set (RUNTIME_SOURCE_ALL
    ${CMAKE_CURRENT_LIST_DIR}/../../product-mini/platforms/linux/main.c
    ${UNCOMMON_SHARED_SOURCE}
)
add_executable (iwasm ${RUNTIME_SOURCE_ALL})
check_pie_supported()
set_target_properties (iwasm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(iwasm vmlib -lm -ldl)
add_dependencies(iwasm wasm-apps)

################  tests  ################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

if(BUILD_TESTING)
  add_dependencies(iwasm wasm-apps)

  # Requires binaryen + wasi-sdk >= 29 + wamrc; not run in the default
  # CI/devcontainer environment (see samples/README.md).
  foreach(APP oob stackoverflow)
    foreach(MODE wasm aot)
      samples_add_test(debug_tools_optimized_${APP}_${MODE}
        "PASS \\["
        ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1 BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
        python3 ${CMAKE_CURRENT_LIST_DIR}/verify.py ${APP} ${MODE})
    endforeach()
  endforeach()
endif()

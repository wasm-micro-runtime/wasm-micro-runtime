# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

include(CheckPIESupported)

project(socket_api_sample)

get_filename_component(WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
find_package(WASISDK REQUIRED)
set(WASI_SDK_DIR "${WASISDK_HOME}")
set(WASI_TOOLCHAIN_FILE "${WASISDK_PTHREAD_TOOLCHAIN}")
set(WASI_SYS_ROOT "${WASISDK_SYSROOT}")

message(STATUS "WASI_SDK_DIR is ${WASI_SDK_DIR}")
message(STATUS "WASI_TOOLCHAIN_FILE is ${WASI_TOOLCHAIN_FILE}")
message(STATUS "WASI_SYS_ROOT is ${WASI_SYS_ROOT}")

###############################################################
## Build socket applications of wasm version and native version
###############################################################
include(ExternalProject)

ExternalProject_Add(wasm-app
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src
  UPDATE_COMMAND    ""
  PATCH_COMMAND     ""
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
                      -DWASI_SDK_PREFIX=${WASI_SDK_DIR}
                      -DCMAKE_TOOLCHAIN_FILE=${WASI_TOOLCHAIN_FILE}
                      -DCMAKE_SYSROOT=${WASI_SYS_ROOT}
                      ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src
  BUILD_COMMAND     ${CMAKE_COMMAND} --build .
  INSTALL_COMMAND   ${CMAKE_COMMAND} -E copy
                      addr_resolve.wasm ${CMAKE_BINARY_DIR}
                      tcp_client.wasm ${CMAKE_BINARY_DIR}
                      tcp_server.wasm ${CMAKE_BINARY_DIR}
                      send_recv.wasm ${CMAKE_BINARY_DIR}
                      socket_opts.wasm ${CMAKE_BINARY_DIR}
                      udp_client.wasm ${CMAKE_BINARY_DIR}
                      udp_server.wasm ${CMAKE_BINARY_DIR}
                      multicast_client.wasm ${CMAKE_BINARY_DIR}
                      multicast_server.wasm ${CMAKE_BINARY_DIR}
                      timeout_client.wasm ${CMAKE_BINARY_DIR}
                      timeout_server.wasm ${CMAKE_BINARY_DIR}
)

add_executable(tcp_server ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/tcp_server.c)
target_link_libraries(tcp_server pthread)

add_executable(tcp_client ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/tcp_client.c)

add_executable(send_recv ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/send_recv.c)
target_link_libraries(send_recv pthread)

add_executable(addr_resolve ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/addr_resolve.c)

add_executable(socket_opts ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/socket_opts.c)

add_executable(udp_client ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/udp_client.c)

add_executable(udp_server ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/udp_server.c)

add_executable(multicast_client ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/multicast_client.c)

add_executable(multicast_server ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/multicast_server.c)

add_executable(timeout_client ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/timeout_client.c)

add_executable(timeout_server ${CMAKE_CURRENT_SOURCE_DIR}/wasm-src/timeout_server.c)

############################################
## Build iwasm with wasi and pthread support
############################################
string (TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
if (APPLE)
  add_definitions(-DBH_PLATFORM_DARWIN)
endif ()

# Reset linker flags
set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

# Set WAMR features

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
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_BUILD_LIB_WASI_THREADS 1)

# compiling and linking flags
if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
  set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
endif ()

# build vmlib static lib
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

# build iwasm
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)
set (RUNTIME_SOURCE_ALL
    ${CMAKE_CURRENT_LIST_DIR}/../../product-mini/platforms/linux/main.c
    ${UNCOMMON_SHARED_SOURCE}
)
add_executable (iwasm ${RUNTIME_SOURCE_ALL})
check_pie_supported()
set_target_properties (iwasm PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(iwasm vmlib -lpthread -lm -ldl)

############################################
## Tests
############################################
include(CTest)
include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)

# Multicast and DNS scenarios are environment-sensitive (multicast group
# membership, external DNS) and are NOT run by default. They can be enabled
# with -DSOCKET_API_ENABLE_EXTRA_TESTS=ON.
option(SOCKET_API_ENABLE_EXTRA_TESTS "Enable multicast/DNS socket-api tests" OFF)

if(BUILD_TESTING)
  add_dependencies(iwasm wasm-app)

  set(SOCKET_API_PY ${CMAKE_CURRENT_LIST_DIR}/socket_test.py)

  # Single-process scenarios (send/receive and socket options).
  samples_add_test(socket_api_send_recv
    "Send 106 bytes successfully!;Receive 106 bytes successfully!"
    $<TARGET_FILE:iwasm> --addr-pool=127.0.0.1/0 ./send_recv.wasm)
  samples_add_test(socket_api_socket_opts
    "getsockopt SO_RCVTIMEO result is expected;\\[Client\\] Close sockets"
    $<TARGET_FILE:iwasm> ./socket_opts.wasm)

  # Two-process scenarios driven by socket_test.py (starts the server wasm,
  # runs the client, kills the server).
  samples_add_test(socket_api_tcp
    "PASS \\[socket-api tcp\\]"
    ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
    python3 ${SOCKET_API_PY} tcp ${CMAKE_CURRENT_BINARY_DIR})
  samples_add_test(socket_api_udp
    "PASS \\[socket-api udp\\]"
    ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
    python3 ${SOCKET_API_PY} udp ${CMAKE_CURRENT_BINARY_DIR})
  samples_add_test(socket_api_timeout
    "PASS \\[socket-api timeout\\]"
    ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
    python3 ${SOCKET_API_PY} timeout ${CMAKE_CURRENT_BINARY_DIR})

  set_tests_properties(socket_api_tcp socket_api_udp socket_api_timeout
    PROPERTIES RUN_SERIAL TRUE)

  if(SOCKET_API_ENABLE_EXTRA_TESTS)
    # multicast / addr_resolve are excluded from the default test set.
    samples_add_test(socket_api_multicast
      "PASS \\[socket-api multicast\\]"
      ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
      python3 ${SOCKET_API_PY} multicast ${CMAKE_CURRENT_BINARY_DIR})
    samples_add_test(socket_api_addr_resolve
      "PASS \\[socket-api addr_resolve\\]"
      ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
      python3 ${SOCKET_API_PY} addr_resolve ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(socket_api_multicast socket_api_addr_resolve
      PROPERTIES RUN_SERIAL TRUE)
  endif()
endif()

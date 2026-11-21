# Copyright (C) 2026 KylinSoft.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_RTEMS)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${PLATFORM_SHARED_DIR}/../include)

include (${CMAKE_CURRENT_LIST_DIR}/../common/posix/platform_api_posix.cmake)

set (source_all
    ${PLATFORM_SHARED_DIR}/rtems_platform.c)

if (WAMR_BUILD_LIBC_WASI EQUAL 1)
    list(APPEND source_all
        ${PLATFORM_SHARED_DIR}/rtems_at_shim.c)
endif ()

if (WAMR_BUILD_LIBC_WASI EQUAL 1 OR WAMR_BUILD_DEBUG_INTERP EQUAL 1)
    list(APPEND source_all
        ${PLATFORM_SHARED_DIR}/rtems_socket_stub.c)
endif ()

set (PLATFORM_SHARED_SOURCE ${source_all} ${PLATFORM_COMMON_POSIX_SOURCE})

list(REMOVE_ITEM PLATFORM_SHARED_SOURCE
    ${PLATFORM_COMMON_POSIX_DIR}/posix_memmap.c
    ${PLATFORM_COMMON_POSIX_DIR}/posix_socket.c
    ${PLATFORM_COMMON_POSIX_DIR}/posix_time.c)

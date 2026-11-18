# Copyright (c) 2021, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})
add_definitions(-DBH_PLATFORM_RTT)
include_directories(${PLATFORM_SHARED_DIR} ${PLATFORM_SHARED_DIR}/../include)

# Feature macros come from config_common.cmake, not platform-local overrides.
set(PLATFORM_SHARED_SOURCE
    ${PLATFORM_SHARED_DIR}/rtt_platform.c
    ${PLATFORM_SHARED_DIR}/rtt_thread.c)

if(WAMR_BUILD_LIBC_WASI EQUAL 1)
    message(WARNING "RT-Thread WASI backend is experimental and needs dedicated validation")
    include(${PLATFORM_SHARED_DIR}/../common/libc-util/platform_common_libc_util.cmake)
    list(APPEND PLATFORM_SHARED_SOURCE
        ${PLATFORM_SHARED_DIR}/rtt_file.c
        ${PLATFORM_SHARED_DIR}/rtt_socket.c
        ${PLATFORM_SHARED_DIR}/../common/posix/posix_file.c
        ${PLATFORM_COMMON_LIBC_UTIL_SOURCE})
endif()


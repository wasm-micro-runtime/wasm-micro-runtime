# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# QNX 8.0 platform (2026, Selman Altinay): derived from platform/freebsd.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_QNX)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${PLATFORM_SHARED_DIR}/../include)

include (${CMAKE_CURRENT_LIST_DIR}/../common/posix/platform_api_posix.cmake)

file (GLOB_RECURSE source_all ${PLATFORM_SHARED_DIR}/*.c)

set (PLATFORM_SHARED_SOURCE ${source_all} ${PLATFORM_COMMON_POSIX_SOURCE})

file (GLOB header ${PLATFORM_SHARED_DIR}/../include/*.h)
LIST (APPEND RUNTIME_LIB_HEADER_LIST ${header})

# On QNX the BSD socket API (socket, getaddrinfo, inet_pton, ...) lives in
# libsocket rather than libc, and the math functions in libm. pthreads,
# dlopen and clock functions are in libc, so no -lpthread/-ldl/-lrt is needed.
link_libraries (socket m)

# Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# core/iwasm/libraries/libc-wasi-p2/libc-wasi-p2.cmake
#
# Defines variables for the iwasm_libc_wasi_p2 library.
# This file does not create any targets.

# Set the directory for this module
set(LIBC_WASI_P2_DIR ${CMAKE_CURRENT_LIST_DIR})

# Gather all source files for the library
file(GLOB LIBC_WASI_P2_SOURCE
    "${LIBC_WASI_P2_DIR}/*.c"
)
list(APPEND LIBC_WASI_P2_SOURCE
    "${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src/random.c"
    "${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util/libc_errno.c"
)

# Define the list of include directories needed to compile the library
set(LIBC_WASI_P2_INCLUDE_DIRS
    ${LIBC_WASI_P2_DIR}
    ${LIBC_WASI_P2_DIR}/../../include
    ${WAMR_ROOT_DIR}/core/iwasm/include
    ${WAMR_ROOT_DIR}/core/shared/platform/include
    ${WAMR_ROOT_DIR}/core/shared/utils
    ${WAMR_ROOT_DIR}/core/shared/mem-alloc
    ${WAMR_ROOT_DIR}/core/shared/platform/linux
    ${WAMR_ROOT_DIR}/core/iwasm/interpreter
    ${WAMR_ROOT_DIR}/core/iwasm/libraries/lib-socket/src/wasi
    ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include
    ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src
    ${WAMR_ROOT_DIR}/core/shared/platform/common/libc-util
    # Note: OpenSSL include is handled at the target level in the root CMakeLists
)

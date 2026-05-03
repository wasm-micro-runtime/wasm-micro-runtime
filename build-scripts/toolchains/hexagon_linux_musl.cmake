# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# CMake toolchain file for cross-compiling to Hexagon Linux (musl)
# Requires CodeLinaro hexagon-unknown-linux-musl toolchain and clang-22/lld-22.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR hexagon)

set(CMAKE_C_COMPILER hexagon-unknown-linux-musl-clang)
set(CMAKE_CXX_COMPILER hexagon-unknown-linux-musl-clang++)
set(CMAKE_ASM_COMPILER hexagon-unknown-linux-musl-clang)
set(CMAKE_AR llvm-ar-22)
set(CMAKE_RANLIB llvm-ranlib-22)

set(CMAKE_C_FLAGS_INIT "-mv68 -G0")
set(CMAKE_CXX_FLAGS_INIT "-mv68 -G0")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

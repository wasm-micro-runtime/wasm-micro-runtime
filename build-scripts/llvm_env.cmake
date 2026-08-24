# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Shared LLVM environment setup for the main build
# (build-scripts/config_common.cmake, JIT section), the unit tests
# (tests/unit/unit_common.cmake, aot/jit runtime modes) and the
# standalone wamr-compiler (wamr-compiler/CMakeLists.txt).
#
# config_common.cmake only sets up the LLVM environment when JIT is
# enabled, while the unit tests also need it in aot mode (their aot
# suites compile the in-process AOT compiler), so this block used to be
# duplicated in the three files.  It must be included in every directory
# scope that links LLVM: it sets directory-scoped variables (LLVM_DIR,
# CMAKE_PREFIX_PATH) and directory properties (include directories,
# definitions) that do not propagate to sibling scopes (each unit-test
# suite is a separate subdirectory).

if (NOT DEFINED LLVM_DIR)
  # wamr-compiler is a standalone project and does not define
  # WAMR_ROOT_DIR; derive it from this file's location.
  if (NOT DEFINED WAMR_ROOT_DIR)
    set (WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
  endif ()
  # wamr-compiler can use a custom LLVM (WAMR_BUILD_WITH_CUSTOM_LLVM=1),
  # in which case the user supplies LLVM_DIR or CMAKE_PREFIX_PATH and the
  # WAMR bundled LLVM build is not probed here.
  if (NOT WAMR_BUILD_WITH_CUSTOM_LLVM)
    set (LLVM_SRC_ROOT "${WAMR_ROOT_DIR}/core/deps/llvm")
    set (LLVM_BUILD_ROOT "${LLVM_SRC_ROOT}/build")
    if (NOT EXISTS "${LLVM_BUILD_ROOT}")
        message (FATAL_ERROR "Cannot find LLVM dir: ${LLVM_BUILD_ROOT}")
    endif ()
    set (CMAKE_PREFIX_PATH "${LLVM_BUILD_ROOT};${CMAKE_PREFIX_PATH}")
    set (LLVM_DIR ${LLVM_BUILD_ROOT}/lib/cmake/llvm)
  endif ()
endif ()

# The LLVM build's own dependencies (zlib, zstd, ...) are resolved by
# find_package(LLVM) itself: LLVMConfig.cmake looks them up when the LLVM
# build was configured with LLVM_ENABLE_ZLIB / LLVM_ENABLE_ZSTD, and
# LLVMExports fails with "The link interface of target LLVMSupport
# contains: <dep> but the target was not found" when a dependency is
# missing.  Do not pre-check them here: distro zstd/zlib development
# packages often lack a CMake config file (e.g. Ubuntu's libzstd-dev),
# so an up-front find_package would only add misleading diagnostics.
find_package(LLVM REQUIRED CONFIG)
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20)

project(minimum C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WAMR_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# A running mode has to be picked, otherwise runtime_lib.cmake refuses to
# configure.  Leave the choice to the caller when there is one -- the presets in
# CMakePresets.json are the whole point of this sample, and an AOT-only preset
# must not get the interpreter forced back on.
if (NOT DEFINED WAMR_BUILD_INTERP AND NOT DEFINED WAMR_BUILD_AOT)
  set(WAMR_BUILD_INTERP 1)
endif ()
include(${WAMR_ROOT}/build-scripts/runtime_lib.cmake)
add_library(vmlib ${WAMR_RUNTIME_LIB_SOURCE})

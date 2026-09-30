# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
project(wamr_failed_join_guest C)

# Reuse the sample's builtin-pthread guest toolchain and linker settings.
include(${CMAKE_CURRENT_LIST_DIR}/../../wasm-app/CMakeLists.txt)

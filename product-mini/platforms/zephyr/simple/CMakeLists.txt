# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr)

# The runtime is built by the wasm-micro-runtime Zephyr module and configured
# through the CONFIG_WAMR_* options in prj.conf. The build target is derived
# from the board architecture, override it with -DWAMR_BUILD_TARGET=<target>.
target_sources(app PRIVATE src/main.c)

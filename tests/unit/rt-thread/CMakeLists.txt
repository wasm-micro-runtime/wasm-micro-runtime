# Copyright (C) 2026, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

# Host-only regressions; also runnable without the other unit-test dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(rtthread-host-tests C)
  enable_testing()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME rt-thread-config
  COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_LIST_DIR}/test_config.py")
set_tests_properties(rt-thread-config PROPERTIES LABELS "rt-thread;host" TIMEOUT 60)

if(NOT CMAKE_CROSSCOMPILING AND NOT MSVC AND
   CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
  add_test(NAME rt-thread-osal
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_LIST_DIR}/run_host.py"
            --cc "${CMAKE_C_COMPILER}")
  set_tests_properties(rt-thread-osal PROPERTIES LABELS "rt-thread;host" TIMEOUT 60)
else()
  message(STATUS "RT-Thread OSAL host test requires a native GNU/Clang driver; skipped")
endif()

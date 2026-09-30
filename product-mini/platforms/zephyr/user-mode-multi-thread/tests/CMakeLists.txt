# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
project(wamr_guest_cleanup_test C)

find_package(Threads REQUIRED)
enable_testing()
add_executable(guest_cleanup test_guest_cleanup.c ../wasm-app/main.c)
set_source_files_properties(../wasm-app/main.c PROPERTIES
  COMPILE_DEFINITIONS main=wamr_guest_main)
target_link_libraries(guest_cleanup PRIVATE Threads::Threads)
target_link_options(guest_cleanup PRIVATE
  -Wl,--wrap=pthread_create
  -Wl,--wrap=pthread_join
  -Wl,--wrap=pthread_mutex_destroy
  -Wl,--wrap=pthread_cond_destroy)
add_test(NAME first_worker_failure COMMAND guest_cleanup 1)
add_test(NAME second_worker_failure COMMAND guest_cleanup 2)
set_tests_properties(first_worker_failure second_worker_failure
  PROPERTIES TIMEOUT 5)

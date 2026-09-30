# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.20.0)
project(zephyr_user_mode_pthreads C)

# WAMR's builtin pthread ABI differs from WASI libc's types and entry points.
# Configure the toolchain normally, then select WAMR's headers for this guest.
set(CMAKE_SYSROOT ${WAMR_ROOT_DIR}/wamr-sdk/app/libc-builtin-sysroot)
add_executable(test_wasm main.c)
set_target_properties(test_wasm PROPERTIES SUFFIX .wasm)
target_compile_options(test_wasm PRIVATE -O2 -pthread)
target_link_options(test_wasm PRIVATE
  -nostdlib -pthread
  -z stack-size=4096
  -Wl,--initial-memory=65536
  -Wl,--max-memory=131072
  -Wl,--shared-memory
  -Wl,--no-entry
  -Wl,--strip-all
  -Wl,--export=main
  -Wl,--export=__main_argc_argv
  -Wl,--export=__data_end
  -Wl,--export=__heap_base
  -Wl,--allow-undefined
)
install(FILES $<TARGET_FILE:test_wasm> DESTINATION .)

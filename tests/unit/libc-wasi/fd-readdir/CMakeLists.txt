# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# GNU ld / lld (ELF) support -Wl,--wrap; Apple ld does not.
if(APPLE OR NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  message(STATUS "libc-wasi/fd-readdir: skipped, linker lacks --wrap")
  return()
endif()

wamr_unit_test_suite_run_modes(libc-wasi/fd-readdir MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_LIBC_WASI 1)

include(../../unit_common.cmake)

# posix.c is #included by the test translation unit, so it must not also be
# compiled through the runtime library.
set(libc_wasi_fd_lib_sources ${WAMR_RUNTIME_LIB_SOURCE})
list(FILTER libc_wasi_fd_lib_sources EXCLUDE
     REGEX ".*sandboxed-system-primitives/src/posix\\.c$")

add_executable(libc-wasi-fd-readdir-test
  fd_readdir_test.c
  ${libc_wasi_fd_lib_sources}
)

target_include_directories(libc-wasi-fd-readdir-test PRIVATE
  ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/src
  ${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi/sandboxed-system-primitives/include
)

# Stub OS and runtime dependencies; see the __wrap_* definitions in the test.
target_link_options(libc-wasi-fd-readdir-test PRIVATE
  -Wl,--wrap=os_fstat
  -Wl,--wrap=os_isatty
  -Wl,--wrap=os_file_get_access_mode
  -Wl,--wrap=os_close
  -Wl,--wrap=os_fdopendir
  -Wl,--wrap=os_closedir
  -Wl,--wrap=os_readdir
  -Wl,--wrap=wasm_runtime_malloc
  -Wl,--wrap=wasm_runtime_free
  -Wl,--wrap=wasm_runtime_begin_blocking_op
  -Wl,--wrap=wasm_runtime_end_blocking_op
)

target_link_libraries(libc-wasi-fd-readdir-test cmocka::cmocka m)

add_test(NAME libc-wasi-fd-readdir-test COMMAND libc-wasi-fd-readdir-test)
set_tests_properties(libc-wasi-fd-readdir-test PROPERTIES TIMEOUT 60)

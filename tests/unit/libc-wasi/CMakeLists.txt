# Copyright (C) 2026 WAMR Community.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(libc-wasi MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_LIBC_WASI 1)

include (../unit_common.cmake)

set (unit_test_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/libc_wasi_test.cc
  ${WAMR_RUNTIME_LIB_SOURCE}
)

add_executable (libc_wasi_test ${unit_test_sources})

target_link_libraries (libc_wasi_test gtest_main)

gtest_discover_tests (libc_wasi_test)

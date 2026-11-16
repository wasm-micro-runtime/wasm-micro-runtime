# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(libc-wasi/double-fetch MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# The iovec cases call the libc-wasi fd and socket APIs. The thread case runs
# a wasi-threads module, which also enables shared memory and the thread
# manager.
set(WAMR_BUILD_LIBC_WASI 1)
set(WAMR_BUILD_LIB_WASI_THREADS 1)

include(../../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
    ${source_all}
    ${WAMR_RUNTIME_LIB_SOURCE}
    ${UNCOMMON_SHARED_SOURCE}
)

add_executable(libc_wasi_iovec_test ${unit_test_sources})
target_link_libraries(libc_wasi_iovec_test ${LLVM_AVAILABLE_LIBS} gtest_main -lm -ldl -lpthread)

wamr_unit_test_compile_wat_to_wasm(
  TARGET libc_wasi_iovec_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/iovec_ops.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/iovec_ops.wasm
)

wamr_unit_test_compile_wat_to_wasm(
  TARGET libc_wasi_iovec_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/iovec_threads.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/iovec_threads.wasm
)

wamr_unit_test_compile_wat_to_wasm(
  TARGET libc_wasi_iovec_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/addr_ops.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/addr_ops.wasm
)

wamr_unit_test_compile_wat_to_wasm(
  TARGET libc_wasi_iovec_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/addr_threads.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/addr_threads.wasm
)

gtest_discover_tests(libc_wasi_iovec_test PROPERTIES TIMEOUT 120)

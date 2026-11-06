# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(posix-thread/base MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

include(../../unit_common.cmake)

# unit_common.cmake sets HW bound check macros for the whole directory;
# this suite picks its own values on the target below.
remove_definitions(-DWASM_DISABLE_HW_BOUND_CHECK=0
                   -DWASM_DISABLE_STACK_HW_BOUND_CHECK=0
                   -DWASM_DISABLE_HW_BOUND_CHECK=1
                   -DWASM_DISABLE_STACK_HW_BOUND_CHECK=1)

set(PLAT ${WAMR_ROOT_DIR}/core/shared/platform)

add_executable(posix-thread-base-test
  test_runner.c
  ${PLAT}/common/posix/posix_thread.c
  ${PLAT}/linux/platform_init.c
  ${PLAT}/common/posix/posix_memmap.c
)

target_include_directories(posix-thread-base-test PRIVATE
  ${PLAT}/include
  ${PLAT}/linux
  ${PLAT}/common/posix
  ${PLAT}/common
)

target_compile_definitions(posix-thread-base-test PRIVATE
  WASM_DISABLE_HW_BOUND_CHECK=1
  WASM_DISABLE_STACK_HW_BOUND_CHECK=1
)

target_link_libraries(posix-thread-base-test cmocka::cmocka pthread m)

add_test(NAME posix-thread-base-test COMMAND posix-thread-base-test)
set_tests_properties(posix-thread-base-test PROPERTIES TIMEOUT 60)

# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(posix-thread/hw-bound-check MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

# platform_internal.h defines OS_ENABLE_HW_BOUND_CHECK only for these targets.
if(NOT (WAMR_BUILD_TARGET STREQUAL "X86_64" OR
        WAMR_BUILD_TARGET STREQUAL "AARCH64"))
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

add_executable(posix-thread-hwbc-test
  test_runner.c
  ${PLAT}/common/posix/posix_thread.c
  ${PLAT}/linux/platform_init.c
  ${PLAT}/common/posix/posix_memmap.c
)

target_include_directories(posix-thread-hwbc-test PRIVATE
  ${PLAT}/include
  ${PLAT}/linux
  ${PLAT}/common/posix
  ${PLAT}/common
)

target_compile_definitions(posix-thread-hwbc-test PRIVATE
  WASM_DISABLE_HW_BOUND_CHECK=0
  WASM_DISABLE_STACK_HW_BOUND_CHECK=0
)

target_link_libraries(posix-thread-hwbc-test cmocka::cmocka pthread m)

add_test(NAME posix-thread-hwbc-test COMMAND posix-thread-hwbc-test)
set_tests_properties(posix-thread-hwbc-test PROPERTIES TIMEOUT 60)

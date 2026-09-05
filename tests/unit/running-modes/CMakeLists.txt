# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(running-modes
  MODES classic-interp fast-interp llvm-jit
)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../build-scripts")
find_package(WASISDK REQUIRED)

# Fast-interp and fast-jit runtime behavior is only exercised on X86_64
# unit-test builds (see .github/workflows/unit_test.yml); skip the suite
# on other targets for these modes to avoid untested combinations.
if(NOT WAMR_BUILD_TARGET STREQUAL "X86_64")
  if(WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_FAST_JIT)
    message(STATUS "Skipping running-modes: fast-interp and fast-jit are not supported")
    return()
  endif()
endif()

include(ExternalProject)
ExternalProject_Add(
  test-running-modes-wasm-apps
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps
  BUILD_ALWAYS YES
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps -B build
                      -DWASI_SDK_PREFIX=${WASISDK_HOME}
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}
)

# The wasm-apps fixtures (mytest.c/hello.c) call printf/malloc/snprintf/
# free, which resolve to the libc-builtin native functions, so the suite
# unconditionally needs LIBC_BUILTIN.
set(WAMR_BUILD_LIBC_BUILTIN 1)

# if only load this CMake other than load it as subdirectory
include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SRC_LIST)

set(unit_test_sources
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
  ${SRC_LIST}
)

# Now simply link against gtest or gtest_main as needed. Eg
add_executable(wasm_running_modes_test ${unit_test_sources})
target_link_libraries(wasm_running_modes_test ${LLVM_AVAILABLE_LIBS} gtest_main)
add_dependencies(wasm_running_modes_test test-running-modes-wasm-apps)

gtest_discover_tests(wasm_running_modes_test)

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(runtime-common MODES classic-interp aot)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set(WAMR_BUILD_MULTI_MODULE 1)

include(../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${IWASM_DIR}/aot)

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set(unit_test_sources
  ${source_all}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(runtime_common_test ${unit_test_sources})
target_link_libraries(runtime_common_test ${LLVM_AVAILABLE_LIBS} gtest_main)

# main.wasm has no .wat/.c source to compile it from; main.aot is generated
# from it with wamrc at build time, because an AOT file is tied to both the
# target arch and the AOT format version, so it cannot be checked in.  Both
# are read relative to the test working directory.
wamr_unit_test_copy_wasm_files(runtime_common_test
  FILES ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/main.wasm
  DEST_DIR ${CMAKE_CURRENT_BINARY_DIR}
)

# module_malloc.wasm exports malloc/free for the wasm_runtime_module_malloc()
# and wasm_runtime_module_free() guest-offset tests.
wamr_unit_test_compile_wat_to_wasm(
  TARGET runtime_common_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/module_malloc.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc.wasm
)

# module_malloc_offset.wasm takes its offset from guest memory instead of the
# size argument, and can grow the memory inside malloc.
wamr_unit_test_compile_wat_to_wasm(
  TARGET runtime_common_test
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/module_malloc_offset.wat
  OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc_offset.wasm
)

if(WAMR_BUILD_AOT)
  wamr_unit_test_compile_wasm_to_aot(
    TARGET runtime_common_test
    INPUT ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/main.wasm
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.aot
  )

  # module_malloc.aot is the same module as module_malloc.wasm, compiled
  # with wamrc.
  wamr_unit_test_compile_wasm_to_aot(
    TARGET runtime_common_test
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc.wasm
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc.aot
  )

  wamr_unit_test_compile_wasm_to_aot(
    TARGET runtime_common_test
    INPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc_offset.wasm
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/module_malloc_offset.aot
  )
endif()

gtest_discover_tests(runtime_common_test)

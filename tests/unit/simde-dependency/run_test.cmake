# Copyright (C) 2026 Ray Wong.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(prefix "${TEST_BINARY_DIR}/prefix")
set(override "${TEST_BINARY_DIR}/override")
set(fetch_base "${TEST_BINARY_DIR}/deps")

if(NOT TEST_CASE STREQUAL "cached_fetch")
  file(MAKE_DIRECTORY "${prefix}/include/simde/wasm")
  file(WRITE "${prefix}/include/simde/wasm/simd128.h" "#define TEST_SOURCE 1\n")
  if(TEST_CASE STREQUAL "old_local")
    file(WRITE "${prefix}/include/simde/simde-common.h"
      "#define SIMDE_VERSION_MAJOR 0\n#define SIMDE_VERSION_MINOR 7\n#define SIMDE_VERSION_MICRO 4\n")
  else()
    file(WRITE "${prefix}/include/simde/simde-common.h"
      "#define SIMDE_VERSION_MAJOR 0\n#define SIMDE_VERSION_MINOR 8\n#define SIMDE_VERSION_MICRO 2\n")
  endif()
  set(expected 1)
  if(TEST_CASE STREQUAL "manual")
    list(APPEND options "-DWAMR_SIMDE_INCLUDE_DIR=${prefix}/include")
  else()
    list(APPEND options "-DCMAKE_PREFIX_PATH=${prefix}")
  endif()
endif()

if(TEST_CASE STREQUAL "override")
  file(MAKE_DIRECTORY "${override}/simde/wasm")
  file(WRITE "${override}/simde/wasm/simd128.h" "#define TEST_SOURCE 2\n")
  set(expected 2)
  list(APPEND options "-DFETCHCONTENT_SOURCE_DIR_SIMDE=${override}")
elseif(TEST_CASE STREQUAL "cached_fetch" OR TEST_CASE STREQUAL "old_local")
  file(MAKE_DIRECTORY "${fetch_base}/simde-src/simde/wasm" "${TEST_BINARY_DIR}/empty-root")
  file(WRITE "${fetch_base}/simde-src/simde/wasm/simd128.h" "#define TEST_SOURCE 3\n")
  set(expected 3)
  list(APPEND options "-DFETCHCONTENT_BASE_DIR=${fetch_base}")
  if(TEST_CASE STREQUAL "cached_fetch")
    list(APPEND options
      "-DCMAKE_FIND_ROOT_PATH=${TEST_BINARY_DIR}/empty-root"
      "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY"
    )
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -S "${TEST_SOURCE_DIR}" -B "${TEST_BINARY_DIR}/build"
    "-DSIMDE_TEST_EXPECTED=${expected}" -DFETCHCONTENT_FULLY_DISCONNECTED=ON ${options}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SIMDe ${TEST_CASE} configure failed:\n${output}\n${error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${TEST_BINARY_DIR}/build"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "SIMDe ${TEST_CASE} build failed:\n${output}\n${error}")
endif()

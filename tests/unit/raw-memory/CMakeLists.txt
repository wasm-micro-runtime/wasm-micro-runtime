# Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(test-raw-memory)

add_definitions(-DRUN_ON_LINUX)

# Engine profile (override from CLI / matrix script):
#   fast_jit     — classic interp + Fast-JIT (default)
#   fast_interp  — fast interpreter only
#   memory64     — classic interp + Memory64 (no Fast-JIT)
set(RAW_MEMORY_TEST_PROFILE "fast_jit" CACHE STRING
    "Raw unit-test engine profile: fast_jit|fast_interp|memory64")

set(WAMR_BUILD_APP_FRAMEWORK 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_SHARED_HEAP 0)
set(WAMR_BUILD_RAW_MEMORY 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_WAMR_COMPILER 0)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_BULK_MEMORY_OPT 1)

if (RAW_MEMORY_TEST_PROFILE STREQUAL "fast_interp")
  set(WAMR_BUILD_AOT 0)
  set(WAMR_BUILD_FAST_JIT 0)
  set(WAMR_BUILD_FAST_INTERP 1)
  set(WAMR_BUILD_MEMORY64 0)
elseif (RAW_MEMORY_TEST_PROFILE STREQUAL "memory64")
  set(WAMR_BUILD_AOT 1)
  set(WAMR_BUILD_FAST_JIT 0)
  set(WAMR_BUILD_FAST_INTERP 0)
  set(WAMR_BUILD_MEMORY64 1)
else ()
  # fast_jit (default)
  set(WAMR_BUILD_AOT 1)
  set(WAMR_BUILD_FAST_JIT 1)
  set(WAMR_BUILD_FAST_INTERP 1) # forced off by Fast-JIT in config_common
  set(WAMR_BUILD_MEMORY64 0)
endif ()

message(STATUS "Raw memory unit-test profile: ${RAW_MEMORY_TEST_PROFILE}")

# Standalone builds need gtest; when included from tests/unit parent it
# is already available.
if (NOT TARGET gtest_main)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  include(GoogleTest)
  enable_testing()
endif ()

# unit_common.cmake insists on an LLVM_DIR; point at system LLVM when the
# WAMR-bundled tree is absent (standalone Fast-JIT/interp harness).
if (NOT DEFINED LLVM_DIR AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../core/deps/llvm/build")
  if (EXISTS /usr/lib/llvm-18/lib/cmake/llvm)
    set(LLVM_DIR /usr/lib/llvm-18/lib/cmake/llvm)
  endif ()
endif ()

include(${CMAKE_CURRENT_SOURCE_DIR}/../unit_common.cmake)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

file(GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)
list(FILTER source_all EXCLUDE REGEX "raw_memory_bench\\.cc$")
set(UNIT_SOURCE ${source_all})

set(unit_test_sources
  ${UNIT_SOURCE}
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)

add_executable(raw_memory_test ${unit_test_sources})
target_link_libraries(raw_memory_test gtest_main)

add_executable(raw_memory_bench
  ${CMAKE_CURRENT_SOURCE_DIR}/raw_memory_bench.cc
  ${WAMR_RUNTIME_LIB_SOURCE}
  ${UNCOMMON_SHARED_SOURCE}
)
target_link_libraries(raw_memory_bench pthread m dl)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(RAW_MEM_TEST_WASM ${CMAKE_CURRENT_BINARY_DIR}/raw_mem_test.wasm)
set(RAW_MEM_BENCH_WASM ${CMAKE_CURRENT_BINARY_DIR}/raw_mem_bench.wasm)
add_custom_command(
  OUTPUT ${RAW_MEM_TEST_WASM}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_test_wasm.py
          ${RAW_MEM_TEST_WASM}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_test_wasm.py
  COMMENT "Generate raw_mem_test.wasm fixture"
)
add_custom_command(
  OUTPUT ${RAW_MEM_BENCH_WASM}
  COMMAND ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_bench_wasm.py
          ${RAW_MEM_BENCH_WASM}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_bench_wasm.py
  COMMENT "Generate raw_mem_bench.wasm fixture"
)
add_custom_target(raw_mem_test_wasm DEPENDS ${RAW_MEM_TEST_WASM})
add_custom_target(raw_mem_bench_wasm DEPENDS ${RAW_MEM_BENCH_WASM})
add_dependencies(raw_memory_test raw_mem_test_wasm)
add_dependencies(raw_memory_bench raw_mem_bench_wasm)

if (WAMR_BUILD_MEMORY64 EQUAL 1)
  set(RAW_MEM64_TEST_WASM ${CMAKE_CURRENT_BINARY_DIR}/raw_mem64_test.wasm)
  add_custom_command(
    OUTPUT ${RAW_MEM64_TEST_WASM}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_test_wasm.py
            ${RAW_MEM64_TEST_WASM}
            --mem64
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/wasm-apps/gen_raw_mem_test_wasm.py
    COMMENT "Generate raw_mem64_test.wasm fixture"
  )
  add_custom_target(raw_mem64_test_wasm DEPENDS ${RAW_MEM64_TEST_WASM})
  add_dependencies(raw_memory_test raw_mem64_test_wasm)
endif ()

# Optional AOT fixture via wamrc --enable-raw-memory (fast_jit / memory64 profiles).
option(RAW_MEMORY_TEST_BUILD_AOT "Build wamrc and compile Raw AOT fixture" ON)
if (RAW_MEMORY_TEST_BUILD_AOT AND WAMR_BUILD_AOT EQUAL 1)
  set(WAMRC_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/wamrc-build)
  set(WAMRC_BIN ${WAMRC_BUILD_DIR}/wamrc)
  set(RAW_MEM_TEST_AOT ${CMAKE_CURRENT_BINARY_DIR}/raw_mem_test.aot)

  # Single producer for nested wamrc — both AOT fixtures must share it so
  # parallel make does not double-configure the same build tree.
  add_custom_command(
    OUTPUT ${WAMRC_BIN}
    COMMAND ${CMAKE_COMMAND}
            -S ${CMAKE_CURRENT_SOURCE_DIR}/../../../wamr-compiler
            -B ${WAMRC_BUILD_DIR}
            -DCMAKE_BUILD_TYPE=Release
            -DWAMR_BUILD_PLATFORM=linux
            -DWAMR_BUILD_WITH_CUSTOM_LLVM=1
            -DLLVM_DIR=${LLVM_DIR}
    # Nested archive builds race under high -j (ranlib/libvmlib.a); keep serial.
    COMMAND ${CMAKE_COMMAND} --build ${WAMRC_BUILD_DIR} --target wamrc -j1
    COMMENT "Build wamrc with Raw memory support"
  )
  add_custom_target(raw_memory_wamrc DEPENDS ${WAMRC_BIN})

  add_custom_command(
    OUTPUT ${RAW_MEM_TEST_AOT}
    COMMAND ${WAMRC_BIN} --enable-raw-memory
            -o ${RAW_MEM_TEST_AOT}
            ${RAW_MEM_TEST_WASM}
    DEPENDS ${RAW_MEM_TEST_WASM}
    COMMENT "AOT-compile raw_mem_test.wasm with --enable-raw-memory"
  )
  add_custom_target(raw_mem_test_aot DEPENDS ${RAW_MEM_TEST_AOT})
  add_dependencies(raw_mem_test_aot raw_memory_wamrc)
  add_dependencies(raw_memory_test raw_mem_test_aot)
  target_compile_definitions(raw_memory_test PRIVATE RAW_MEMORY_HAS_AOT_FIXTURE=1)

  # Bench AOT fixtures: Raw-specialized vs sandbox codegen for host_churn.
  set(RAW_MEM_BENCH_AOT_RAW ${CMAKE_CURRENT_BINARY_DIR}/raw_mem_bench_raw.aot)
  set(RAW_MEM_BENCH_AOT_SBX ${CMAKE_CURRENT_BINARY_DIR}/raw_mem_bench_sandbox.aot)
  add_custom_command(
    OUTPUT ${RAW_MEM_BENCH_AOT_RAW}
    COMMAND ${WAMRC_BIN} --enable-raw-memory
            -o ${RAW_MEM_BENCH_AOT_RAW}
            ${RAW_MEM_BENCH_WASM}
    DEPENDS ${RAW_MEM_BENCH_WASM}
    COMMENT "AOT-compile bench wasm with --enable-raw-memory"
  )
  add_custom_command(
    OUTPUT ${RAW_MEM_BENCH_AOT_SBX}
    COMMAND ${WAMRC_BIN}
            -o ${RAW_MEM_BENCH_AOT_SBX}
            ${RAW_MEM_BENCH_WASM}
    DEPENDS ${RAW_MEM_BENCH_WASM}
    COMMENT "AOT-compile bench wasm (sandbox)"
  )
  add_custom_target(raw_mem_bench_aot
    DEPENDS ${RAW_MEM_BENCH_AOT_RAW} ${RAW_MEM_BENCH_AOT_SBX})
  add_dependencies(raw_mem_bench_aot raw_memory_wamrc)
  add_dependencies(raw_memory_bench raw_mem_bench_aot)
  target_compile_definitions(raw_memory_bench PRIVATE RAW_MEMORY_HAS_AOT_FIXTURE=1)

  if (WAMR_BUILD_MEMORY64 EQUAL 1)
    set(RAW_MEM64_TEST_AOT ${CMAKE_CURRENT_BINARY_DIR}/raw_mem64_test.aot)
    add_custom_command(
      OUTPUT ${RAW_MEM64_TEST_AOT}
      COMMAND ${WAMRC_BIN} --enable-raw-memory
              -o ${RAW_MEM64_TEST_AOT}
              ${RAW_MEM64_TEST_WASM}
      DEPENDS ${RAW_MEM64_TEST_WASM}
      COMMENT "AOT-compile raw_mem64_test.wasm with --enable-raw-memory"
    )
    add_custom_target(raw_mem64_test_aot DEPENDS ${RAW_MEM64_TEST_AOT})
    add_dependencies(raw_mem64_test_aot raw_memory_wamrc)
    add_dependencies(raw_memory_test raw_mem64_test_aot)
    target_compile_definitions(raw_memory_test PRIVATE RAW_MEMORY_HAS_AOT64_FIXTURE=1)
  endif ()
endif ()

gtest_discover_tests(raw_memory_test)

# Convenience: build+run all engine profiles from a configured tree.
add_custom_target(raw_memory_test_matrix
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_matrix.sh
          ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/_matrix
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  USES_TERMINAL
  COMMENT "Build and run Raw memory tests for fast_jit, fast_interp, memory64"
)

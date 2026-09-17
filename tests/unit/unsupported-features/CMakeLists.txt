# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(unsupported_features_tests C)

# This suite compiles and runs nothing.  Every case re-configures this very
# directory with one unsupported flag combination and expects cmake to fail.
#
# What it exercises is build-scripts/unsupported_combination.cmake, which
# runtime_lib.cmake includes, so this includes runtime_lib.cmake directly
# instead of ../unit_common.cmake: the unit test helpers bring in gtest, a
# running mode of their own and the whole vmlib source list, none of which a
# configure-time check needs, and their choice of running mode would decide the
# outcome of the cases here.  It is a standalone project:
#
#     cmake -S . -B build && ctest --test-dir build
#
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)

# A fake target to satisfy runtime_lib.cmake's requirement for a running mode.
if(NOT DEFINED WAMR_BUILD_INTERP AND NOT DEFINED WAMR_BUILD_AOT)
  set(WAMR_BUILD_INTERP 1)
endif()

include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

enable_testing()

# Define a function to add tests with unsupported features
function(add_unsupported_feature_test test_name flags)
  add_test(
    NAME ${PROJECT_NAME}.verify_${test_name}
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_LIST_DIR} -B build_${test_name} ${flags} --fresh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(${PROJECT_NAME}.verify_${test_name} PROPERTIES WILL_FAIL TRUE)
endfunction()

# List of unsupported feature tests
set(UNSUPPORTED_FEATURE_TESTS
  "exce_handling_aot -DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_AOT=1"
  "exce_handling_fast_interp -DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "exce_handling_fast_jit -DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_FAST_JIT=1"
  "exce_handling_llvm_jit -DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_JIT=1"
  "gc_fast_jit -DWAMR_BUILD_GC=1 -DWAMR_BUILD_FAST_JIT=1"
  "lime1_spec1 -DWAMR_BUILD_LIME1=1 -DWAMR_BUILD_WASM_SPEC1=1 -DWAMR_BUILD_INTERP=1"
  "lime1_spec2 -DWAMR_BUILD_LIME1=1 -DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  "memory64_fast_interp -DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "memory64_fast_jit -DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_FAST_JIT=1"
  "memory64_llvm_jit -DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_JIT=1"
  "multi_memory_aot -DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_AOT=1"
  "multi_memory_fast_interp -DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "multi_memory_fast_jit -DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_FAST_JIT=1"
  "multi_memory_llvm_jit -DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_JIT=1"
  "multi_module_fast_jit -DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_FAST_JIT=1"
  "multi_module_llvm_jit -DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_JIT=1"
  "simd_classic_interp -DWAMR_BUILD_SIMD=1 -DWAMR_BUILD_INTERP=1"
  "spec2_classic_interp -DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  # The GC/stringref chain: a later link cannot be asked for without its predecessor.
  "stringref_without_gc -DWAMR_BUILD_STRINGREF=1 -DWAMR_BUILD_GC=0 -DWAMR_BUILD_INTERP=1"
  "stringref_impl_without_stringref -DWAMR_STRINGREF_IMPL_SOURCE=STUB -DWAMR_BUILD_STRINGREF=0 -DWAMR_BUILD_INTERP=1"
  "simd_fast_jit -DWAMR_BUILD_SIMD=1 -DWAMR_BUILD_FAST_JIT=1"
  # Each preset is a complete feature set, so they do not stack.
  "spec1_spec2 -DWAMR_BUILD_WASM_SPEC1=1 -DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  "wasm_spec3 -DWAMR_BUILD_WASM_SPEC3=1 -DWAMR_BUILD_INTERP=1"
)

# Add each test using the function
foreach(test ${UNSUPPORTED_FEATURE_TESTS})
  # string -> list by replacing space with ;
  string(REPLACE " " ";" test_parts "${test}")
  # list[0]
  list(GET test_parts 0 test_name)
  # list[1:]
  list(REMOVE_AT test_parts 0)
  # pass list to cmake and let cmake split it
  set(flags ${test_parts})

  add_unsupported_feature_test(${test_name} "${flags}")
endforeach()

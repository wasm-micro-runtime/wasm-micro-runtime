# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

project(unsupported_features_tests C)

# This suite compiles and runs nothing.  Every case re-configures this very
# directory with one unsupported flag combination, expects cmake to fail, and
# expects the failure to carry the diagnostic that combination is supposed to
# produce, so an unrelated failure (no LLVM, a dependency that cannot be
# fetched) fails the case instead of passing it.
#
# The cases that ask for LLVM JIT need an LLVM installation, and the ones that
# ask for fast JIT need the asmjit sources reachable; run them where those are
# available.
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
#
# The expected diagnostic is what separates a rejected configuration from a
# build that failed for an unrelated reason -- a missing LLVM, a dependency that
# could not be fetched, a broken toolchain.
#
# PASS_REGULAR_EXPRESSION replaces the exit code as the pass criterion (CTest
# documents it that way, and WILL_FAIL would invert the match instead of adding
# to it), so the case passes exactly when the diagnostic it expects is printed,
# whether the configure failed early or late.
function(add_unsupported_feature_test test_name expected flags)
  add_test(
    NAME ${PROJECT_NAME}.verify_${test_name}
    COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_LIST_DIR} -B build_${test_name} ${flags} --fresh
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(${PROJECT_NAME}.verify_${test_name} PROPERTIES
    PASS_REGULAR_EXPRESSION "${expected}"
  )
endfunction()

# Each case is "name|expected diagnostic|flags".  The diagnostic is a regular
# expression, so a literal '+' is written "\\+"; everything else in these
# messages is literal.
set(UNSUPPORTED_FEATURE_TESTS
  "exce_handling_aot|Unsupported build configuration: EXCE_HANDLING \\+ AOT|-DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_AOT=1"
  "exce_handling_fast_interp|Unsupported build configuration: EXCE_HANDLING \\+ FAST_INTERP|-DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "exce_handling_fast_jit|Unsupported build configuration: EXCE_HANDLING \\+ FAST_JIT|-DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_FAST_JIT=1"
  "exce_handling_llvm_jit|Unsupported build configuration: EXCE_HANDLING \\+ AOT|-DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_JIT=1"
  "gc_fast_jit|Unsupported build configuration: GC \\+ FAST_JIT|-DWAMR_BUILD_GC=1 -DWAMR_BUILD_FAST_JIT=1"
  "lime1_spec1|WAMR_BUILD_LIME1 and the WAMR_BUILD_WASM_SPEC|-DWAMR_BUILD_LIME1=1 -DWAMR_BUILD_WASM_SPEC1=1 -DWAMR_BUILD_INTERP=1"
  "lime1_spec2|WAMR_BUILD_LIME1 and the WAMR_BUILD_WASM_SPEC|-DWAMR_BUILD_LIME1=1 -DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  "memory64_fast_interp|Unsupported build configuration: MEMORY64 \\+ FAST_INTERP|-DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "memory64_fast_jit|Unsupported build configuration: MEMORY64 \\+ FAST_JIT|-DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_FAST_JIT=1"
  "memory64_llvm_jit|Unsupported build configuration: MEMORY64 \\+ JIT|-DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_JIT=1"
  "multi_memory_aot|Unsupported build configuration: MULTI_MEMORY \\+ AOT|-DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_AOT=1"
  "multi_memory_fast_interp|Unsupported build configuration: MULTI_MEMORY \\+ FAST_INTERP|-DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
  "multi_memory_fast_jit|Unsupported build configuration: MULTI_MEMORY \\+ FAST_JIT|-DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_FAST_JIT=1"
  # LLVM JIT implies AOT, and the AOT check comes first for these two features,
  # so the diagnostic names AOT rather than JIT.
  "multi_memory_llvm_jit|Unsupported build configuration: MULTI_MEMORY \\+ AOT|-DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_JIT=1"
  "multi_module_fast_jit|Unsupported build configuration: MULTI_MODULE \\+ FAST_JIT|-DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_FAST_JIT=1"
  "multi_module_llvm_jit|Unsupported build configuration: MULTI_MODULE \\+ JIT|-DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_JIT=1"
  "simd_classic_interp|Unsupported build configuration: SIMD \\+ CLASSIC_INTERP|-DWAMR_BUILD_SIMD=1 -DWAMR_BUILD_INTERP=1"
  "spec2_classic_interp|Unsupported build configuration: SIMD \\+ CLASSIC_INTERP|-DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  # The GC/stringref chain: a later link cannot be asked for without its predecessor.
  "stringref_without_gc|WAMR_BUILD_STRINGREF=1 needs WAMR_BUILD_GC=1|-DWAMR_BUILD_STRINGREF=1 -DWAMR_BUILD_GC=0 -DWAMR_BUILD_INTERP=1"
  "stringref_impl_without_stringref|WAMR_STRINGREF_IMPL_SOURCE is set but WAMR_BUILD_STRINGREF is|-DWAMR_STRINGREF_IMPL_SOURCE=STUB -DWAMR_BUILD_STRINGREF=0 -DWAMR_BUILD_INTERP=1"
  "simd_fast_jit|Unsupported build configuration: SIMD \\+ FAST_JIT|-DWAMR_BUILD_SIMD=1 -DWAMR_BUILD_FAST_JIT=1"
  # Each preset is a complete feature set, so they do not stack.
  "spec1_spec2|WAMR_BUILD_WASM_SPEC1 and WAMR_BUILD_WASM_SPEC2 are mutually exclusive|-DWAMR_BUILD_WASM_SPEC1=1 -DWAMR_BUILD_WASM_SPEC2=1 -DWAMR_BUILD_INTERP=1"
  "wasm_spec3|WAMR_BUILD_WASM_SPEC3 is not supported|-DWAMR_BUILD_WASM_SPEC3=1 -DWAMR_BUILD_INTERP=1"
)

# Add each test using the function
foreach(test ${UNSUPPORTED_FEATURE_TESTS})
  # "name|expected|flags" -> list
  string(REPLACE "|" ";" test_parts "${test}")
  list(GET test_parts 0 test_name)
  list(GET test_parts 1 expected)
  list(GET test_parts 2 flag_string)
  # flags: string -> list, cmake splits it for the re-configure
  string(REPLACE " " ";" flags "${flag_string}")

  add_unsupported_feature_test(${test_name} "${expected}" "${flags}")
endforeach()

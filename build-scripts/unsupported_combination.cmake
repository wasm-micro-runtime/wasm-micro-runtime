# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(CMakePrintHelpers)

# Define a function to check for unsupported combinations
function(check_aot_mode_error error_message)
  if(WAMR_BUILD_AOT EQUAL 1)
    message(FATAL_ERROR "${error_message}")
  endif()
endfunction()

# Define a function to check for unsupported combinations with CLASSIC_INTERP
function(check_classic_interp_error error_message)
  # Usually, Enable INTERP to enable wasm loader for JIT
  # WAMR_BUILD_JIT might be undefined, so check it first
  if(WAMR_BUILD_JIT EQUAL 1)
    return()
  endif()

  if(WAMR_BUILD_FAST_JIT EQUAL 1)
    return()
  endif()

  if(WAMR_BUILD_INTERP EQUAL 1 AND WAMR_BUILD_FAST_INTERP EQUAL 0)
    message(FATAL_ERROR "${error_message}")
  endif()
endfunction()

# Define a function to check for unsupported combinations with FAST_INTERP
function(check_fast_interp_error error_message)
  # Usually, Enable INTERP to enable wasm loader for JIT
  # WAMR_BUILD_JIT might be undefined, so check it first
  if(WAMR_BUILD_JIT EQUAL 1)
    return()
  endif()

  if(WAMR_BUILD_FAST_JIT EQUAL 1)
    return()
  endif()

  if(WAMR_BUILD_INTERP EQUAL 1 AND WAMR_BUILD_FAST_INTERP EQUAL 1)
    message(FATAL_ERROR "${error_message}")
  endif()
endfunction()

# Define a function to check for unsupported combinations with FAST_JIT
function(check_fast_jit_error error_message)
  if(WAMR_BUILD_FAST_JIT EQUAL 1)
    message(FATAL_ERROR "${error_message}")
  endif()
endfunction()

# Define a function to check for unsupported combinations with LLVM_JIT
function(check_llvm_jit_error error_message)
  if(WAMR_BUILD_JIT EQUAL 1)
    message(FATAL_ERROR "${error_message}")
  endif()
endfunction()

# Below are the unsupported combinations checks
# Please keep this list in sync with tests/unit/unsupported-features/CMakeLists.txt
# and tests/wamr-test-suites/test_wamr.sh

if(WAMR_BUILD_EXCE_HANDLING EQUAL 1)
  check_aot_mode_error("Unsupported build configuration: EXCE_HANDLING + AOT")
  # FAST_INTERP + EXCE_HANDLING is supported for *throw-only* shapes:
  # WASM modules that declare tags and execute throw / rethrow without
  # ever entering a same-function try / catch handler. The throw
  # propagates to the caller via the existing got_exception bailout
  # path, exactly like any other trap. This covers Porffor (its
  # JS-to-wasm compiler emits 0 try/catch handlers; every JS throw
  # escapes to the host). Modules that contain WASM_OP_TRY / CATCH /
  # CATCH_ALL / DELEGATE still load, but those handlers report
  # "unsupported opcode" at runtime — see the WASM_OP_TRY handler in
  # core/iwasm/interpreter/wasm_interp_fast.c. Full same-function
  # try / catch lowering is the natural follow-up.
  check_fast_jit_error("Unsupported build configuration: EXCE_HANDLING + FAST_JIT")
  check_llvm_jit_error("Unsupported build configuration: EXCE_HANDLING + JIT")
endif()

if(WAMR_BUILD_GC EQUAL 1)
  check_fast_jit_error("Unsupported build configuration: GC + FAST_JIT")
endif()

if(WAMR_BUILD_MEMORY64 EQUAL 1)
  check_fast_interp_error("Unsupported build configuration: MEMORY64 + FAST_INTERP")
  check_fast_jit_error("Unsupported build configuration: MEMORY64 + FAST_JIT")
  check_llvm_jit_error("Unsupported build configuration: MEMORY64 + JIT")
endif()

if(WAMR_BUILD_MULTI_MEMORY EQUAL 1)
  check_aot_mode_error("Unsupported build configuration: MULTI_MEMORY + AOT")
  check_fast_interp_error("Unsupported build configuration: MULTI_MEMORY + FAST_INTERP")
  check_fast_jit_error("Unsupported build configuration: MULTI_MEMORY + FAST_JIT")
  check_llvm_jit_error("Unsupported build configuration: MULTI_MEMORY + JIT")
endif()

if(WAMR_BUILD_MULTI_MODULE EQUAL 1)
  check_fast_jit_error("Unsupported build configuration: MULTI_MODULE + FAST_JIT")
  check_llvm_jit_error("Unsupported build configuration: MULTI_MODULE + JIT")
endif()

if(WAMR_BUILD_SHARED_HEAP EQUAL 1)
  check_fast_jit_error("Unsupported build configuration: SHARED_HEAP + FAST_JIT")
endif()

if(WAMR_BUILD_SIMD EQUAL 1)
  check_classic_interp_error("Unsupported build configuration: SIMD + CLASSIC_INTERP")
  check_fast_jit_error("Unsupported build configuration: SIMD + FAST_JIT")
endif()

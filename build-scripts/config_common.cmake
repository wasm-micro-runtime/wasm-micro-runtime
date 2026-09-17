# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

string(TOUPPER ${WAMR_BUILD_TARGET} WAMR_BUILD_TARGET)

# Add definitions for the build target
if (WAMR_BUILD_TARGET STREQUAL "X86_64")
  add_definitions(-DBUILD_TARGET_X86_64)
elseif (WAMR_BUILD_TARGET STREQUAL "AMD_64")
  add_definitions(-DBUILD_TARGET_AMD_64)
elseif (WAMR_BUILD_TARGET STREQUAL "X86_32")
  add_definitions(-DBUILD_TARGET_X86_32)
elseif (WAMR_BUILD_TARGET MATCHES "ARM.*")
  if (WAMR_BUILD_TARGET MATCHES "(ARM.*)_VFP")
    add_definitions(-DBUILD_TARGET_ARM_VFP)
    add_definitions(-DBUILD_TARGET="${CMAKE_MATCH_1}")
  else ()
    add_definitions(-DBUILD_TARGET_ARM)
    add_definitions(-DBUILD_TARGET="${WAMR_BUILD_TARGET}")
  endif ()
elseif (WAMR_BUILD_TARGET MATCHES "THUMB.*")
  if (WAMR_BUILD_TARGET MATCHES "(THUMB.*)_VFP")
    add_definitions(-DBUILD_TARGET_THUMB_VFP)
    add_definitions(-DBUILD_TARGET="${CMAKE_MATCH_1}")
  else ()
    add_definitions(-DBUILD_TARGET_THUMB)
    add_definitions(-DBUILD_TARGET="${WAMR_BUILD_TARGET}")
  endif ()
elseif (WAMR_BUILD_TARGET MATCHES "AARCH64.*")
  add_definitions(-DBUILD_TARGET_AARCH64)
  add_definitions(-DBUILD_TARGET="${WAMR_BUILD_TARGET}")
elseif (WAMR_BUILD_TARGET STREQUAL "MIPS")
  add_definitions(-DBUILD_TARGET_MIPS)
elseif (WAMR_BUILD_TARGET STREQUAL "XTENSA")
  add_definitions(-DBUILD_TARGET_XTENSA)
elseif (WAMR_BUILD_TARGET STREQUAL "RISCV64" OR WAMR_BUILD_TARGET STREQUAL "RISCV64_LP64D")
  add_definitions(-DBUILD_TARGET_RISCV64_LP64D)
elseif (WAMR_BUILD_TARGET STREQUAL "RISCV64_LP64")
  add_definitions(-DBUILD_TARGET_RISCV64_LP64)
elseif (WAMR_BUILD_TARGET STREQUAL "RISCV32" OR WAMR_BUILD_TARGET STREQUAL "RISCV32_ILP32D")
  add_definitions(-DBUILD_TARGET_RISCV32_ILP32D)
elseif (WAMR_BUILD_TARGET STREQUAL "RISCV32_ILP32F")
  add_definitions(-DBUILD_TARGET_RISCV32_ILP32F)
elseif (WAMR_BUILD_TARGET STREQUAL "RISCV32_ILP32")
  add_definitions(-DBUILD_TARGET_RISCV32_ILP32)
elseif (WAMR_BUILD_TARGET STREQUAL "ARC")
  add_definitions(-DBUILD_TARGET_ARC)
else ()
  message (FATAL_ERROR "-- WAMR build target isn't set")
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DBH_DEBUG=1)
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  if (WAMR_BUILD_TARGET STREQUAL "X86_64" OR WAMR_BUILD_TARGET STREQUAL "AMD_64"
      OR WAMR_BUILD_TARGET MATCHES "AARCH64.*" OR WAMR_BUILD_TARGET MATCHES "RISCV64.*")
    if (NOT WAMR_BUILD_PLATFORM STREQUAL "windows")
      # Add -fPIC flag if build as 64-bit
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
      set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS} -fPIC")
      set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS} -fPIC")
    endif ()
  else ()
    include(CheckCCompilerFlag)
    Check_C_Compiler_Flag(-m32 M32_OK)
    if (M32_OK OR WAMR_BUILD_TARGET STREQUAL "X86_32")
      add_definitions (-m32)
      set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
      set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
    endif ()
  endif ()
endif ()

if (WAMR_BUILD_TARGET MATCHES "ARM.*")
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -marm")
elseif (WAMR_BUILD_TARGET MATCHES "THUMB.*")
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mthumb")
  set (CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -Wa,-mthumb")
elseif (WAMR_BUILD_TARGET MATCHES "X86_.*" OR WAMR_BUILD_TARGET STREQUAL "AMD_64")
    if (CMAKE_C_COMPILER_ID MATCHES ".*GNU")
      set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mindirect-branch-register")
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mindirect-branch-register")
    endif ()
endif ()


include (${CMAKE_CURRENT_LIST_DIR}/warnings.cmake)

if (NOT WAMR_BUILD_INTERP EQUAL 1)
if (NOT WAMR_BUILD_AOT EQUAL 1)
  message (FATAL_ERROR "-- WAMR Interpreter and AOT must be enabled at least one")
endif ()
endif ()

########################################
# Wasm specification version presets
#
# A preset says which proposals a Wasm version requires, and nothing else: every
# other feature already defaults to 0, so a version is described by the handful
# of switches it turns on.  Bulk memory is the one exception -- it defaults to 1
# for compatibility (see the "Default values" block below) -- so Wasm 1.0 has to
# turn it back off.
#
# Listing what a version excludes was tried and dropped: it made every new
# feature something to remember to add to the list, and forgetting would have
# broken the preset silently.  Relying on the defaults costs nothing and cannot
# be forgotten, and samples/minimum's all-off preset asserts that the defaults
# really are off.
#
# This block runs before the "Default values" section so that its set() calls
# win over the if (NOT DEFINED ...) defaults, and before the derived switches
# (WAMR_BUILD_BULK_MEMORY_OPT, WAMR_BUILD_CALL_INDIRECT_OVERLONG) so that those
# follow from what a preset leaves here.
#
# A feature the caller asks for on top of a preset is kept: the preset is the
# baseline, not a cage.
########################################

if (NOT DEFINED WAMR_BUILD_WASM_SPEC1)
  set (WAMR_BUILD_WASM_SPEC1 0)
endif ()

if (NOT DEFINED WAMR_BUILD_WASM_SPEC2)
  set (WAMR_BUILD_WASM_SPEC2 0)
endif ()

if (NOT DEFINED WAMR_BUILD_WASM_SPEC3)
  set (WAMR_BUILD_WASM_SPEC3 0)
endif ()

# LIME1 and the version presets are profiles of the same kind, so a build asks
# for one or the other: LIME1 is the Lime1 minimal feature set (bulk-memory-opt,
# call-indirect-overlong and extended constant expressions), which is neither
# the Wasm 1.0 nor the Wasm 2.0 feature set.
if (WAMR_BUILD_LIME1 EQUAL 1
    AND (WAMR_BUILD_WASM_SPEC1
         OR WAMR_BUILD_WASM_SPEC2
         OR WAMR_BUILD_WASM_SPEC3))
  message (FATAL_ERROR
    "WAMR_BUILD_LIME1 and the WAMR_BUILD_WASM_SPEC* presets are mutually "
    "exclusive: each one describes its own feature set. Set "
    "WAMR_BUILD_LIME1=1 or one WAMR_BUILD_WASM_SPEC<N>=1, not both.")
endif ()

if (WAMR_BUILD_WASM_SPEC1 EQUAL 1 AND WAMR_BUILD_WASM_SPEC2 EQUAL 1)
  message (FATAL_ERROR
    "WAMR_BUILD_WASM_SPEC1 and WAMR_BUILD_WASM_SPEC2 are mutually exclusive: "
    "each one is a complete feature set, not a layer on top of the other. "
    "Set exactly one of them.")
endif ()

# WAMR does not implement Wasm 3.0 completely (Relaxed SIMD, Custom Annotation
# Syntax, JS String Builtins and the up-to-date Exception Handling proposal are
# missing, and GC, Multi-Memory and Memory64 are incomplete or limited to some
# running modes), so the preset is not offered.  Asking for it is a
# configuration error: fail the configure step, and let the caller express what
# it needs with WAMR_BUILD_WASM_SPEC2 plus the individual feature switches.
if (WAMR_BUILD_WASM_SPEC3)
  #TODO: SPEC3 should use SPEC2(includes SPEC1) as a base and enable additional features
  message (FATAL_ERROR
    "WAMR_BUILD_WASM_SPEC3 is not supported: WAMR does not implement Wasm 3.0 "
    "completely. Missing or limited Wasm 3.0 proposals:\n"
    "  \"Relaxed SIMD\": not implemented\n"
    "  \"Custom Annotation Syntax in the Text Format\": not implemented\n"
    "  \"JS String Builtins\": not implemented (WAMR_BUILD_STRINGREF is the "
    "phase-1 \"Reference-Typed Strings\" proposal, not \"JS String Builtins\")\n"
    "  \"Exception Handling\": only the legacy, classic-interp-only proposal "
    "is implemented\n"
    "  \"Garbage Collection\"/\"Typed Function References\": partially "
    "compliant\n"
    "  \"Multiple Memories\": classic-interp only\n"
    "  \"Memory64\": classic-interp and AOT only\n"
    "Build Wasm 2.0 with WAMR_BUILD_WASM_SPEC2=1 and add the Wasm 3.0 features "
    "you need (WAMR_BUILD_TAIL_CALL, WAMR_BUILD_GC, ...) individually.")
endif ()

if (WAMR_BUILD_WASM_SPEC2 EQUAL 1)
  message ("     Wasm 2.0 preset enabled via WAMR_BUILD_WASM_SPEC2")
  # The only configurable switches among the Wasm 2.0 proposals; Multi-value,
  # Non-trapping float-to-int Conversions, Sign-extension Operators and
  # Import/Export of Mutable Globals are always on and have no switch.
  set (WAMR_BUILD_BULK_MEMORY 1)
  set (WAMR_BUILD_REF_TYPES 1)
  set (WAMR_BUILD_SIMD 1)
  #TODO: SPEC2 should use SPEC1 as a base and enable additional features
elseif (WAMR_BUILD_WASM_SPEC1 EQUAL 1)
  message ("     Wasm 1.0 preset enabled via WAMR_BUILD_WASM_SPEC1")
  # Every Wasm 1.0 proposal is always on in WAMR, so there is nothing to turn
  # on; bulk memory is post-1.0 and defaults to 1, so it is turned off here.
  set (WAMR_BUILD_BULK_MEMORY 0)
endif ()

# Lazy JIT is an implementation detail of the JIT running modes rather than a
# feature of its own: derive it from the running mode unless the user asked for
# a specific value.
if (NOT DEFINED WAMR_BUILD_LAZY_JIT)
  if (WAMR_BUILD_FAST_JIT EQUAL 1 OR WAMR_BUILD_JIT EQUAL 1)
    set (WAMR_BUILD_LAZY_JIT 1)
  else ()
    set (WAMR_BUILD_LAZY_JIT 0)
  endif ()
endif ()

if (WAMR_BUILD_JIT EQUAL 1)
  # In Debug mode, always use release builds of pre-built dependency libraries
  if (WAMR_BUILD_PLATFORM STREQUAL "windows" AND MSVC)
    add_compile_options($<$<CONFIG:Debug>:/MD>)
  endif()

  # Resolve LLVM_DIR and load the LLVM package; its zlib/zstd
  # dependencies are handled by find_package(LLVM) itself.  Shared with
  # the unit tests (see llvm_env.cmake).
  include (${CMAKE_CURRENT_LIST_DIR}/llvm_env.cmake)

  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  # Disable -Wredundant-move when building LLVM JIT
  include(CheckCXXCompilerFlag)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    check_cxx_compiler_flag("-Wredundant-move" CXX_SUPPORTS_REDUNDANT_MOVE_FLAG)
    if (CXX_SUPPORTS_REDUNDANT_MOVE_FLAG)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-redundant-move")
    endif ()
    # Enable exporting symbols after llvm-17, or LLVM JIT may run failed
    # with `llvm_orc_registerEHFrameSectionWrapper` symbol not found error
    if (${LLVM_PACKAGE_VERSION} VERSION_GREATER_EQUAL "17.0.0")
      set (CMAKE_ENABLE_EXPORTS 1)
    endif ()
  endif ()
else ()
  unset (LLVM_AVAILABLE_LIBS)
endif ()

# Version
include (${WAMR_ROOT_DIR}/build-scripts/version.cmake)

# Package
include (${WAMR_ROOT_DIR}/build-scripts/package.cmake)

# Sanitizers

if (NOT DEFINED WAMR_BUILD_SANITIZER)
  set(WAMR_BUILD_SANITIZER "$ENV{WAMR_BUILD_SANITIZER}")
endif()

if (NOT WAMR_BUILD_SANITIZER STREQUAL "")
  set(SUPPORTED_SANITIZERS "ubsan;asan;tsan;posan")
  string(REPLACE "," ";" SANITIZER_LIST "${WAMR_BUILD_SANITIZER}")

  # Check uncompabile sanitizers
  if("tsan" IN_LIST SANITIZER_LIST AND "asan" IN_LIST SANITIZER_LIST)
    message(FATAL_ERROR "ThreadSanitizer (tsan) and AddressSanitizer (asan) cannot be used together!")
  endif()

  # Check every sanitizer in the list
  set(INVALID_SANITIZERS "")
  list(REMOVE_DUPLICATES SANITIZER_LIST)
  set(SANITIZER_FLAGS)
  set(NO_SANITIZER_FLAGS)
  foreach(sanitizer ${SANITIZER_LIST})
    string(STRIP "${sanitizer}" sanitizer)
    if(NOT sanitizer IN_LIST SUPPORTED_SANITIZERS)
      list(APPEND INVALID_SANITIZERS "${sanitizer}")
    elseif(sanitizer STREQUAL "ubsan")
      list(APPEND SANITIZER_FLAGS "undefined")
      list(APPEND NO_SANITIZER_FLAGS "alignment")
    elseif(sanitizer STREQUAL "asan")
      if (NOT WAMR_BUILD_JIT EQUAL 1)
        set(ENV{ASAN_OPTIONS} "verbosity=2 debug=true")
        list(APPEND SANITIZER_FLAGS "address")
      else()
        message(WARNING "AddressSanitizer is not supported in LLVM JIT mode, skip it")
      endif()
    elseif(sanitizer STREQUAL "tsan")
      list(APPEND SANITIZER_FLAGS "thread")
    elseif(sanitizer STREQUAL "posan")
      list(APPEND SANITIZER_FLAGS "pointer-overflow")
    endif()
  endforeach()

  if(INVALID_SANITIZERS)
    message(FATAL_ERROR "Unsupported sanitizers: ${INVALID_SANITIZERS}")
  endif()
  # common flags for all sanitizers
  # clang: warning: the object size sanitizer has no effect at -O0, but is explicitly enabled ... [-Winvalid-command-line-argument]
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O1 -fno-omit-frame-pointer -fno-sanitize-recover=all -fno-sanitize=alignment")
  if(CMAKE_C_COMPILER_ID MATCHES ".*Clang")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-sanitize=unsigned-integer-overflow")
  endif()
  if(SANITIZER_FLAGS)
    string(REPLACE ";" "," SANITIZER_FLAGS_STR "${SANITIZER_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=${SANITIZER_FLAGS_STR}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZER_FLAGS_STR}")
  endif()
  if(NO_SANITIZER_FLAGS)
    string(REPLACE ";" "," NO_SANITIZER_FLAGS_STR "${NO_SANITIZER_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-sanitize=${NO_SANITIZER_FLAGS_STR}")
  endif()
endif ()

if (WAMR_BUILD_LINUX_PERF EQUAL 1)
  if (NOT WAMR_BUILD_JIT AND NOT WAMR_BUILD_AOT)
    message(WARNING "only support perf in aot and llvm-jit")
    set(WAMR_BUILD_LINUX_PERF 0)
  endif ()
endif ()

if (NOT DEFINED WAMR_BUILD_SHRUNK_MEMORY)
  # Kept on: see the note on the "Default values" block below.
  #TODO: SHRUNK_MEMORY should be off by default. It is kept for now to
  # maintain compatibility with existing embedders.
  set (WAMR_BUILD_SHRUNK_MEMORY 1)
endif ()

########################################
# Default values
#
# A feature added from here on defaults to OFF, so that a build which does not
# ask for anything gets the smallest possible runtime.  Products that want a
# feature on by default set it explicitly at their own entry point (see the root
# CMakeLists.txt, product-mini/platforms/*/ and wamr-compiler/).
#
# WAMR_BUILD_BULK_MEMORY and WAMR_BUILD_SHRUNK_MEMORY are the two exceptions:
# they shipped defaulting to ON, and turning them off here would silently drop
# instructions (memory.fill, ...) from every embedder that does not pass them
# explicitly.  That failure only shows up at run time as "unsupported opcode",
# so it is not worth the churn -- ask for the minimal runtime with the all-off
# preset in samples/minimum/CMakePresets.json instead.
########################################
#TODO: BULK_MEMORY should be off by default. But it is kept on for now to
# maintain compatibility with existing embedders.
if (NOT DEFINED WAMR_BUILD_BULK_MEMORY)
  set (WAMR_BUILD_BULK_MEMORY 1)
endif ()

if (NOT DEFINED WAMR_BUILD_BULK_MEMORY_OPT)
  set (WAMR_BUILD_BULK_MEMORY_OPT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_BRANCH_HINTS)
  set (WAMR_BUILD_BRANCH_HINTS 0)
endif ()

if (NOT DEFINED WAMR_BUILD_CALL_INDIRECT_OVERLONG)
  set (WAMR_BUILD_CALL_INDIRECT_OVERLONG 0)
endif ()

if (NOT DEFINED WAMR_BUILD_EXCE_HANDLING)
  set (WAMR_BUILD_EXCE_HANDLING 0)
endif ()

if (NOT DEFINED WAMR_BUILD_GC)
  set (WAMR_BUILD_GC 0)
endif ()

if (NOT DEFINED WAMR_BUILD_MEMORY64)
  set (WAMR_BUILD_MEMORY64 0)
endif ()

if (NOT DEFINED WAMR_BUILD_MULTI_MEMORY)
  set (WAMR_BUILD_MULTI_MEMORY 0)
endif ()

if (NOT DEFINED WAMR_BUILD_REF_TYPES)
  set (WAMR_BUILD_REF_TYPES 0)
endif ()

if (NOT DEFINED WAMR_BUILD_SHARED_MEMORY)
  set(WAMR_BUILD_SHARED_MEMORY 0)
endif ()

if (NOT DEFINED WAMR_BUILD_SIMD)
  set (WAMR_BUILD_SIMD 0)
endif ()

if (NOT DEFINED WAMR_BUILD_STRINGREF)
  set(WAMR_BUILD_STRINGREF 0)
endif ()

if (NOT DEFINED WAMR_BUILD_TAIL_CALL)
  set (WAMR_BUILD_TAIL_CALL 0)
endif ()

if (NOT DEFINED WAMR_BUILD_EXTENDED_CONST_EXPR)
  set (WAMR_BUILD_EXTENDED_CONST_EXPR 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LIME1)
  set (WAMR_BUILD_LIME1 0)
endif ()

# Macros which used to be reachable only by defining them on the compiler
# command line.  They keep their core/config.h default, they just become
# configurable from cmake like every other switch.
if (NOT DEFINED WAMR_BUILD_LOG)
  set (WAMR_BUILD_LOG 1)
endif ()

if (NOT DEFINED WAMR_BUILD_OPCODE_COUNTER)
  set (WAMR_BUILD_OPCODE_COUNTER 0)
endif ()

if (NOT DEFINED WAMR_BUILD_WORD_ALIGN_READ)
  set (WAMR_BUILD_WORD_ALIGN_READ 0)
endif ()

if (NOT DEFINED WAMR_BUILD_BASE_LIB)
  set (WAMR_BUILD_BASE_LIB 0)
endif ()

if (NOT DEFINED WAMR_BUILD_APP_FRAMEWORK)
  set (WAMR_BUILD_APP_FRAMEWORK 0)
endif ()

if (NOT DEFINED WAMR_BUILD_FUZZ_TEST)
  set (WAMR_BUILD_FUZZ_TEST 0)
endif ()

if (WAMR_BUILD_LIME1 EQUAL 1)
  set (WAMR_BUILD_BULK_MEMORY_OPT 1)
  set (WAMR_BUILD_CALL_INDIRECT_OVERLONG 1)
  set (WAMR_BUILD_EXTENDED_CONST_EXPR 1)
endif ()

if (WAMR_BUILD_BULK_MEMORY EQUAL 1)
  set (WAMR_BUILD_BULK_MEMORY_OPT 1)
endif ()

if (WAMR_BUILD_REF_TYPES EQUAL 1)
  set (WAMR_BUILD_CALL_INDIRECT_OVERLONG 1)
endif ()

if (NOT DEFINED WAMR_BUILD_INTERP)
  set (WAMR_BUILD_INTERP 0)
endif ()

if (NOT DEFINED WAMR_BUILD_AOT)
  set (WAMR_BUILD_AOT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_JIT)
  set (WAMR_BUILD_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_LAZY_JIT)
  set (WAMR_BUILD_LAZY_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_JIT)
  set (WAMR_BUILD_FAST_JIT 0)
endif ()

if (NOT DEFINED WAMR_BUILD_FAST_INTERP)
  set (WAMR_BUILD_FAST_INTERP 0)
endif ()

include(${CMAKE_CURRENT_LIST_DIR}/unsupported_combination.cmake)

########################################
# Compilation options to marco
# If a compilation option is enabled, the corresponding macro will be defined.
# Otherwise, core/config.h will take care of defining the macro with its default value.
########################################

message ("-- Build Configurations:")
message ("     Build as target ${WAMR_BUILD_TARGET}")
message ("     Build for platform ${WAMR_BUILD_PLATFORM}")
message ("     CMAKE_BUILD_TYPE " ${CMAKE_BUILD_TYPE})
message ("     BUILD_SHARED_LIBS " ${BUILD_SHARED_LIBS})
################## running mode ##################
if (WAMR_BUILD_INTERP EQUAL 1)
  message ("     WAMR Interpreter enabled")
else ()
  message ("     WAMR Interpreter disabled")
endif ()
if ((WAMR_BUILD_FAST_INTERP EQUAL 1) AND (WAMR_BUILD_INTERP EQUAL 1))
  add_definitions (-DWASM_ENABLE_FAST_INTERP=1)
  message ("     Fast interpreter enabled")
else ()
  add_definitions (-DWASM_ENABLE_FAST_INTERP=0)
  message ("     Fast interpreter disabled")
endif ()
if (WAMR_BUILD_AOT EQUAL 1)
  message ("     WAMR AOT enabled")
else ()
  message ("     WAMR AOT disabled")
endif ()
if (WAMR_BUILD_LAZY_JIT EQUAL 1)
  add_definitions("-DWASM_ENABLE_LAZY_JIT=1")
else ()
  add_definitions("-DWASM_ENABLE_LAZY_JIT=0")
endif ()
if (WAMR_BUILD_FAST_JIT EQUAL 1)
  if (WAMR_BUILD_LAZY_JIT EQUAL 1)
    message ("     WAMR Fast JIT enabled with Lazy Compilation")
  else ()
    message ("     WAMR Fast JIT enabled with Eager Compilation")
  endif ()
else ()
  message ("     WAMR Fast JIT disabled")
endif ()
if (WAMR_BUILD_JIT EQUAL 1)
  add_definitions("-DWASM_ENABLE_JIT=1")
  if (WAMR_BUILD_LAZY_JIT EQUAL 1)
    message ("     WAMR LLVM ORC JIT enabled with Lazy Compilation")
  else ()
    message ("     WAMR LLVM ORC JIT enabled with Eager Compilation")
  endif ()
else ()
  message ("     WAMR LLVM ORC JIT disabled")
endif ()
if (WAMR_BUILD_FAST_JIT EQUAL 1 AND WAMR_BUILD_JIT EQUAL 1
    AND WAMR_BUILD_LAZY_JIT EQUAL 1)
  message ("     Multi-tier JIT enabled")
endif ()
################## test modes ##################
if (WAMR_BUILD_SPEC_TEST EQUAL 1)
  add_definitions (-DWASM_ENABLE_SPEC_TEST=1)
  message ("     spec test compatible mode is on")
endif ()
if (WAMR_BUILD_WASI_TEST EQUAL 1)
  add_definitions (-DWASM_ENABLE_WASI_TEST=1)
  message ("     wasi test compatible mode is on")
endif ()
################## native ##################
if (WAMR_BUILD_LIBC_BUILTIN EQUAL 1)
  message ("     Libc builtin enabled")
else ()
  message ("     Libc builtin disabled")
endif ()
if (WAMR_BUILD_LIBC_UVWASI EQUAL 1)
  message ("     Libc WASI enabled with uvwasi implementation\n"
           "     WANRING:: uvwasi does not currently provide the comprehensive\n"
           "     file system security properties provided by some WASI runtimes.\n"
           "     Full support for secure file system sandboxing may or may not\n"
           "     be implemented in future. In the mean time, DO NOT RELY ON IT\n"
           "     TO RUN UNTRUSTED CODE.")
elseif (WAMR_BUILD_LIBC_WASI EQUAL 1)
  message ("     Libc WASI enabled")
else ()
  message ("     Libc WASI disabled")
endif ()
if (WAMR_BUILD_THREAD_MGR EQUAL 1)
  message ("     Thread manager enabled")
endif ()
if (WAMR_BUILD_LIB_PTHREAD EQUAL 1)
  message ("     Lib pthread enabled")
endif ()
if (WAMR_BUILD_LIB_PTHREAD_SEMAPHORE EQUAL 1)
  message ("     Lib pthread semaphore enabled")
endif ()
if (WAMR_BUILD_LIB_WASI_THREADS EQUAL 1)
  message ("     Lib wasi-threads enabled")
endif ()
if (WAMR_BUILD_LIBC_EMCC EQUAL 1)
  message ("     Libc emcc enabled")
endif ()
if (WAMR_BUILD_LIB_RATS EQUAL 1)
  message ("     Lib rats enabled")
endif()
if ((WAMR_BUILD_LIB_SIMDE EQUAL 1))
  message ("     Lib simde enabled")
endif()
################## WAMR features ##################
if (WAMR_BUILD_MULTI_MODULE EQUAL 1)
  add_definitions (-DWASM_ENABLE_MULTI_MODULE=1)
  message ("     Multiple modules enabled")
else ()
  add_definitions (-DWASM_ENABLE_MULTI_MODULE=0)
  message ("     Multiple modules disabled")
endif ()
if (WAMR_BUILD_BULK_MEMORY EQUAL 1)
  add_definitions (-DWASM_ENABLE_BULK_MEMORY=1)
else ()
  add_definitions (-DWASM_ENABLE_BULK_MEMORY=0)
endif ()
if (WAMR_BUILD_BULK_MEMORY_OPT EQUAL 1)
  add_definitions (-DWASM_ENABLE_BULK_MEMORY_OPT=1)
else()
  add_definitions (-DWASM_ENABLE_BULK_MEMORY_OPT=0)
endif ()
if (WAMR_BUILD_SHARED_MEMORY EQUAL 1)
  add_definitions (-DWASM_ENABLE_SHARED_MEMORY=1)
  message ("     Shared memory enabled")
else ()
  add_definitions (-DWASM_ENABLE_SHARED_MEMORY=0)
endif ()
if (WAMR_BUILD_SHARED_HEAP EQUAL 1)
  add_definitions (-DWASM_ENABLE_SHARED_HEAP=1)
  message ("     Shared heap enabled")
endif()
if (WAMR_BUILD_COPY_CALL_STACK EQUAL 1)
  add_definitions (-DWASM_ENABLE_COPY_CALL_STACK=1)
  message("     Copy callstack enabled")
endif()
if (WAMR_BUILD_MEMORY64 EQUAL 1)
  # if native is 32-bit or cross-compiled to 32-bit
  if (NOT WAMR_BUILD_TARGET MATCHES ".*64.*")
    message (FATAL_ERROR "-- Memory64 is only available on the 64-bit platform/target")
  endif()
  add_definitions (-DWASM_ENABLE_MEMORY64=1)
  set (WAMR_DISABLE_HW_BOUND_CHECK 1)
endif ()
if (WAMR_BUILD_MULTI_MEMORY EQUAL 1)
  add_definitions (-DWASM_ENABLE_MULTI_MEMORY=1)
  set (WAMR_BUILD_DEBUG_INTERP 0)
endif ()
if (WAMR_BUILD_MINI_LOADER EQUAL 1)
  add_definitions (-DWASM_ENABLE_MINI_LOADER=1)
  message ("     WASM mini loader enabled")
else ()
  add_definitions (-DWASM_ENABLE_MINI_LOADER=0)
endif ()
if (WAMR_DISABLE_HW_BOUND_CHECK EQUAL 1)
  add_definitions (-DWASM_DISABLE_HW_BOUND_CHECK=1)
  add_definitions (-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1)
  message ("     Hardware boundary check disabled")
else ()
  add_definitions (-DWASM_DISABLE_HW_BOUND_CHECK=0)
  if (WAMR_DISABLE_STACK_HW_BOUND_CHECK EQUAL 1)
    add_definitions (-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1)
    message ("     Hardware boundary check for native stack disabled")
  else ()
    add_definitions (-DWASM_DISABLE_STACK_HW_BOUND_CHECK=0)
  endif ()
endif ()
if (WAMR_DISABLE_WAKEUP_BLOCKING_OP EQUAL 1)
  add_definitions (-DWASM_DISABLE_WAKEUP_BLOCKING_OP=1)
  message ("     Wakeup of blocking operations disabled")
else ()
  add_definitions (-DWASM_DISABLE_WAKEUP_BLOCKING_OP=0)
  message ("     Wakeup of blocking operations enabled")
endif ()
if (WAMR_BUILD_SIMD EQUAL 1)
  if (WAMR_BUILD_TARGET MATCHES "RISCV64.*")
    set(SIMD_ENABLED 0)
    message ("     SIMD disabled due to not supported on target RISCV64")
  else()
    set(SIMD_ENABLED 1)
    message ("     SIMD enabled")
  endif ()
  add_definitions(-DWASM_ENABLE_SIMD=${SIMD_ENABLED})
endif ()
if (WAMR_BUILD_AOT_STACK_FRAME EQUAL 1)
  add_definitions (-DWASM_ENABLE_AOT_STACK_FRAME=1)
  message ("     AOT stack frame enabled")
endif ()
if (WAMR_BUILD_MEMORY_PROFILING EQUAL 1)
  add_definitions (-DWASM_ENABLE_MEMORY_PROFILING=1)
  message ("     Memory profiling enabled")
endif ()
if (WAMR_BUILD_MEMORY_TRACING EQUAL 1)
  add_definitions (-DWASM_ENABLE_MEMORY_TRACING=1)
  message ("     Memory tracing enabled")
endif ()
if (WAMR_BUILD_PERF_PROFILING EQUAL 1)
  add_definitions (-DWASM_ENABLE_PERF_PROFILING=1)
  message ("     Performance profiling enabled")
endif ()
if (DEFINED WAMR_APP_THREAD_STACK_SIZE_MAX)
  add_definitions (-DAPP_THREAD_STACK_SIZE_MAX=${WAMR_APP_THREAD_STACK_SIZE_MAX})
endif ()
if (WAMR_BUILD_CUSTOM_NAME_SECTION EQUAL 1)
  add_definitions (-DWASM_ENABLE_CUSTOM_NAME_SECTION=1)
  message ("     Custom name section enabled")
endif ()
if (WAMR_BUILD_DUMP_CALL_STACK EQUAL 1)
  add_definitions (-DWASM_ENABLE_DUMP_CALL_STACK=1)
  message ("     Dump call stack enabled")
endif ()
if (WAMR_BUILD_TAIL_CALL EQUAL 1)
  add_definitions (-DWASM_ENABLE_TAIL_CALL=1)
endif ()
if (WAMR_BUILD_REF_TYPES EQUAL 1)
  add_definitions (-DWASM_ENABLE_REF_TYPES=1)
endif ()
if (WAMR_BUILD_CALL_INDIRECT_OVERLONG EQUAL 1)
  add_definitions (-DWASM_ENABLE_CALL_INDIRECT_OVERLONG=1)
else ()
  add_definitions(-DWASM_ENABLE_CALL_INDIRECT_OVERLONG=0)
endif ()
if (WAMR_BUILD_GC EQUAL 1)
  if (WAMR_TEST_GC EQUAL 1)
    message("      GC testing enabled")
  endif()
endif ()
if (WAMR_BUILD_GC EQUAL 1 AND WAMR_BUILD_GC_PERF_PROFILING EQUAL 1)
  add_definitions (-DWASM_ENABLE_GC_PERF_PROFILING=1)
  message ("     GC performance profiling enabled")
else ()
  message ("     GC performance profiling disabled")
endif ()
if (WAMR_BUILD_STRINGREF EQUAL 1)
  # runtime_lib.cmake always gives this a value; "STUB" is the builtin one.
  if (WAMR_STRINGREF_IMPL_SOURCE STREQUAL "STUB")
    message ("       Using WAMR builtin implementation for stringref")
  else ()
    message ("       Using custom implementation for stringref")
  endif()
endif ()
if (WAMR_BUILD_PERF_PROFILING EQUAL 1 OR
    WAMR_BUILD_DUMP_CALL_STACK EQUAL 1 OR
    WAMR_BUILD_GC EQUAL 1)
  # Enable AOT/JIT stack frame when perf-profiling, dump-call-stack
  # or GC is enabled
  if (WAMR_BUILD_AOT EQUAL 1 OR WAMR_BUILD_JIT EQUAL 1)
    add_definitions (-DWASM_ENABLE_AOT_STACK_FRAME=1)
  endif ()
endif ()
if (WAMR_BUILD_EXCE_HANDLING EQUAL 1)
  add_definitions (-DWASM_ENABLE_EXCE_HANDLING=1)
  add_definitions (-DWASM_ENABLE_TAGS=1)
  message ("     Exception Handling enabled")
endif ()
if (DEFINED WAMR_BH_VPRINTF)
  add_definitions (-DBH_VPRINTF=${WAMR_BH_VPRINTF})
endif ()
if (DEFINED WAMR_BH_LOG)
  add_definitions (-DBH_LOG=${WAMR_BH_LOG})
endif ()
if (WAMR_DISABLE_APP_ENTRY EQUAL 1)
  message ("     WAMR application entry functions excluded")
endif ()
if (WAMR_BUILD_DEBUG_INTERP EQUAL 1)
    message ("     Debug Interpreter enabled")
endif ()
if (WAMR_BUILD_DEBUG_AOT EQUAL 1)
    message ("     Debug AOT enabled")
endif ()
if (WAMR_BUILD_DYNAMIC_AOT_DEBUG EQUAL 1)
    add_definitions (-DWASM_ENABLE_DYNAMIC_AOT_DEBUG=1)
    message ("     Dynamic AOT debug enabled")
endif ()
if (WAMR_BUILD_LOAD_CUSTOM_SECTION EQUAL 1)
    add_definitions (-DWASM_ENABLE_LOAD_CUSTOM_SECTION=1)
    message ("     Load custom section enabled")
endif ()
if (WAMR_BUILD_GLOBAL_HEAP_POOL EQUAL 1)
  add_definitions(-DWASM_ENABLE_GLOBAL_HEAP_POOL=1)
  message ("     Global heap pool enabled")
endif ()
if (WAMR_BUILD_GLOBAL_HEAP_SIZE GREATER 0)
  add_definitions (-DWASM_GLOBAL_HEAP_SIZE=${WAMR_BUILD_GLOBAL_HEAP_SIZE})
  message ("     Custom global heap size: " ${WAMR_BUILD_GLOBAL_HEAP_SIZE})
else ()
  # Spec test requires more heap pool size
  if (WAMR_BUILD_SPEC_TEST EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "linux-sgx")
      math(EXPR WAMR_BUILD_GLOBAL_HEAP_SIZE "100 * 1024 * 1024")
    else ()
      math(EXPR WAMR_BUILD_GLOBAL_HEAP_SIZE "300 * 1024 * 1024")
    endif ()
    add_definitions (-DWASM_GLOBAL_HEAP_SIZE=${WAMR_BUILD_GLOBAL_HEAP_SIZE})
  else ()
    # By default, the global heap size is of 10 MB
    math(EXPR WAMR_BUILD_GLOBAL_HEAP_SIZE "10 * 1024 * 1024")
    add_definitions (-DWASM_GLOBAL_HEAP_SIZE=${WAMR_BUILD_GLOBAL_HEAP_SIZE})
  endif ()
endif ()
if (WAMR_BUILD_STACK_GUARD_SIZE GREATER 0)
    add_definitions (-DWASM_STACK_GUARD_SIZE=${WAMR_BUILD_STACK_GUARD_SIZE})
    message ("     Custom stack guard size: " ${WAMR_BUILD_STACK_GUARD_SIZE})
endif ()
if (WAMR_BUILD_SGX_IPFS EQUAL 1)
    add_definitions (-DWASM_ENABLE_SGX_IPFS=1)
    message ("     SGX IPFS enabled")
endif ()
if (WAMR_BUILD_WASI_NN EQUAL 1)
  message ("     WASI-NN enabled")
  add_definitions (-DWASM_ENABLE_WASI_NN=1)
  # Variant backends
  if (NOT WAMR_BUILD_WASI_NN_TFLITE EQUAL 1 AND
      NOT WAMR_BUILD_WASI_NN_OPENVINO EQUAL 1 AND
      NOT WAMR_BUILD_WASI_NN_LLAMACPP EQUAL 1 AND
      NOT WAMR_BUILD_WASI_NN_ONNX EQUAL 1)
    message (FATAL_ERROR "   Need to select a backend for WASI-NN")
  endif ()

  if (WAMR_BUILD_WASI_NN_TFLITE EQUAL 1)
    message ("     WASI-NN: backend tflite enabled")
    add_definitions (-DWASM_ENABLE_WASI_NN_TFLITE)
  endif ()
  if (WAMR_BUILD_WASI_NN_OPENVINO EQUAL 1)
    message ("     WASI-NN: backend openvino enabled")
    add_definitions (-DWASM_ENABLE_WASI_NN_OPENVINO)
  endif ()
  if (WAMR_BUILD_WASI_NN_LLAMACPP EQUAL 1)
    message ("     WASI-NN: backend llamacpp enabled")
    add_definitions (-DWASM_ENABLE_WASI_NN_LLAMACPP)
  endif ()
  if (WAMR_BUILD_WASI_NN_ONNX EQUAL 1)
    message ("     WASI-NN: backend onnx enabled")
    add_definitions (-DWASM_ENABLE_WASI_NN_ONNX)
  endif ()
  # Variant devices
  if (WAMR_BUILD_WASI_NN_ENABLE_GPU EQUAL 1)
      message ("     WASI-NN: GPU enabled")
      add_definitions (-DWASM_ENABLE_WASI_NN_GPU=1)
  endif ()
  if (WAMR_BUILD_WASI_NN_ENABLE_EXTERNAL_DELEGATE EQUAL 1)
      message ("     WASI-NN: External Delegation enabled")
      add_definitions (-DWASM_ENABLE_WASI_NN_EXTERNAL_DELEGATE=1)
  endif ()
  if (DEFINED WAMR_BUILD_WASI_NN_EXTERNAL_DELEGATE_PATH)
      add_definitions (-DWASM_WASI_NN_EXTERNAL_DELEGATE_PATH="${WAMR_BUILD_WASI_NN_EXTERNAL_DELEGATE_PATH}")
  endif ()
  if (NOT DEFINED WAMR_BUILD_WASI_EPHEMERAL_NN)
      set(WAMR_BUILD_WASI_EPHEMERAL_NN 1)
  endif()
  if (WAMR_BUILD_WASI_EPHEMERAL_NN EQUAL 1)
      message ("     WASI-NN: use 'wasi_ephemeral_nn' instead of 'wasi-nn'")
      add_definitions (-DWASM_ENABLE_WASI_EPHEMERAL_NN=1)
  endif()
endif ()
if (WAMR_BUILD_ALLOC_WITH_USER_DATA EQUAL 1)
  add_definitions(-DWASM_MEM_ALLOC_WITH_USER_DATA=1)
endif()
if (WAMR_BUILD_WASM_CACHE EQUAL 1)
  add_definitions (-DWASM_ENABLE_WASM_CACHE=1)
  message ("     Wasm files cache enabled")
endif ()
if (WAMR_BUILD_MODULE_INST_CONTEXT EQUAL 1)
  add_definitions (-DWASM_ENABLE_MODULE_INST_CONTEXT=1)
  message ("     Module instance context enabled")
else ()
  add_definitions (-DWASM_ENABLE_MODULE_INST_CONTEXT=0)
endif ()
# WAMR_BUILD_GC_VERIFY is the older name of WAMR_BUILD_GC_HEAP_VERIFY; both used
# to define BH_ENABLE_GC_VERIFY, from two different cmake files.  Keep the old
# name working and define the macro in one place only.
if (WAMR_BUILD_GC_VERIFY EQUAL 1)
  message (DEPRECATION
           "WAMR_BUILD_GC_VERIFY is deprecated, use WAMR_BUILD_GC_HEAP_VERIFY")
  set (WAMR_BUILD_GC_HEAP_VERIFY 1)
endif ()
if (WAMR_BUILD_GC_HEAP_VERIFY EQUAL 1)
  add_definitions (-DBH_ENABLE_GC_VERIFY=1)
  message ("     GC heap verification enabled")
else ()
  add_definitions (-DBH_ENABLE_GC_VERIFY=0)
endif ()
if ("$ENV{COLLECT_CODE_COVERAGE}" STREQUAL "1" OR COLLECT_CODE_COVERAGE EQUAL 1)
  include(${CMAKE_CURRENT_LIST_DIR}/code_coverage.cmake)
  message ("     Collect code coverage enabled")
endif ()
if (WAMR_BUILD_STATIC_PGO EQUAL 1)
  add_definitions (-DWASM_ENABLE_STATIC_PGO=1)
  message ("     AOT static PGO enabled")
endif ()
if (WAMR_BUILD_TARGET STREQUAL "X86_64"
    AND WAMR_BUILD_PLATFORM STREQUAL "linux")
  if (WAMR_DISABLE_WRITE_GS_BASE EQUAL 1)
    # disabled by user
    set (DISABLE_WRITE_GS_BASE 1)
  elseif (WAMR_DISABLE_WRITE_GS_BASE EQUAL 0)
    # enabled by user
    set (DISABLE_WRITE_GS_BASE 0)
  elseif (CMAKE_CROSSCOMPILING)
    # disabled in cross compilation environment
    set (DISABLE_WRITE_GS_BASE 1)
  else ()
    # auto-detected by the compiler
    set (TEST_WRGSBASE_SOURCE "${CMAKE_BINARY_DIR}/test_wrgsbase.c")
    file (WRITE "${TEST_WRGSBASE_SOURCE}" "
    #include <stdio.h>
    #include <stdint.h>
    int main() {
        uint64_t value;
        asm volatile (\"wrgsbase %0\" : : \"r\"(value));
        printf(\"WRGSBASE instruction is available.\\n\");
        return 0;
    }")
    # Try to compile and run the test program
    try_run (TEST_WRGSBASE_RESULT
      TEST_WRGSBASE_COMPILED
      ${CMAKE_BINARY_DIR}/test_wrgsbase
      SOURCES ${TEST_WRGSBASE_SOURCE}
      CMAKE_FLAGS -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    )
    #message("${TEST_WRGSBASE_COMPILED}, ${TEST_WRGSBASE_RESULT}")
    if (TEST_WRGSBASE_RESULT EQUAL 0)
      set (DISABLE_WRITE_GS_BASE 0)
    else ()
      set (DISABLE_WRITE_GS_BASE 1)
    endif ()
  endif ()
  if (DISABLE_WRITE_GS_BASE EQUAL 1)
    add_definitions (-DWASM_DISABLE_WRITE_GS_BASE=1)
    message ("     Write linear memory base addr to x86 GS register disabled")
  else ()
    add_definitions (-DWASM_DISABLE_WRITE_GS_BASE=0)
    message ("     Write linear memory base addr to x86 GS register enabled")
  endif ()
endif ()
if (WAMR_CONFIGURABLE_BOUNDS_CHECKS EQUAL 1)
  add_definitions (-DWASM_CONFIGURABLE_BOUNDS_CHECKS=1)
  message ("     Configurable bounds checks enabled")
endif ()
if (WAMR_BUILD_LINUX_PERF EQUAL 1)
  add_definitions (-DWASM_ENABLE_LINUX_PERF=1)
  message ("     Linux perf support enabled")
endif ()
# Quick AOT/JIT entries only exist in the AOT and LLVM JIT running modes, so the
# running mode decides the value unless the user asked for a specific one.
if (NOT DEFINED WAMR_BUILD_QUICK_AOT_ENTRY)
  if (WAMR_BUILD_AOT EQUAL 1 OR WAMR_BUILD_JIT EQUAL 1)
    set (WAMR_BUILD_QUICK_AOT_ENTRY 1)
  else ()
    set (WAMR_BUILD_QUICK_AOT_ENTRY 0)
  endif ()
endif ()
if (WAMR_BUILD_QUICK_AOT_ENTRY EQUAL 1)
  add_definitions (-DWASM_ENABLE_QUICK_AOT_ENTRY=1)
  message ("     Quick AOT/JIT entries enabled")
else ()
  add_definitions (-DWASM_ENABLE_QUICK_AOT_ENTRY=0)
  message ("     Quick AOT/JIT entries disabled")
endif ()

# Likewise, AOT intrinsics are only meaningful in the AOT running mode.
if (NOT DEFINED WAMR_BUILD_AOT_INTRINSICS)
  if (WAMR_BUILD_AOT EQUAL 1)
    set (WAMR_BUILD_AOT_INTRINSICS 1)
  else ()
    set (WAMR_BUILD_AOT_INTRINSICS 0)
  endif ()
endif ()
if (WAMR_BUILD_AOT_INTRINSICS EQUAL 1)
  add_definitions (-DWASM_ENABLE_AOT_INTRINSICS=1)
  message ("     AOT intrinsics enabled")
else ()
  add_definitions (-DWASM_ENABLE_AOT_INTRINSICS=0)
  message ("     AOT intrinsics disabled")
endif ()
if (WAMR_BUILD_ALLOC_WITH_USAGE EQUAL 1)
  add_definitions(-DWASM_MEM_ALLOC_WITH_USAGE=1)
endif()
if (NOT WAMR_BUILD_SANITIZER STREQUAL "")
  message ("     Sanitizer ${WAMR_BUILD_SANITIZER} enabled")
endif ()
if (WAMR_BUILD_SHRUNK_MEMORY EQUAL 1)
  add_definitions (-DWASM_ENABLE_SHRUNK_MEMORY=1)
  message ("     Shrunk memory enabled")
else ()
  add_definitions (-DWASM_ENABLE_SHRUNK_MEMORY=0)
  message ("     Shrunk memory disabled")
endif()
if (WAMR_BUILD_AOT_VALIDATOR EQUAL 1)
  message ("     AOT validator enabled")
  add_definitions (-DWASM_ENABLE_AOT_VALIDATOR=1)
endif ()
if (WAMR_BUILD_INSTRUCTION_METERING EQUAL 1)
  message ("     Instruction metering enabled")
  add_definitions (-DWASM_ENABLE_INSTRUCTION_METERING=1)
endif ()
if (WAMR_BUILD_EXTENDED_CONST_EXPR EQUAL 1)
  message ("     Extended constant expression enabled")
  add_definitions(-DWASM_ENABLE_EXTENDED_CONST_EXPR=1)
else()
  message ("     Extended constant expression disabled")
  add_definitions(-DWASM_ENABLE_EXTENDED_CONST_EXPR=0)
endif ()
if (WAMR_BUILD_LIME1 EQUAL 1)
  message ("     Lime1 enabled")
endif ()
if (WAMR_BUILD_BRANCH_HINTS EQUAL 1)
  message ("     Branch hints enabled")
  add_definitions(-DWASM_ENABLE_BRANCH_HINTS=1)
endif ()
if (WAMR_BUILD_LOG EQUAL 1)
  add_definitions (-DWASM_ENABLE_LOG=1)
else ()
  add_definitions (-DWASM_ENABLE_LOG=0)
  message ("     Log disabled")
endif ()
if (WAMR_BUILD_OPCODE_COUNTER EQUAL 1)
  add_definitions (-DWASM_ENABLE_OPCODE_COUNTER=1)
  message ("     Opcode counter enabled")
else ()
  add_definitions (-DWASM_ENABLE_OPCODE_COUNTER=0)
endif ()
if (WAMR_BUILD_WORD_ALIGN_READ EQUAL 1)
  add_definitions (-DWASM_ENABLE_WORD_ALIGN_READ=1)
  message ("     Word align read enabled")
else ()
  add_definitions (-DWASM_ENABLE_WORD_ALIGN_READ=0)
endif ()
if (WAMR_BUILD_BASE_LIB EQUAL 1)
  add_definitions (-DWASM_ENABLE_BASE_LIB=1)
  message ("     Base lib enabled")
else ()
  add_definitions (-DWASM_ENABLE_BASE_LIB=0)
endif ()
if (WAMR_BUILD_APP_FRAMEWORK EQUAL 1)
  add_definitions (-DWASM_ENABLE_APP_FRAMEWORK=1)
  message ("     App framework enabled")
else ()
  add_definitions (-DWASM_ENABLE_APP_FRAMEWORK=0)
endif ()
if (WAMR_BUILD_FUZZ_TEST EQUAL 1)
  add_definitions (-DWASM_ENABLE_FUZZ_TEST=1)
  message ("     Fuzz test mode enabled")
else ()
  add_definitions (-DWASM_ENABLE_FUZZ_TEST=0)
endif ()

########################################
# Show Phase4 Wasm proposals status.
########################################

message (
"-- About Wasm Proposals:\n"
"     Always-on:\n"
"       \"Import/Export of Mutable Globals\"\n"
"       \"Multi-value\"\n"
"       \"Non-trapping float-to-int Conversions\"\n"
"       \"Sign-extension Operators\"\n"
"       \"WebAssembly C and C++ API\"\n"
"     Configurable. 0 is OFF. 1 is ON:\n"
"       \"Branch Hinting\" via WAMR_BUILD_BRANCH_HINTS: ${WAMR_BUILD_BRANCH_HINTS}\n"
"       \"Bulk Memory Operation\" via WAMR_BUILD_BULK_MEMORY: ${WAMR_BUILD_BULK_MEMORY}\n"
"       \"Bulk-memory-opt\" via WAMR_BUILD_BULK_MEMORY_OPT: ${WAMR_BUILD_BULK_MEMORY_OPT}\n"
"       \"Call-indirect-overlong\" via WAMR_BUILD_CALL_INDIRECT_OVERLONG: ${WAMR_BUILD_CALL_INDIRECT_OVERLONG}\n"
"       \"Extended Constant Expressions\" via WAMR_BUILD_EXTENDED_CONST_EXPR: ${WAMR_BUILD_EXTENDED_CONST_EXPR}\n"
"       \"Fixed-width SIMD\" via WAMR_BUILD_SIMD: ${WAMR_BUILD_SIMD}\n"
"       \"Garbage Collection\" via WAMR_BUILD_GC: ${WAMR_BUILD_GC}\n"
"       \"Legacy Exception Handling\" via WAMR_BUILD_EXCE_HANDLING: ${WAMR_BUILD_EXCE_HANDLING}\n"
"       \"Memory64\" via WAMR_BUILD_MEMORY64: ${WAMR_BUILD_MEMORY64}\n"
"       \"Multiple Memories\" via WAMR_BUILD_MULTI_MEMORY: ${WAMR_BUILD_MULTI_MEMORY}\n"
"       \"Reference Types\" via WAMR_BUILD_REF_TYPES: ${WAMR_BUILD_REF_TYPES}\n"
"       \"Reference-Typed Strings\" via WAMR_BUILD_STRINGREF: ${WAMR_BUILD_STRINGREF}\n"
"       \"Tail Call\" via WAMR_BUILD_TAIL_CALL: ${WAMR_BUILD_TAIL_CALL}\n"
"       \"Threads\" via WAMR_BUILD_SHARED_MEMORY: ${WAMR_BUILD_SHARED_MEMORY}\n"
"       \"Typed Function References\" via WAMR_BUILD_GC: ${WAMR_BUILD_GC}\n"
"     Unsupported (>= Phase4):\n"
"       \"Custom Annotation Syntax in the Text Format\"\n"
"       \"Exception Handling\"\n"
"       \"JS String Builtins\"\n"
"       \"Relaxed SIMD\"\n"
)

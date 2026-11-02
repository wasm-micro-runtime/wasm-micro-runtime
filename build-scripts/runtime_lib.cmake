# Copyright (C) 2019 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (NOT DEFINED WAMR_ROOT_DIR)
    set (WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../)
endif ()
if (NOT DEFINED SHARED_DIR)
    set (SHARED_DIR ${WAMR_ROOT_DIR}/core/shared)
endif ()
if (NOT DEFINED IWASM_DIR)
    set (IWASM_DIR ${WAMR_ROOT_DIR}/core/iwasm)
endif ()
if (NOT DEFINED DEPS_DIR)
    set (DEPS_DIR ${WAMR_ROOT_DIR}/core/deps)
endif ()
# If not defined, use host platform as the default build platform
if (NOT DEFINED WAMR_BUILD_PLATFORM)
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" WAMR_BUILD_PLATFORM)
endif ()
if (NOT DEFINED SHARED_PLATFORM_CONFIG)
    # CMake file for platform configuration. The PLATFORM_SHARED_SOURCE variable
    # should point to a list of platform-specfic source files to compile.
    set (SHARED_PLATFORM_CONFIG ${SHARED_DIR}/platform/${WAMR_BUILD_PLATFORM}/shared_platform.cmake)
endif ()

if (DEFINED EXTRA_SDK_INCLUDE_PATH)
    message(STATUS, "EXTRA_SDK_INCLUDE_PATH = ${EXTRA_SDK_INCLUDE_PATH} ")
    include_directories (
        ${EXTRA_SDK_INCLUDE_PATH}
    )
endif ()

# Set WAMR_BUILD_TARGET, currently values supported:
# "X86_64", "AMD_64", "X86_32", "AARCH64[sub]", "ARM[sub]", "THUMB[sub]",
# "MIPS", "XTENSA", "RISCV64[sub]", "RISCV32[sub]"
if (NOT DEFINED WAMR_BUILD_TARGET)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
        set (WAMR_BUILD_TARGET "AARCH64")
    elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
        set (WAMR_BUILD_TARGET "RISCV64")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
        # Build as X86_64 by default in 64-bit platform
        set (WAMR_BUILD_TARGET "X86_64")
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        # Build as X86_32 by default in 32-bit platform
        set (WAMR_BUILD_TARGET "X86_32")
    else ()
        message(SEND_ERROR "Unsupported build target platform!")
    endif ()
endif ()

########################################
# Wasm specification version presets
#
# A version preset is a complete feature set: the proposals it does not include
# are turned off here.
#
# A feature that a derivation needs is turned back on after the preset has
# cleared it.
#
# The "Default values" block of config_common.cmake runs after this block, so a
# value cleared here stays cleared.  Defaults an entry point sets for itself
# (REF_TYPES and SIMD in the top-level CMakeLists.txt, for example) are not
# visible here: if an entry point enables a feature a preset excludes, the
# preset wins.
#
# A feature asked for on top of a preset, and not excluded by it, is kept.
########################################

# Turn off a feature that a version preset does not include.  <feature> is the
# full variable name, for example WAMR_BUILD_SIMD.
#
# The variable is assigned even when undefined: the compatibility default below
# would otherwise turn it back on.
macro (wamr_preset_exclude feature)
  set (${feature} 0)
endmacro ()

if (NOT DEFINED WAMR_BUILD_WASM_SPEC1)
  set (WAMR_BUILD_WASM_SPEC1 0)
endif ()

if (NOT DEFINED WAMR_BUILD_WASM_SPEC2)
  set (WAMR_BUILD_WASM_SPEC2 0)
endif ()

if (NOT DEFINED WAMR_BUILD_WASM_SPEC3)
  set (WAMR_BUILD_WASM_SPEC3 0)
endif ()

# LIME1 and the version presets are profiles of the same kind; a build asks for
# one of them.
if (WAMR_BUILD_LIME1 EQUAL 1
    AND (WAMR_BUILD_WASM_SPEC1
         OR WAMR_BUILD_WASM_SPEC2
         OR WAMR_BUILD_WASM_SPEC3))
  message (FATAL_ERROR
    "WAMR_BUILD_LIME1 and the WAMR_BUILD_WASM_SPEC* presets are mutually "
    "exclusive: each one describes its own feature set. Set "
    "WAMR_BUILD_LIME1=1 or one WAMR_BUILD_WASM_SPEC<N>=1, not both.")
endif ()

# Lime1 is a profile of its own, not Wasm 1.0 plus bulk memory, so the features
# outside it are cleared as the version presets clear theirs.
if (WAMR_BUILD_LIME1 EQUAL 1)
  wamr_preset_exclude (WAMR_BUILD_BULK_MEMORY)
  wamr_preset_exclude (WAMR_BUILD_REF_TYPES)
  wamr_preset_exclude (WAMR_BUILD_SIMD)
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
  # A Wasm 1.0 runtime has no post-1.0 proposal.  Of the ones WAMR can switch,
  # bulk memory is on by default in this file and reference types and SIMD are
  # on by default in the entry points, so all three are cleared here.
  wamr_preset_exclude (WAMR_BUILD_BULK_MEMORY)
  wamr_preset_exclude (WAMR_BUILD_REF_TYPES)
  wamr_preset_exclude (WAMR_BUILD_SIMD)
endif ()

# GC, stringref and the stringref implementation are one chain: stringref is
# built on GC, and it needs an implementation to link against.  Each link turns
# the next one on, so asking for GC is enough, and asking for a later link
# without its predecessor is a configuration error rather than a silent
# promotion -- a build that says WAMR_BUILD_GC=0 WAMR_BUILD_STRINGREF=1 wants
# two different things.
if (DEFINED WAMR_STRINGREF_IMPL_SOURCE
    AND DEFINED WAMR_BUILD_STRINGREF
    AND NOT WAMR_BUILD_STRINGREF EQUAL 1)
    message (FATAL_ERROR
      "WAMR_STRINGREF_IMPL_SOURCE is set but WAMR_BUILD_STRINGREF is "
      "${WAMR_BUILD_STRINGREF}: the implementation is what stringref links "
      "against, so it says nothing on its own. Set WAMR_BUILD_STRINGREF=1, or "
      "drop WAMR_STRINGREF_IMPL_SOURCE.")
endif ()

if (DEFINED WAMR_STRINGREF_IMPL_SOURCE AND NOT DEFINED WAMR_BUILD_STRINGREF)
    set (WAMR_BUILD_STRINGREF 1)
endif ()

if (WAMR_BUILD_STRINGREF EQUAL 1
    AND DEFINED WAMR_BUILD_GC
    AND NOT WAMR_BUILD_GC EQUAL 1)
    message (FATAL_ERROR
      "WAMR_BUILD_STRINGREF=1 needs WAMR_BUILD_GC=1: stringref values are GC "
      "objects. Set WAMR_BUILD_GC=1, or drop WAMR_BUILD_STRINGREF.")
endif ()

if (WAMR_BUILD_STRINGREF EQUAL 1)
    set (WAMR_BUILD_GC 1)
endif ()

if (WAMR_BUILD_GC EQUAL 1 AND NOT DEFINED WAMR_BUILD_STRINGREF)
    set (WAMR_BUILD_STRINGREF 1)
endif ()

if (WAMR_BUILD_STRINGREF EQUAL 1 AND NOT DEFINED WAMR_STRINGREF_IMPL_SOURCE)
    # The builtin implementation; a custom one is given by path.
    set (WAMR_STRINGREF_IMPL_SOURCE "STUB")
endif ()

################ optional according to settings ################
if (WAMR_BUILD_FAST_JIT EQUAL 1 OR WAMR_BUILD_JIT EQUAL 1)
    # Enable classic interpreter if Fast JIT or LLVM JIT is enabled
    set (WAMR_BUILD_INTERP 1)
    set (WAMR_BUILD_FAST_INTERP 0)
endif ()

if (WAMR_BUILD_INTERP EQUAL 1)
    include (${IWASM_DIR}/interpreter/iwasm_interp.cmake)
endif ()

if (WAMR_BUILD_FAST_JIT EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
        message ("Fast JIT currently not supported on Windows")
        set (WAMR_BUILD_FAST_JIT 0)
    else ()
        include (${IWASM_DIR}/fast-jit/iwasm_fast_jit.cmake)
    endif ()
endif ()

if (WAMR_BUILD_JIT EQUAL 1)
    # Enable AOT if LLVM JIT is enabled
    set (WAMR_BUILD_AOT 1)
    include (${IWASM_DIR}/compilation/iwasm_compl.cmake)
endif ()

if (WAMR_BUILD_AOT EQUAL 1)
    include (${IWASM_DIR}/aot/iwasm_aot.cmake)
endif ()

if (WAMR_BUILD_GC EQUAL 1)
    include (${IWASM_DIR}/common/gc/iwasm_gc.cmake)
    # Enable the dependent feature if GC is enabled
    set (WAMR_BUILD_REF_TYPES 1)
endif ()

if (WAMR_BUILD_LIBC_BUILTIN EQUAL 1)
    include (${IWASM_DIR}/libraries/libc-builtin/libc_builtin.cmake)
endif ()

if (WAMR_BUILD_LIBC_UVWASI EQUAL 1)
    include (${IWASM_DIR}/libraries/libc-uvwasi/libc_uvwasi.cmake)
    set (WAMR_BUILD_MODULE_INST_CONTEXT 1)
elseif (WAMR_BUILD_LIBC_WASI EQUAL 1)
    include (${IWASM_DIR}/libraries/libc-wasi/libc_wasi.cmake)
    set (WAMR_BUILD_MODULE_INST_CONTEXT 1)
endif ()

if (WAMR_BUILD_LIB_PTHREAD_SEMAPHORE EQUAL 1)
    # Enable the dependent feature if lib pthread semaphore is enabled
    set (WAMR_BUILD_LIB_PTHREAD 1)
endif ()

if (WAMR_BUILD_WASI_NN EQUAL 1)
    include (${IWASM_DIR}/libraries/wasi-nn/cmake/wasi_nn.cmake)
    set (WAMR_BUILD_MODULE_INST_CONTEXT 1)
endif ()

if (WAMR_BUILD_LIB_PTHREAD EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
        set (WAMR_BUILD_LIB_PTHREAD_SEMAPHORE 0)
        message ("Lib pthread semaphore currently not supported on Windows")
    endif ()
    include (${IWASM_DIR}/libraries/lib-pthread/lib_pthread.cmake)
    # Enable the dependent feature if lib pthread is enabled
    set (WAMR_BUILD_THREAD_MGR 1)
    set (WAMR_BUILD_BULK_MEMORY 1)
    set (WAMR_BUILD_SHARED_MEMORY 1)
endif ()

if (WAMR_BUILD_LIB_WASI_THREADS EQUAL 1)
    include (${IWASM_DIR}/libraries/lib-wasi-threads/lib_wasi_threads.cmake)
    # Enable the dependent feature if lib wasi threads is enabled
    set (WAMR_BUILD_THREAD_MGR 1)
    set (WAMR_BUILD_BULK_MEMORY 1)
    set (WAMR_BUILD_SHARED_MEMORY 1)
endif ()

if (WAMR_BUILD_SHARED_HEAP EQUAL 1)
    include (${IWASM_DIR}/libraries/shared-heap/shared_heap.cmake)
endif ()

if (WAMR_BUILD_DEBUG_INTERP EQUAL 1)
    set (WAMR_BUILD_THREAD_MGR 1)
    include (${IWASM_DIR}/libraries/debug-engine/debug_engine.cmake)

    if (WAMR_BUILD_FAST_INTERP EQUAL 1)
        set (WAMR_BUILD_FAST_INTERP 0)
        message(STATUS
                "Debugger doesn't work with fast interpreter, switch to classic interpreter")
    endif ()
endif ()

if (WAMR_BUILD_THREAD_MGR EQUAL 1)
    include (${IWASM_DIR}/libraries/thread-mgr/thread_mgr.cmake)
endif ()

if (WAMR_BUILD_LIBC_EMCC EQUAL 1)
    include (${IWASM_DIR}/libraries/libc-emcc/libc_emcc.cmake)
endif ()

if (WAMR_BUILD_LIB_RATS EQUAL 1)
    include (${IWASM_DIR}/libraries/lib-rats/lib_rats.cmake)
endif ()

if (WAMR_BUILD_WASM_CACHE EQUAL 1)
    include (${WAMR_ROOT_DIR}/build-scripts/involve_boringssl.cmake)
endif ()

####################### Common sources #######################
if (NOT MSVC)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -ffunction-sections -fdata-sections")
endif ()

# include the build config template file
include (${CMAKE_CURRENT_LIST_DIR}/config_common.cmake)

if (WAMR_BUILD_SIMD EQUAL 1 AND WAMR_BUILD_FAST_INTERP EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
        message(STATUS "SIMDe doesnt support platform " ${WAMR_BUILD_PLATFORM})
        set(WAMR_BUILD_SIMDE 0)
    else()
        include (${IWASM_DIR}/libraries/simde/simde.cmake)
        set (WAMR_BUILD_SIMDE 1)
    endif()
else()
    set(WAMR_BUILD_SIMDE 0)
endif ()

include_directories (${IWASM_DIR}/include)

file (GLOB header
    ${IWASM_DIR}/include/*.h
)
LIST (APPEND RUNTIME_LIB_HEADER_LIST ${header})

if (WAMR_BUILD_PLATFORM STREQUAL "windows")
    enable_language (ASM_MASM)
else()
    enable_language (ASM)
endif()

# it will expose the runtime APIs.
# you'll use the following command to check the exported APIs
# dumpbin.exe /EXPORTS xxx
if (MSVC)
    add_compile_definitions(COMPILING_WASM_RUNTIME_API=1)
endif ()

include (${SHARED_PLATFORM_CONFIG})
include (${SHARED_DIR}/mem-alloc/mem_alloc.cmake)
include (${IWASM_DIR}/common/iwasm_common.cmake)
include (${SHARED_DIR}/utils/shared_utils.cmake)


set (source_all
    ${PLATFORM_SHARED_SOURCE}
    ${MEM_ALLOC_SHARED_SOURCE}
    ${UTILS_SHARED_SOURCE}
    ${LIBC_BUILTIN_SOURCE}
    ${LIBC_WASI_SOURCE}
    ${WASI_NN_SOURCES}
    ${IWASM_COMMON_SOURCE}
    ${IWASM_INTERP_SOURCE}
    ${IWASM_AOT_SOURCE}
    ${IWASM_COMPL_SOURCE}
    ${IWASM_FAST_JIT_SOURCE}
    ${IWASM_GC_SOURCE}
    ${LIB_WASI_THREADS_SOURCE}
    ${LIB_PTHREAD_SOURCE}
    ${THREAD_MGR_SOURCE}
    ${LIBC_EMCC_SOURCE}
    ${LIB_RATS_SOURCE}
    ${DEBUG_ENGINE_SOURCE}
    ${LIB_SHARED_HEAP_SOURCE}
)

set (WAMR_RUNTIME_LIB_SOURCE ${source_all})

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (IWASM_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories (${IWASM_COMMON_DIR})
if (MSVC AND WAMR_BUILD_PLATFORM STREQUAL "windows" AND WAMR_BUILD_TARGET MATCHES "AARCH64.*" AND NOT WAMR_BUILD_WAMR_COMPILER)
  if (CMAKE_ASM_MARMASM_COMPILER)
    # Use the already detected assembler
    set(_ARMASM64_EXE "${CMAKE_ASM_MARMASM_COMPILER}")
  elseif (DEFINED ENV{VCToolsInstallDir} OR VCToolsInstallDir)
    if (NOT VCToolsInstallDir)
      set(VCToolsInstallDir "$ENV{VCToolsInstallDir}")
    endif()
    # Detect host tool dir
    set(_ARMASM64_CANDIDATES
        "${VCToolsInstallDir}bin/Hostx64/arm64/armasm64.exe"
        "${VCToolsInstallDir}bin/Hostarm64/arm64/armasm64.exe")
    set(_ARMASM64_EXE "")
    foreach(_p IN LISTS _ARMASM64_CANDIDATES)
      if (EXISTS "${_p}")
        set(_ARMASM64_EXE "${_p}")
        break()
      endif()
    endforeach()
    if (_ARMASM64_EXE STREQUAL "")
      message(FATAL_ERROR "armasm64.exe not found under VCToolsInstallDir: ${VCToolsInstallDir}")
    endif()
  else()
    message(FATAL_ERROR "VCToolsInstallDir is not defined. Please run from a Developer Command Prompt or specify armasm64.exe manually.")
  endif()

  # Wrapper without spaces to avoid quoting hell on NMake/cmd.exe
  set(_WRAP "${CMAKE_BINARY_DIR}/armasm64_wrapper.bat")
  file(WRITE "${_WRAP}"
"@echo off\r\n"
"setlocal enabledelayedexpansion\r\n"
"set ARGS=\r\n"
"for %%A in (%*) do (\r\n"
"  if /I not \"%%~A\"==\"/experimental:c11atomics\" (\r\n"
"    set ARGS=!ARGS! %%A\r\n"
"  )\r\n"
")\r\n"
"\"${_ARMASM64_EXE}\" !ARGS!\r\n")

  # Use wrapper as compiler (no spaces in path)
  set(CMAKE_ASM_MASM_COMPILER
      "${_WRAP}"
      CACHE FILEPATH "" FORCE)

  set(CMAKE_ASM_MARMASM_COMPILER
      "${_WRAP}"
      CACHE FILEPATH "" FORCE)

  # Quote ONLY object and source (compiler path has no spaces now)
  set(CMAKE_ASM_MASM_COMPILE_OBJECT
      "<CMAKE_ASM_MASM_COMPILER> /nologo -o \"<OBJECT>\" \"<SOURCE>\""
      CACHE STRING "" FORCE)

  set(CMAKE_ASM_MARMASM_COMPILE_OBJECT
      "<CMAKE_ASM_MARMASM_COMPILER> /nologo -o \"<OBJECT>\" \"<SOURCE>\""
      CACHE STRING "" FORCE)
endif()

add_definitions(-DBH_MALLOC=wasm_runtime_malloc)
add_definitions(-DBH_FREE=wasm_runtime_free)

file (GLOB c_source_all ${IWASM_COMMON_DIR}/*.c)

if (WAMR_DISABLE_APP_ENTRY EQUAL 1)
  list(REMOVE_ITEM c_source_all "${IWASM_COMMON_DIR}/wasm_application.c")
endif ()

if (CMAKE_OSX_ARCHITECTURES)
  string(TOLOWER "${CMAKE_OSX_ARCHITECTURES}" OSX_ARCHS)

  list(FIND OSX_ARCHS arm64 OSX_AARCH64)
  list(FIND OSX_ARCHS x86_64 OSX_X86_64)

  if (NOT "${OSX_AARCH64}" STREQUAL "-1" AND NOT "${OSX_X86_64}" STREQUAL "-1")
    set(OSX_UNIVERSAL_BUILD 1)
  endif()
endif()

if (WAMR_BUILD_INVOKE_NATIVE_GENERAL EQUAL 1 AND NOT (MSVC AND WAMR_BUILD_PLATFORM STREQUAL "windows" AND WAMR_BUILD_TARGET MATCHES "AARCH64.*"))
  # Use invokeNative C version instead of asm code version
  # if WAMR_BUILD_INVOKE_NATIVE_GENERAL is explicitly set.
  # Note:
  #   the maximum number of native arguments is limited to 20,
  #   and there are possible issues when passing arguments to
  #   native function for some cpus, e.g. int64 and double arguments
  #   in arm and mips need to be 8-bytes aligned, and some arguments
  #   of x86_64 are passed by registers but not stack
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_general.c)
elseif (OSX_UNIVERSAL_BUILD EQUAL 1)
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_osx_universal.s)
elseif (WAMR_BUILD_TARGET STREQUAL "X86_64" OR WAMR_BUILD_TARGET STREQUAL "AMD_64")
  if (NOT WAMR_BUILD_SIMD EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
      if (NOT MINGW)
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_em64.asm)
      else ()
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_mingw_x64.s)
      endif ()
    else ()
      set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_em64.s)
    endif ()
  else ()
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
      if (NOT MINGW)
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_em64_simd.asm)
      else ()
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_mingw_x64_simd.s)
      endif ()
    else()
      set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_em64_simd.s)
    endif()
  endif ()
elseif (WAMR_BUILD_TARGET STREQUAL "X86_32")
  if (WAMR_BUILD_PLATFORM STREQUAL "windows")
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_ia32.asm)
  else ()
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_ia32.s)
  endif ()
elseif (WAMR_BUILD_TARGET MATCHES "ARM.*")
  if (WAMR_BUILD_TARGET MATCHES "ARM.*_VFP")
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_arm_vfp.s)
  else ()
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_arm.s)
  endif ()
elseif (WAMR_BUILD_TARGET MATCHES "THUMB.*")
  if (WAMR_BUILD_TARGET MATCHES "THUMB.*_VFP")
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_thumb_vfp.s)
  else ()
    set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_thumb.s)
  endif ()
elseif (WAMR_BUILD_TARGET MATCHES "AARCH64.*")
  if (NOT WAMR_BUILD_SIMD EQUAL 1)
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
      if (MSVC)
        set(_WAMR_ARM64_MASM_SOURCE ${IWASM_COMMON_DIR}/arch/invokeNative_armasm64.asm)
        set(_WAMR_ARM64_MASM_OBJ ${CMAKE_CURRENT_BINARY_DIR}/invokeNative_armasm64.obj)
        add_custom_command(
          OUTPUT ${_WAMR_ARM64_MASM_OBJ}
          COMMAND ${CMAKE_ASM_MASM_COMPILER} /nologo -o "${_WAMR_ARM64_MASM_OBJ}" "${_WAMR_ARM64_MASM_SOURCE}"
          DEPENDS ${_WAMR_ARM64_MASM_SOURCE}
          COMMENT "Assembling invokeNative_armasm64.asm"
          VERBATIM
        )
        set_source_files_properties(${_WAMR_ARM64_MASM_OBJ}
          PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE
        )
        set (source_all ${c_source_all} ${_WAMR_ARM64_MASM_OBJ})
      else ()
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_aarch64.s)
      endif ()
    else ()
      set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_aarch64.s)
    endif ()
  else()
    if (WAMR_BUILD_PLATFORM STREQUAL "windows")
      if (MSVC)
        set(_WAMR_ARM64_MASM_SIMD_SOURCE ${IWASM_COMMON_DIR}/arch/invokeNative_armasm64_simd.asm)
        set(_WAMR_ARM64_MASM_SIMD_OBJ ${CMAKE_CURRENT_BINARY_DIR}/invokeNative_armasm64_simd.obj)
        add_custom_command(
          OUTPUT ${_WAMR_ARM64_MASM_SIMD_OBJ}
          COMMAND ${CMAKE_ASM_MASM_COMPILER} /nologo -o "${_WAMR_ARM64_MASM_SIMD_OBJ}" "${_WAMR_ARM64_MASM_SIMD_SOURCE}"
          DEPENDS ${_WAMR_ARM64_MASM_SIMD_SOURCE}
          COMMENT "Assembling invokeNative_armasm64_simd.asm"
          VERBATIM
        )
        set_source_files_properties(${_WAMR_ARM64_MASM_SIMD_OBJ}
          PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE
        )
        set (source_all ${c_source_all} ${_WAMR_ARM64_MASM_SIMD_OBJ})
      else ()
        set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_aarch64_simd.s)
      endif ()
    else ()
      set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_aarch64_simd.s)
    endif ()
  endif()
elseif (WAMR_BUILD_TARGET STREQUAL "MIPS")
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_mips.s)
elseif (WAMR_BUILD_TARGET STREQUAL "XTENSA")
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_xtensa.s)
elseif (WAMR_BUILD_TARGET MATCHES "RISCV*")
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_riscv.S)
elseif (WAMR_BUILD_TARGET STREQUAL "ARC")
  set (source_all ${c_source_all} ${IWASM_COMMON_DIR}/arch/invokeNative_arc.s)
else ()
  message (FATAL_ERROR "Build target isn't set")
endif ()

set (IWASM_COMMON_SOURCE ${source_all})


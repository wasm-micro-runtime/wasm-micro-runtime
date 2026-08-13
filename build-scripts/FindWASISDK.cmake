# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(FindPackageHandleStandardArgs)

if(NOT WASISDK_ROOT)
  set(WASISDK_ROOT "${WASI_SDK_DIR}")
endif()

file(GLOB _WASISDK_DEFAULT_PATHS "/opt/wasi-sdk-*")
find_path(WASISDK_HOME
  NAMES share/wasi-sysroot
  HINTS "${WASISDK_ROOT}" "$ENV{WASISDK_ROOT}" "$ENV{WASI_SDK_DIR}"
  PATHS /opt/wasi-sdk ${_WASISDK_DEFAULT_PATHS}
)

if(WASISDK_HOME)
  find_program(WASISDK_CC_COMMAND NAMES clang HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_CXX_COMMAND NAMES clang++ HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_WASM_LD NAMES wasm-ld HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_LLVM_AR NAMES llvm-ar HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_LLVM_NM NAMES llvm-nm HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_LLVM_STRIP NAMES llvm-strip HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_LLVM_DWARFDUMP NAMES llvm-dwarfdump HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)
  find_program(WASISDK_LLVM_RANLIB NAMES llvm-ranlib HINTS "${WASISDK_HOME}/bin" NO_DEFAULT_PATH)

  set(WASISDK_SYSROOT "${WASISDK_HOME}/share/wasi-sysroot")
  set(WASISDK_TOOLCHAIN "${WASISDK_HOME}/share/cmake/wasi-sdk.cmake")
  set(WASISDK_PTHREAD_TOOLCHAIN "${WASISDK_HOME}/share/cmake/wasi-sdk-pthread.cmake")

  if(WASISDK_CC_COMMAND)
    execute_process(
      COMMAND "${WASISDK_CC_COMMAND}" --version
      OUTPUT_VARIABLE _WASISDK_VERSION_OUTPUT
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" WASISDK_VERSION "${_WASISDK_VERSION_OUTPUT}")
  endif()
endif()

find_package_handle_standard_args(WASISDK
  REQUIRED_VARS WASISDK_HOME WASISDK_CC_COMMAND WASISDK_CXX_COMMAND
  VERSION_VAR WASISDK_VERSION
)

if(WASISDK_FOUND)
  foreach(_tool clang clang++ wasm-ld llvm-ar llvm-nm llvm-strip llvm-dwarfdump llvm-ranlib)
    string(TOUPPER "${_tool}" _tool_upper)
    string(REPLACE "-" "_" _tool_upper "${_tool_upper}")
    if(_tool STREQUAL "clang")
      set(_WASISDK_TOOL "${WASISDK_CC_COMMAND}")
    elseif(_tool STREQUAL "clang++")
      set(_WASISDK_TOOL "${WASISDK_CXX_COMMAND}")
    else()
      set(_WASISDK_TOOL "${WASISDK_${_tool_upper}}")
    endif()
    if(_WASISDK_TOOL AND NOT TARGET WASISDK::${_tool})
      add_executable(WASISDK::${_tool} IMPORTED)
      set_target_properties(WASISDK::${_tool} PROPERTIES
        IMPORTED_LOCATION "${_WASISDK_TOOL}"
      )
    endif()
  endforeach()
endif()

mark_as_advanced(
  WASISDK_HOME WASISDK_CC_COMMAND WASISDK_CXX_COMMAND WASISDK_WASM_LD
  WASISDK_LLVM_AR WASISDK_LLVM_NM WASISDK_LLVM_STRIP WASISDK_LLVM_DWARFDUMP
  WASISDK_LLVM_RANLIB WASISDK_SYSROOT WASISDK_TOOLCHAIN
  WASISDK_PTHREAD_TOOLCHAIN
)

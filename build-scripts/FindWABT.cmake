# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(FindPackageHandleStandardArgs)

file(GLOB _WABT_DEFAULT_PATHS "/opt/wabt*")
set(_WABT_HINTS
  "${WABT_ROOT}" "$ENV{WABT_ROOT}" "${WABT_DIR}"
  /opt/wabt ${_WABT_DEFAULT_PATHS}
)

find_program(WABT_WAT2WASM NAMES wat2wasm HINTS ${_WABT_HINTS} PATH_SUFFIXES bin)
find_program(WABT_WASM2WAT NAMES wasm2wat HINTS ${_WABT_HINTS} PATH_SUFFIXES bin)
find_program(WABT_WASM_OBJDUMP NAMES wasm-objdump HINTS ${_WABT_HINTS} PATH_SUFFIXES bin)

foreach(_component wat2wasm wasm2wat wasm-objdump)
  string(TOUPPER "${_component}" _component_upper)
  string(REPLACE "-" "_" _component_upper "${_component_upper}")
  if(WABT_${_component_upper})
    set(WABT_${_component}_FOUND TRUE)
    if(NOT WABT_DIR)
      get_filename_component(WABT_DIR "${WABT_${_component_upper}}" DIRECTORY)
      get_filename_component(WABT_DIR "${WABT_DIR}" DIRECTORY)
    endif()
  else()
    set(WABT_${_component}_FOUND FALSE)
  endif()
endforeach()

if(WABT_WAT2WASM)
  execute_process(
    COMMAND "${WABT_WAT2WASM}" --version
    OUTPUT_VARIABLE _WABT_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" WABT_VERSION "${_WABT_VERSION_OUTPUT}")
endif()

find_package_handle_standard_args(WABT
  REQUIRED_VARS WABT_DIR
  VERSION_VAR WABT_VERSION
  HANDLE_COMPONENTS
)

foreach(_tool WAT2WASM WASM2WAT WASM_OBJDUMP)
  string(TOLOWER "${_tool}" _target_name)
  string(REPLACE "_" "-" _target_name "${_target_name}")
  if(WABT_${_tool} AND NOT TARGET WABT::${_target_name})
    add_executable(WABT::${_target_name} IMPORTED)
    set_target_properties(WABT::${_target_name} PROPERTIES
      IMPORTED_LOCATION "${WABT_${_tool}}"
    )
  endif()
endforeach()

# Compatibility variables used by existing CMakeLists.txt files.
set(WAT2WASM "${WABT_WAT2WASM}")
set(WASM2WAT "${WABT_WASM2WAT}")
set(WASM_OBJDUMP "${WABT_WASM_OBJDUMP}")
set(WAT2WASM_VERSION "${WABT_VERSION}")
mark_as_advanced(WABT_DIR WABT_WAT2WASM WABT_WASM2WAT WABT_WASM_OBJDUMP)

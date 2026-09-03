# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(FindPackageHandleStandardArgs)

file(GLOB _BINARYEN_DEFAULT_PATHS "/opt/binaryen*")
find_program(Binaryen_WASM_OPT
  NAMES wasm-opt
  HINTS
    "${Binaryen_ROOT}" "$ENV{Binaryen_ROOT}"
    "${BINARYEN_ROOT}" "$ENV{BINARYEN_ROOT}"
    "${BINARYEN_PATH}" "$ENV{BINARYEN_PATH}"
    /opt/binaryen ${_BINARYEN_DEFAULT_PATHS}
  PATH_SUFFIXES bin
)

if(Binaryen_WASM_OPT)
  get_filename_component(Binaryen_HOME "${Binaryen_WASM_OPT}" DIRECTORY)
  get_filename_component(Binaryen_HOME "${Binaryen_HOME}" DIRECTORY)
  execute_process(
    COMMAND "${Binaryen_WASM_OPT}" --version
    OUTPUT_VARIABLE _Binaryen_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "[0-9]+" Binaryen_VERSION "${_Binaryen_VERSION_OUTPUT}")
endif()

find_package_handle_standard_args(Binaryen
  REQUIRED_VARS Binaryen_WASM_OPT
  VERSION_VAR Binaryen_VERSION
)

if(Binaryen_FOUND AND NOT TARGET Binaryen::wasm-opt)
  add_executable(Binaryen::wasm-opt IMPORTED)
  set_target_properties(Binaryen::wasm-opt PROPERTIES
    IMPORTED_LOCATION "${Binaryen_WASM_OPT}"
  )
endif()

mark_as_advanced(Binaryen_HOME Binaryen_WASM_OPT)

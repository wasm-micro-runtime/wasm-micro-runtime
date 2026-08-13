# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(FindPackageHandleStandardArgs)

get_filename_component(_WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_program(WAMRC_BIN
  NAMES wamrc
  HINTS
    "${WAMRC_ROOT}"
    "${WAMRC_ROOT}/bin"
    "${WAMRC_ROOT}/build"
    "$ENV{WAMRC_ROOT}"
    "$ENV{WAMRC_ROOT}/bin"
    "$ENV{WAMRC_ROOT}/build"
    "${_WAMR_ROOT_DIR}/wamr-compiler/build"
)

if(WAMRC_BIN)
  execute_process(
    COMMAND "${WAMRC_BIN}" --version
    OUTPUT_VARIABLE _WAMRC_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" WAMRC_VERSION "${_WAMRC_VERSION_OUTPUT}")
endif()

find_package_handle_standard_args(WAMRC
  REQUIRED_VARS WAMRC_BIN
  VERSION_VAR WAMRC_VERSION
  FAIL_MESSAGE "Could NOT find wamrc. Build it first with: cmake -S wamr-compiler -B wamr-compiler/build && cmake --build wamr-compiler/build"
)

if(WAMRC_FOUND AND NOT TARGET WAMRC::wamrc)
  add_executable(WAMRC::wamrc IMPORTED)
  set_target_properties(WAMRC::wamrc PROPERTIES IMPORTED_LOCATION "${WAMRC_BIN}")
endif()

mark_as_advanced(WAMRC_BIN)

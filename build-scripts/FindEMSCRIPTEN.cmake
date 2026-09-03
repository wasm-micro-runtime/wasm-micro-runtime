# Copyright (C) 2026 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

include(FindPackageHandleStandardArgs)

file(GLOB _EMSCRIPTEN_DEFAULT_PATHS "/opt/emsdk*")
find_path(EMSCRIPTEN_HOME
  NAMES upstream/emscripten/emcc
  HINTS
    "${EMSCRIPTEN_ROOT}" "$ENV{EMSCRIPTEN_ROOT}"
    "${EMSDK_ROOT}" "$ENV{EMSDK_ROOT}"
    "${EMSDK}" "$ENV{EMSDK}"
  PATHS /opt/emsdk ${_EMSCRIPTEN_DEFAULT_PATHS}
)

if(EMSCRIPTEN_HOME)
  set(_EMSCRIPTEN_BIN_DIR "${EMSCRIPTEN_HOME}/upstream/emscripten")
  find_program(EMCC NAMES emcc HINTS "${_EMSCRIPTEN_BIN_DIR}" NO_DEFAULT_PATH)
  find_file(EMSCRIPTEN_TOOLCHAIN
    NAMES cmake/Modules/Platform/Emscripten.cmake
    HINTS "${_EMSCRIPTEN_BIN_DIR}"
    NO_DEFAULT_PATH
  )
  find_file(_EMSCRIPTEN_VERSION_FILE
    NAMES emscripten-version.txt
    HINTS "${_EMSCRIPTEN_BIN_DIR}"
    NO_DEFAULT_PATH
  )

  if(_EMSCRIPTEN_VERSION_FILE)
    file(READ "${_EMSCRIPTEN_VERSION_FILE}" _EMSCRIPTEN_VERSION_CONTENT)
    string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" EMSCRIPTEN_VERSION
      "${_EMSCRIPTEN_VERSION_CONTENT}")
  endif()
endif()

find_package_handle_standard_args(EMSCRIPTEN
  REQUIRED_VARS EMSCRIPTEN_HOME EMCC EMSCRIPTEN_TOOLCHAIN
  VERSION_VAR EMSCRIPTEN_VERSION
  HANDLE_VERSION_RANGE
)

if(EMSCRIPTEN_FOUND AND NOT TARGET EMSCRIPTEN::emcc)
  add_executable(EMSCRIPTEN::emcc IMPORTED)
  set_target_properties(EMSCRIPTEN::emcc PROPERTIES IMPORTED_LOCATION "${EMCC}")
endif()

mark_as_advanced(EMSCRIPTEN_HOME EMCC EMSCRIPTEN_TOOLCHAIN)

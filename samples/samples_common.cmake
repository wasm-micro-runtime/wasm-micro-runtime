# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Shared helpers for samples.
#
# Two mechanisms are provided:
#
# 1. samples_add_test()     - register a ctest test that runs in the sample's
#                             build directory, with an optional
#                             PASS_REGULAR_EXPRESSION assertion.
# 2. samples_build_wasm_app()- build a sample's wasm application through a
#                             standalone CMake project (wasm-apps/) driven by
#                             ExternalProject, and install the artifacts into
#                             the sample's build directory. This is the
#                             standard way to produce .wasm/.aot files; each
#                             sample's wasm-apps/CMakeLists.txt is
#                             self-contained (it finds the tools it needs and
#                             defines its own targets).
#
# Usage (from a sample CMakeLists.txt):
#   include(${CMAKE_CURRENT_LIST_DIR}/../samples_common.cmake)
#   samples_add_test(<name> <pass-regex|""> <command...>)
#   samples_build_wasm_app(<target-name> <wasm-dir> [<extra-cmake-arg>...])

# The repo root is two levels above samples/ (samples_common.cmake lives in
# samples/).
set(SAMPLES_WAMR_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../..")

function(samples_add_test name regex)
  # samples_add_test(<name> <pass-regex|""> <command...>)
  #   <name>       - test name (must be unique within this sample)
  #   <pass-regex> - PASS_REGULAR_EXPRESSION; pass "" to skip the assertion
  #   <command...> - test command (e.g. $<TARGET_FILE:xxx> args...)
  add_test(NAME ${name} COMMAND ${ARGN})
  set_tests_properties(${name} PROPERTIES
                       WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
                       TIMEOUT 600)
  if(NOT regex STREQUAL "")
    set_tests_properties(${name} PROPERTIES PASS_REGULAR_EXPRESSION "${regex}")
  endif()
endfunction()

function(samples_build_wasm_app target_name wasm_dir)
  # samples_build_wasm_app(<target-name> <wasm-dir> [DEPENDS <target...>]
  #                        [CONFIGURE_ARGS <arg...>])
  #   <target-name>     - ExternalProject target name (unique per sample)
  #   <wasm-dir>        - directory holding the wasm-apps CMake project,
  #                       relative to the sample source dir (e.g. "wasm-apps")
  #   DEPENDS           - host targets the wasm build depends on (built first)
  #   CONFIGURE_ARGS    - extra -D arguments passed to the sub-project
  #
  # Builds the standalone CMake project in <wasm-dir> with ExternalProject
  # (isolated flags: wasm builds must not inherit host compiler flags) and
  # installs its artifacts into <build>/<wasm-dir>/ so tests can reference
  # them with a path relative to the build directory.
  #
  # The sub-project (wasm-dir/CMakeLists.txt) is self-contained: it locates
  # the tools it needs (wasi-sdk / wabt / wamrc via build-scripts/*.cmake)
  # and install()s the produced .wasm/.aot files.
  cmake_parse_arguments(WASM "" "" "DEPENDS;CONFIGURE_ARGS" ${ARGN})
  include(ExternalProject)

  set(EP_ARGS
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${wasm_dir}"
    CONFIGURE_COMMAND ${CMAKE_COMMAND}
      -S "${CMAKE_CURRENT_SOURCE_DIR}/${wasm_dir}"
      -B "${CMAKE_CURRENT_BINARY_DIR}/${wasm_dir}"
      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
      ${WASM_CONFIGURE_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND}
      --build "${CMAKE_CURRENT_BINARY_DIR}/${wasm_dir}"
    INSTALL_COMMAND ${CMAKE_COMMAND}
      -E env DESTDIR=
      ${CMAKE_COMMAND} --install "${CMAKE_CURRENT_BINARY_DIR}/${wasm_dir}"
      --prefix "${CMAKE_CURRENT_BINARY_DIR}"
  )
  if(WASM_DEPENDS)
    list(APPEND EP_ARGS DEPENDS ${WASM_DEPENDS})
  endif()
  ExternalProject_Add(${target_name} ${EP_ARGS})
endfunction()

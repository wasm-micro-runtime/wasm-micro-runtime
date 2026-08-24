# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set(WAMR_UNIT_TEST_ALL_RUN_MODES
    classic-interp fast-interp llvm-jit fast-jit aot multi-tier-jit)

# Determine the single runtime mode selected for this unit-test configure.
function(wamr_unit_test_get_current_run_mode out_var)
  if(WAMR_BUILD_AOT AND
     (WAMR_BUILD_FAST_INTERP OR WAMR_BUILD_JIT OR WAMR_BUILD_FAST_JIT))
    message(FATAL_ERROR
            "Multiple unit test runtime modes are enabled: "
            "WAMR_BUILD_AOT cannot be combined with fast interpreter or JIT options")
  endif()

  if(WAMR_BUILD_FAST_INTERP AND (WAMR_BUILD_JIT OR WAMR_BUILD_FAST_JIT))
    message(FATAL_ERROR
            "Multiple unit test runtime modes are enabled: "
            "WAMR_BUILD_FAST_INTERP cannot be combined with JIT options")
  endif()

  if(WAMR_BUILD_FAST_INTERP AND NOT WAMR_BUILD_INTERP)
    message(FATAL_ERROR
            "WAMR_BUILD_FAST_INTERP requires WAMR_BUILD_INTERP")
  endif()

  if(WAMR_BUILD_JIT AND WAMR_BUILD_FAST_JIT)
    set(_run_mode multi-tier-jit)
  elseif(WAMR_BUILD_FAST_JIT)
    set(_run_mode fast-jit)
  elseif(WAMR_BUILD_JIT)
    set(_run_mode llvm-jit)
  elseif(WAMR_BUILD_AOT)
    set(_run_mode aot)
  elseif(WAMR_BUILD_FAST_INTERP)
    set(_run_mode fast-interp)
  elseif(WAMR_BUILD_INTERP)
    set(_run_mode classic-interp)
  else()
    message(FATAL_ERROR
            "No unit test runtime mode is enabled: "
            "enable interpreter, AOT, JIT, or fast JIT")
  endif()

  set(${out_var} ${_run_mode} PARENT_SCOPE)
endfunction()

# Enable a suite only when the current runtime mode is in its supported list.
function(wamr_unit_test_suite_run_modes suite_name)
  cmake_parse_arguments(ARG "" "" "MODES" ${ARGN})
  if(NOT ARG_MODES)
    message(FATAL_ERROR
            "wamr_unit_test_suite_run_modes(${suite_name}) requires MODES")
  endif()

  if(ARG_MODES STREQUAL "none")
    set(WAMR_UNIT_TEST_SUITE_ENABLED FALSE PARENT_SCOPE)
    message(STATUS "Skipping ${suite_name}: no supported unit test run mode")
    return()
  endif()

  foreach(_mode IN LISTS ARG_MODES)
    list(FIND WAMR_UNIT_TEST_ALL_RUN_MODES ${_mode} _mode_index)
    if(_mode_index EQUAL -1)
      message(FATAL_ERROR "Unknown unit test run mode: ${_mode}")
    endif()
  endforeach()

  wamr_unit_test_get_current_run_mode(_current_run_mode)
  list(FIND ARG_MODES ${_current_run_mode} _current_mode_index)
  if(_current_mode_index EQUAL -1)
    string(REPLACE ";" ", " _supported_modes "${ARG_MODES}")
    message(STATUS
            "Skipping ${suite_name}: supports ${_supported_modes}; current mode is ${_current_run_mode}")
    set(WAMR_UNIT_TEST_SUITE_ENABLED FALSE PARENT_SCOPE)
  else()
    message(STATUS "Enabling ${suite_name}: current mode is ${_current_run_mode}")
    set(WAMR_UNIT_TEST_SUITE_ENABLED TRUE PARENT_SCOPE)
  endif()
endfunction()

# Add a post-build command that copies a directory or selected wasm files.
function(wamr_unit_test_copy_wasm_files target_name)
  cmake_parse_arguments(ARG "" "SOURCE_DIR;DEST_DIR;COMMENT" "FILES" ${ARGN})
  if(NOT ARG_DEST_DIR)
    set(ARG_DEST_DIR ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  if(ARG_SOURCE_DIR AND ARG_FILES)
    message(FATAL_ERROR
            "wamr_unit_test_copy_wasm_files(${target_name}) accepts SOURCE_DIR or FILES, not both")
  endif()
  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "Copy wasm files for ${target_name}")
  endif()

  if(ARG_SOURCE_DIR)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              ${ARG_SOURCE_DIR}
              ${ARG_DEST_DIR}
      COMMENT "${ARG_COMMENT}"
    )
  elseif(ARG_FILES)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory ${ARG_DEST_DIR}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${ARG_FILES}
              ${ARG_DEST_DIR}
      COMMENT "${ARG_COMMENT}"
    )
  else()
    message(FATAL_ERROR
            "wamr_unit_test_copy_wasm_files(${target_name}) requires SOURCE_DIR or FILES")
  endif()
endfunction()

# Compile one WAT/WAST fixture into a WASM file in the build tree.
function(wamr_unit_test_compile_wat_to_wasm)
  cmake_parse_arguments(ARG "" "TARGET;SOURCE;OUTPUT;COMMENT" "FLAGS" ${ARGN})
  if(NOT ARG_TARGET OR NOT ARG_SOURCE OR NOT ARG_OUTPUT)
    message(FATAL_ERROR
            "wamr_unit_test_compile_wat_to_wasm requires TARGET, SOURCE, and OUTPUT")
  endif()

  list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
  find_package(WABT REQUIRED COMPONENTS wat2wasm)

  if(ARG_FLAGS)
    set(_wat2wasm_flags ${ARG_FLAGS})
  else()
    set(_wat2wasm_flags --enable-all)
  endif()
  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "Compiling ${ARG_SOURCE} to ${ARG_OUTPUT}")
  endif()

  get_filename_component(_wasm_output_dir "${ARG_OUTPUT}" DIRECTORY)
  string(MD5 _fixture_target_hash "${ARG_OUTPUT}")
  set(_fixture_target "wamr_unit_test_wat2wasm_${_fixture_target_hash}")

  if(NOT TARGET ${_fixture_target})
    add_custom_command(
      OUTPUT ${ARG_OUTPUT}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_wasm_output_dir}
      COMMAND WABT::wat2wasm ${_wat2wasm_flags} ${ARG_SOURCE} -o ${ARG_OUTPUT}
      DEPENDS ${ARG_SOURCE}
      COMMENT "${ARG_COMMENT}"
      VERBATIM
    )
    add_custom_target(${_fixture_target} DEPENDS ${ARG_OUTPUT})
  endif()
  add_dependencies(${ARG_TARGET} ${_fixture_target})
endfunction()

# Build a C-based wasm fixture subproject with wasi-sdk.
function(wamr_unit_test_compile_c_to_wasm)
  cmake_parse_arguments(ARG "" "TARGET;SOURCE_DIR;DEST_DIR;TOOLCHAIN;COMMENT" "CMAKE_ARGS;OUTPUTS" ${ARGN})
  if(NOT ARG_TARGET OR NOT ARG_SOURCE_DIR OR NOT ARG_DEST_DIR)
    message(FATAL_ERROR
            "wamr_unit_test_compile_c_to_wasm requires TARGET, SOURCE_DIR, and DEST_DIR")
  endif()

  list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
  find_package(WASISDK REQUIRED)
  include(ExternalProject)

  if(ARG_TOOLCHAIN STREQUAL "pthread")
    set(_wasi_toolchain "${WASISDK_PTHREAD_TOOLCHAIN}")
  else()
    set(_wasi_toolchain "${WASISDK_TOOLCHAIN}")
  endif()
  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "Building C wasm fixtures for ${ARG_TARGET}")
  endif()

  string(MD5 _fixture_target_hash "${ARG_TARGET}:${ARG_SOURCE_DIR}:${ARG_DEST_DIR}")
  set(_fixture_target "${ARG_TARGET}_c2wasm_${_fixture_target_hash}")
  set(_fixture_build_dir "${CMAKE_CURRENT_BINARY_DIR}/${_fixture_target}-build")

  ExternalProject_Add(
    ${_fixture_target}
    SOURCE_DIR ${ARG_SOURCE_DIR}
    BUILD_ALWAYS YES
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${ARG_SOURCE_DIR} -B ${_fixture_build_dir}
                      -DWASI_SDK_PREFIX=${WASISDK_HOME}
                      -DCMAKE_TOOLCHAIN_FILE=${_wasi_toolchain}
                      ${ARG_CMAKE_ARGS}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${_fixture_build_dir}
    INSTALL_COMMAND ${CMAKE_COMMAND} --install ${_fixture_build_dir} --prefix ${ARG_DEST_DIR}
  )
  add_dependencies(${ARG_TARGET} ${_fixture_target})
endfunction()

# Compile one WASM fixture into an AOT file with wamrc.
function(wamr_unit_test_compile_wasm_to_aot)
  cmake_parse_arguments(ARG "" "TARGET;INPUT;OUTPUT;COMMENT" "FLAGS" ${ARGN})
  if(NOT ARG_TARGET OR NOT ARG_INPUT OR NOT ARG_OUTPUT)
    message(FATAL_ERROR
            "wamr_unit_test_compile_wasm_to_aot requires TARGET, INPUT, and OUTPUT")
  endif()

  list(APPEND CMAKE_MODULE_PATH "${WAMR_ROOT_DIR}/build-scripts")
  find_package(WAMRC REQUIRED)

  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "Compiling ${ARG_INPUT} to ${ARG_OUTPUT}")
  endif()

  get_filename_component(_aot_output_dir "${ARG_OUTPUT}" DIRECTORY)
  string(MD5 _fixture_target_hash "${ARG_OUTPUT}")
  set(_fixture_target "wamr_unit_test_wasm2aot_${_fixture_target_hash}")

  if(NOT TARGET ${_fixture_target})
    add_custom_command(
      OUTPUT ${ARG_OUTPUT}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_aot_output_dir}
      COMMAND WAMRC::wamrc ${ARG_FLAGS} -o ${ARG_OUTPUT} ${ARG_INPUT}
      DEPENDS ${ARG_INPUT}
      COMMENT "${ARG_COMMENT}"
      VERBATIM
    )
    add_custom_target(${_fixture_target} DEPENDS ${ARG_OUTPUT})
  endif()
  add_dependencies(${ARG_TARGET} ${_fixture_target})
endfunction()

# Add an always-built target that copies a directory of wasm files.
function(wamr_unit_test_add_wasm_copy_target target_name)
  cmake_parse_arguments(ARG "" "SOURCE_DIR;DEST_DIR;COMMENT" "" ${ARGN})
  if(NOT ARG_SOURCE_DIR OR NOT ARG_DEST_DIR)
    message(FATAL_ERROR
            "wamr_unit_test_add_wasm_copy_target(${target_name}) requires SOURCE_DIR and DEST_DIR")
  endif()
  if(NOT ARG_COMMENT)
    set(ARG_COMMENT "Copy wasm files for ${target_name}")
  endif()

  add_custom_target(${target_name} ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${ARG_SOURCE_DIR}
            ${ARG_DEST_DIR}
    COMMENT "${ARG_COMMENT}"
  )
endfunction()

if(WAMR_UNIT_COMMON_HELPERS_ONLY)
  return()
endif()

enable_language (ASM)

# Usually, test cases should identify their unique
# complation flags to implement their test plan

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../..)

# include the build config template file
include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

include_directories (${SHARED_DIR}/include
                    ${IWASM_DIR}/include)

include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)

# Add helper classes
include_directories(${CMAKE_CURRENT_LIST_DIR}/common)

# The LLVM environment is only needed in the modes that compile the AOT
# compiler or LLVM JIT into the test executables: aot, llvm-jit and
# multi-tier-jit (in the llvm-jit modes runtime_lib.cmake pulls the
# compiler sources into every suite; in aot mode the compiler-including
# suites pull them directly).  config_common.cmake only sets it up for
# JIT, so cover aot here as well with the shared setup; classic-interp /
# fast-interp / fast-jit skip it entirely.  It must be included in each
# suite subdirectory because it sets directory-scoped variables and
# properties.
if (WAMR_BUILD_JIT EQUAL 1 OR WAMR_BUILD_AOT EQUAL 1)
  include (${WAMR_ROOT_DIR}/build-scripts/llvm_env.cmake)
endif ()

message(STATUS "unit_common.cmake included")

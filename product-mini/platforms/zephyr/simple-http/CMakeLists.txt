# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

if(DEFINED ZEPHYR_BASE)
  set(ZEPHYR_VERSION_FILE "${ZEPHYR_BASE}/VERSION")
elseif(DEFINED ENV{ZEPHYR_BASE})
  set(ZEPHYR_VERSION_FILE "$ENV{ZEPHYR_BASE}/VERSION")
else()
  message(FATAL_ERROR "ZEPHYR_BASE is required to select the POSIX configuration")
endif()

if(NOT EXISTS "${ZEPHYR_VERSION_FILE}")
  message(FATAL_ERROR "Zephyr version file not found: ${ZEPHYR_VERSION_FILE}")
endif()

file(STRINGS "${ZEPHYR_VERSION_FILE}" ZEPHYR_VERSION_LINES
  REGEX "^VERSION_(MAJOR|MINOR) = [0-9]+$")
foreach(VERSION_LINE ${ZEPHYR_VERSION_LINES})
  if(VERSION_LINE MATCHES "^VERSION_MAJOR = ([0-9]+)$")
    set(ZEPHYR_VERSION_MAJOR "${CMAKE_MATCH_1}")
  elseif(VERSION_LINE MATCHES "^VERSION_MINOR = ([0-9]+)$")
    set(ZEPHYR_VERSION_MINOR "${CMAKE_MATCH_1}")
  endif()
endforeach()

if(NOT DEFINED ZEPHYR_VERSION_MAJOR OR NOT DEFINED ZEPHYR_VERSION_MINOR)
  message(FATAL_ERROR "Invalid Zephyr version file: ${ZEPHYR_VERSION_FILE}")
endif()

if(ZEPHYR_VERSION_MAJOR GREATER 4
   OR (ZEPHYR_VERSION_MAJOR EQUAL 4
       AND ZEPHYR_VERSION_MINOR GREATER_EQUAL 3))
  list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_SOURCE_DIR}/zephyr_4.3_plus.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wamr_simple_http)

# The runtime is built by the wasm-micro-runtime Zephyr module and configured
# through the CONFIG_WAMR_* options in prj.conf. The build target is derived
# from the board architecture, override it with -DWAMR_BUILD_TARGET=<target>.
target_sources(app PRIVATE src/main.c)

################  WebAssembly module  ################
# wasm-app/ is compiled with the wasi-sdk toolchain in a nested build, and the
# resulting http_get.wasm is turned into the byte array src/main.c embeds.
# The runtime is located through the Zephyr module system, never through a
# relative path reaching outside this directory.
set(WAMR_ROOT_DIR ${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR})
list(APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package(WASISDK REQUIRED)

include(ExternalProject)
ExternalProject_Add(wasm_app
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/wasm-app
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/wasm-app -B build
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
                      -DWAMR_ROOT_DIR=${WAMR_ROOT_DIR}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-app
  BUILD_ALWAYS      TRUE
)

set(WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/http_get.h)
add_custom_command(
  OUTPUT  ${WASM_HEADER}
  COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-app/http_get.wasm
            -DOUTPUT=${WASM_HEADER}
            -DSYMBOL=wasm_test_file
            -DALIGNED=4
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/bin_to_header.cmake
  DEPENDS wasm_app
  COMMENT "Generating http_get.h from http_get.wasm"
  VERBATIM
)
add_custom_target(gen_http_get_h DEPENDS ${WASM_HEADER})

target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies(app gen_http_get_h)

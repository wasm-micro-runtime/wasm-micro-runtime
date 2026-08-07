# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

set (WAMR_BUILD_PLATFORM "zephyr")

# Build as ARC on ARC boards; retain X86_32 as the default for the existing
# user-mode configuration.
if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CONFIG_ARC)
    set (WAMR_BUILD_TARGET "ARC")
  else ()
    set (WAMR_BUILD_TARGET "X86_32")
  endif ()
endif ()

if (NOT DEFINED WAMR_BUILD_INTERP)
  # Enable Interpreter by default
  set (WAMR_BUILD_INTERP 1)
endif ()

if (NOT DEFINED WAMR_BUILD_AOT)
  # Enable AOT by default.
  set (WAMR_BUILD_AOT 1)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_BUILTIN)
  # Enable libc builtin support by default
  set (WAMR_BUILD_LIBC_BUILTIN 1)
endif ()

if (NOT DEFINED WAMR_BUILD_LIBC_WASI)
  # Disable libc wasi support by default, in the future, 
  # it can be enabled if libc wasi file/socket/lock support is ready on Zephyr platform
  set (WAMR_BUILD_LIBC_WASI 0)
endif ()

if (WAMR_BUILD_TARGET STREQUAL "RISCV64_LP64" OR WAMR_BUILD_TARGET STREQUAL "RISCV32_ILP32")
  set (WAMR_BUILD_FAST_INTERP 1)
endif ()

# Limited to global heap usage in Zephyr user mode
set (WAMR_BUILD_GLOBAL_HEAP_POOL 1)

# Override the global heap size for small devices
if (NOT DEFINED WAMR_BUILD_GLOBAL_HEAP_SIZE)
  set (WAMR_BUILD_GLOBAL_HEAP_SIZE 40960) # 40 KB
endif ()

set (WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# Embed WAMR as a Zephyr library
zephyr_library_named (wamr_lib)

# Add source files to the library
zephyr_library_sources (
  ${WAMR_RUNTIME_LIB_SOURCE} 
  wamr_lib.c
)

# Suppress -Wtype-limits warnings originating from Zephyr's own headers
# (e.g. net_if.h:861 "comparison is always true due to limited range of data type").
# These are pulled in transitively via platform_internal.h → net_pkt.h → net_if.h
# and cannot be fixed in WAMR source code.
zephyr_library_compile_options(-Wno-type-limits)

# Ensure generated headers (e.g. heap_constants.h) are ready before compiling
# the library. Zephyr adds this dependency for its own libraries automatically,
# but parallel builds can race when the library is added via add_subdirectory.
if(TARGET zephyr_generated_headers)
  add_dependencies(wamr_lib zephyr_generated_headers)
endif()

# Specify the memory partition where all globals(including the WAMR global heap buffer)
# in the library should be placed. This partition will be defined in the app source code
# and added to the use-mode thread that uses the WAMR library.
# When WAMR_USE_PREBUILT_LIB is set, the parent CMakeLists.txt handles partition
# registration via set_property() instead, demonstrating the pre-built library approach.
if (NOT WAMR_USE_PREBUILT_LIB)
  zephyr_library_app_memory (wamr_partition)
endif ()

################  WebAssembly module  ################
# ../wasm-app/ is compiled with the wasi-sdk toolchain in a nested build, and
# the resulting test_wasm.wasm is turned into the byte array wamr_lib.c embeds.
# The array is a global of this library, so it lands in wamr_partition like the
# rest of the runtime state.
list (APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package (WASISDK REQUIRED)

include (ExternalProject)
ExternalProject_Add (wasm_app
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-app
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-app -B build
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-app
  BUILD_ALWAYS      TRUE
)

set (WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/test_wasm.h)
add_custom_command (
  OUTPUT  ${WASM_HEADER}
  COMMAND ${CMAKE_COMMAND}
            -DINPUT=${CMAKE_CURRENT_BINARY_DIR}/wasm-app/test_wasm.wasm
            -DOUTPUT=${WASM_HEADER}
            -DSYMBOL=wasm_test_file
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bin_to_header.cmake
  DEPENDS wasm_app
  COMMENT "Generating test_wasm.h from test_wasm.wasm"
  VERBATIM
)
add_custom_target (gen_test_wasm_h DEPENDS ${WASM_HEADER})

target_include_directories (wamr_lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies (wamr_lib gen_test_wasm_h)

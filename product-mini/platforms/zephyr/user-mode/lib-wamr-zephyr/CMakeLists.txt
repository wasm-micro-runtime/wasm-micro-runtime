# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)

set (WAMR_BUILD_PLATFORM "zephyr")

if (NOT DEFINED WAMR_ZEPHYR_APP_PARTITION)
  set (WAMR_ZEPHYR_APP_PARTITION wamr_partition)
endif ()

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

# The runtime is located through the Zephyr module system, never through a
# relative path reaching outside this directory.
set (WAMR_ROOT_DIR ${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR})

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

# Preserve the WAMR configuration as usage requirements for applications that
# link this library from an out-of-tree Zephyr build.
get_directory_property (WAMR_RUNTIME_INCLUDE_DIRS INCLUDE_DIRECTORIES)
get_directory_property (WAMR_RUNTIME_COMPILE_DEFINITIONS COMPILE_DEFINITIONS)

# Embed WAMR as a Zephyr library
zephyr_library_named (wamr_lib)

# This library is added after Zephyr configures its link list. Express the
# kernel dependency so its archive is scanned after WAMR's syscall references,
# including supervisor-side condition-variable initialization.
target_link_libraries (wamr_lib PRIVATE kernel)

target_include_directories (wamr_lib PUBLIC ${WAMR_RUNTIME_INCLUDE_DIRS})
target_compile_definitions (wamr_lib PUBLIC ${WAMR_RUNTIME_COMPILE_DEFINITIONS})

# Add source files to the library
if (NOT DEFINED WAMR_ZEPHYR_RUNTIME_SOURCE)
  set (WAMR_ZEPHYR_RUNTIME_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wamr_lib.c)
endif ()
zephyr_library_sources (
  ${WAMR_RUNTIME_LIB_SOURCE} 
  ${WAMR_ZEPHYR_RUNTIME_SOURCE}
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
  zephyr_library_app_memory (${WAMR_ZEPHYR_APP_PARTITION})
endif ()

################  WebAssembly module  ################
# Compile the selected C guest with wasi-sdk in a nested build and embed its
# test_wasm.wasm in the selected runtime source.
# The array is a global of this library, so it lands in wamr_partition like the
# rest of the runtime state.
list (APPEND CMAKE_MODULE_PATH ${WAMR_ROOT_DIR}/build-scripts)
find_package (WASISDK REQUIRED)

include (ExternalProject)
set (WASM_BINARY ${CMAKE_CURRENT_BINARY_DIR}/wasm-app/test_wasm.wasm)
if (NOT DEFINED WAMR_ZEPHYR_WASM_APP_SOURCE_DIR)
  set (WAMR_ZEPHYR_WASM_APP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../wasm-app)
endif ()
ExternalProject_Add (wasm_app
  SOURCE_DIR        ${WAMR_ZEPHYR_WASM_APP_SOURCE_DIR}
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -S ${WAMR_ZEPHYR_WASM_APP_SOURCE_DIR} -B build
                      -DCMAKE_TOOLCHAIN_FILE=${WASISDK_TOOLCHAIN}
                      -DWAMR_ROOT_DIR=${WAMR_ROOT_DIR}
  BUILD_COMMAND     ${CMAKE_COMMAND} --build build
  INSTALL_COMMAND   ${CMAKE_COMMAND} --install build --prefix ${CMAKE_CURRENT_BINARY_DIR}/wasm-app
  BUILD_ALWAYS      TRUE
)

# Declare the installed file at a step after installation. BUILD_BYPRODUCTS
# would assign it to the wrong producer, and INSTALL_BYPRODUCTS requires newer
# CMake than this library's minimum. This step makes the file visible to Ninja
# without touching its timestamp when the nested build/install is unchanged.
ExternalProject_Add_Step (wasm_app installed_wasm
  DEPENDEES install
  BYPRODUCTS ${WASM_BINARY}
)

set (WASM_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/test_wasm.h)
add_custom_command (
  OUTPUT  ${WASM_HEADER}
  COMMAND ${CMAKE_COMMAND}
            -DINPUT=${WASM_BINARY}
            -DOUTPUT=${WASM_HEADER}
            -DSYMBOL=wasm_test_file
            -P ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bin_to_header.cmake
  DEPENDS wasm_app ${WASM_BINARY}
          ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/bin_to_header.cmake
  COMMENT "Generating test_wasm.h from test_wasm.wasm"
  VERBATIM
)
add_custom_target (gen_test_wasm_h DEPENDS ${WASM_HEADER})

target_include_directories (wamr_lib PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_dependencies (wamr_lib gen_test_wasm_h)

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (NOT CONFIG_WAMR)
  return ()
endif ()

message("WAMR: building WebAssembly Micro Runtime for Zephyr")

set (WAMR_ROOT_DIR ${ZEPHYR_WASM_MICRO_RUNTIME_MODULE_DIR})
set (WAMR_BUILD_PLATFORM "zephyr")

# Derive the build target from the board architecture. WAMR_BUILD_TARGET may
# still be set on the CMake command line to select a sub-variant.
if (NOT DEFINED WAMR_BUILD_TARGET)
  if (CONFIG_ARCH_POSIX)
    # native_sim and friends compile to a native host executable
    if (CONFIG_64BIT)
      set (WAMR_BUILD_TARGET "X86_64")
    else ()
      set (WAMR_BUILD_TARGET "X86_32")
    endif ()
  elseif (CONFIG_ARM64)
    set (WAMR_BUILD_TARGET "AARCH64")
  elseif (CONFIG_CPU_CORTEX_M)
    set (WAMR_BUILD_TARGET "THUMB")
  elseif (CONFIG_ARM)
    set (WAMR_BUILD_TARGET "ARM")
  elseif (CONFIG_X86 AND CONFIG_64BIT)
    set (WAMR_BUILD_TARGET "X86_64")
  elseif (CONFIG_X86)
    set (WAMR_BUILD_TARGET "X86_32")
  elseif (CONFIG_RISCV AND CONFIG_64BIT)
    set (WAMR_BUILD_TARGET "RISCV64_LP64")
  elseif (CONFIG_RISCV)
    set (WAMR_BUILD_TARGET "RISCV32_ILP32")
  elseif (CONFIG_ARC)
    set (WAMR_BUILD_TARGET "ARC")
  elseif (CONFIG_XTENSA)
    set (WAMR_BUILD_TARGET "XTENSA")
  elseif (CONFIG_MIPS)
    set (WAMR_BUILD_TARGET "MIPS")
  else ()
    message (FATAL_ERROR
             "WAMR: cannot derive WAMR_BUILD_TARGET from the board "
             "architecture, please pass -DWAMR_BUILD_TARGET=<target>")
  endif ()
endif ()

# Map Kconfig options onto the runtime build options. The runtime expects
# plain 0/1 values at configure time, while Kconfig leaves disabled booleans
# undefined.
macro (wamr_option_from_kconfig option)
  if (CONFIG_WAMR_${option})
    set (WAMR_BUILD_${option} 1)
  else ()
    set (WAMR_BUILD_${option} 0)
  endif ()
endmacro ()

wamr_option_from_kconfig (INTERP)
wamr_option_from_kconfig (FAST_INTERP)
wamr_option_from_kconfig (AOT)
wamr_option_from_kconfig (LIBC_BUILTIN)
wamr_option_from_kconfig (LIBC_WASI)
wamr_option_from_kconfig (LIB_PTHREAD)
wamr_option_from_kconfig (REF_TYPES)
wamr_option_from_kconfig (GLOBAL_HEAP_POOL)
if (CONFIG_WAMR_GLOBAL_HEAP_POOL)
  set (WAMR_BUILD_GLOBAL_HEAP_SIZE ${CONFIG_WAMR_GLOBAL_HEAP_SIZE})
endif ()

include (${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

zephyr_library_named (wamr)
zephyr_library_sources (${WAMR_RUNTIME_LIB_SOURCE})

# The littlefs module exports its headers through an interface library rather
# than globally; the platform layer includes zephyr/fs/littlefs.h.
if (TARGET LITTLEFS)
  zephyr_library_link_libraries (LITTLEFS)
endif ()

# Suppress -Wtype-limits warnings originating from Zephyr's own headers
# (e.g. net_if.h "comparison is always true due to limited range of data type").
# Pulled in transitively via platform_internal.h -> net_pkt.h -> net_if.h.
zephyr_library_compile_options (-Wno-type-limits)

# Ensure generated headers (e.g. heap_constants.h) are ready before compiling
# the library. Zephyr adds this dependency for its own libraries automatically,
# but parallel builds can race when the library is added via add_subdirectory.
if (TARGET zephyr_generated_headers)
  add_dependencies (wamr zephyr_generated_headers)
endif ()

# Export the runtime headers and feature macros to the application. Both are
# set at this directory scope by runtime_lib.cmake / config_common.cmake.
get_property (WAMR_INCLUDE_DIRS DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
get_property (WAMR_COMPILE_DEFS DIRECTORY PROPERTY COMPILE_DEFINITIONS)
zephyr_include_directories (${WAMR_INCLUDE_DIRS})
zephyr_compile_definitions (${WAMR_COMPILE_DEFS})

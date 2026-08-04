# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# global additional warnings.
if (MSVC)
  # warning level 4
  add_compile_options($<$<COMPILE_LANGUAGE:C>:/W4>)
else ()
  # refer to https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
  add_compile_options(
    -Wall -Wextra -Wformat -Wformat-security
     $<$<COMPILE_LANGUAGE:C>:-Wshadow>
  )
  # -pedantic causes warnings like "ISO C forbids initialization between function pointer and ‘void *’" which
  #   is widely used in the codebase.
  #
  # -fpermissive causes warnings like "-fpermissive is valid for C++/ObjC++ but not for C"
  #
  # Reference:
  #   - gcc-4.8 https://gcc.gnu.org/onlinedocs/gcc-4.8.4/gcc/Warning-Options.html
  #   - gcc-11.5 https://gcc.gnu.org/onlinedocs/gcc-11.5.0/gcc/Warning-Options.html
  add_compile_options (
    $<$<COMPILE_LANGUAGE:C>:-Wimplicit-function-declaration>
  )

  # https://gcc.gnu.org/gcc-5/changes.html introduces incompatible-pointer-types
  # https://releases.llvm.org/7.0.0/tools/clang/docs/DiagnosticsReference.html#wincompatible-pointer-types
  #   is the earliest version that supports this option I can found.
  # Assume AppClang versioning is compatible with Clang.
  if ((CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "5.1")
      OR (CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0.0")
      OR (CMAKE_C_COMPILER_ID STREQUAL "AppClang" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0.0"))
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-Wincompatible-pointer-types>)
  endif()

  # options benefit embedded system.
  add_compile_options (
    -Wdouble-promotion
  )

  # waivers
  add_compile_options (
    -Wno-unused
    -Wno-unused-parameter
  )
endif ()

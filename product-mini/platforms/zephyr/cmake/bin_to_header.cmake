# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Convert a binary file into a C header holding it as a byte array.
#
# Usage (script mode):
#   cmake -DINPUT=<file> -DOUTPUT=<file.h> -DSYMBOL=<array name> \
#         [-DALIGNED=<n>] -P bin_to_header.cmake

foreach(_var INPUT OUTPUT SYMBOL)
  if(NOT ${_var})
    message(FATAL_ERROR "bin_to_header.cmake: -D${_var}=<value> is required")
  endif()
endforeach()

if(NOT EXISTS "${INPUT}")
  message(FATAL_ERROR "bin_to_header.cmake: ${INPUT} does not exist")
endif()

file(READ "${INPUT}" _hex HEX)
string(TOUPPER "${_hex}" _hex)

# One "0xNN, " per byte, then a line break every 12 bytes. CMake regular
# expressions have no {n} repetition, so the line pattern is spelled out.
string(REGEX REPLACE "(..)" "0x\\1, " _body "${_hex}")
string(REPEAT "0x[0-9A-F][0-9A-F], " 12 _line)
string(REGEX REPLACE "(${_line})" "\\1\n    " _body "${_body}")
# Drop the padding left at the end of each line and of the array
string(REGEX REPLACE " +\n" "\n" _body "${_body}")
string(REGEX REPLACE "[ \n]+$" "" _body "${_body}")

get_filename_component(_input_name "${INPUT}" NAME)

# Alignment attribute, for the samples whose runtime maps the array directly
if(ALIGNED)
  set(_aligned "__aligned(${ALIGNED}) ")
else()
  set(_aligned "")
endif()

file(WRITE "${OUTPUT}"
  "/*\n"
  " * Copyright (C) 2026 Intel Corporation.  All rights reserved.\n"
  " * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception\n"
  " *\n"
  " * Generated from ${_input_name} at build time. Do not edit.\n"
  " */\n"
  "\n"
  "unsigned char ${_aligned}${SYMBOL}[] = {\n"
  "    ${_body}\n"
  "};\n"
)

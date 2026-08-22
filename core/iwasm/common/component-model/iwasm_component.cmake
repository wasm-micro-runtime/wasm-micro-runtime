# Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (IWASM_COMPONENT_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions (-DWASM_ENABLE_COMPONENT_MODEL=1)

set(WAVE_PARSER_DIR ${WAMR_ROOT_DIR}/core/iwasm/common/wave-parser)
if (EXISTS "${WAVE_PARSER_DIR}/wave_parser.cmake")
    include(${WAVE_PARSER_DIR}/wave_parser.cmake)
    include_directories(${WAVE_PARSER_INCLUDE_DIRS})
endif()

include_directories (${IWASM_COMPONENT_DIR})

file (GLOB source_all ${IWASM_COMPONENT_DIR}/*.c)

set (IWASM_COMPONENT_SOURCE ${source_all} ${WAVE_PARSER_SOURCES})
# Copyright (C) 2026 WAMR Community.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

wamr_unit_test_suite_run_modes(instruction-metering MODES classic-interp fast-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

set (WAMR_BUILD_INSTRUCTION_METERING 1)

include (../unit_common.cmake)

set (unit_test_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/instruction_metering_test.cc
  ${WAMR_RUNTIME_LIB_SOURCE}
)

# Both interpreters check the instruction limit separately in computed goto
# dispatch (1) and switch dispatch (0), so build the suite for both.
foreach (labels_as_values 1 0)
  set (test_target instruction_metering_test_lav${labels_as_values})
  add_executable (${test_target} ${unit_test_sources})
  target_compile_definitions (${test_target} PRIVATE
    WASM_ENABLE_LABELS_AS_VALUES=${labels_as_values})
  target_link_libraries (${test_target} gtest_main)
  gtest_discover_tests (${test_target}
    TEST_PREFIX "labels_as_values_${labels_as_values}.")
endforeach ()

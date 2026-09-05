# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# WAMR features switch
wamr_unit_test_suite_run_modes(wasm-c-api MODES classic-interp)
if(NOT WAMR_UNIT_TEST_SUITE_ENABLED)
  return()
endif()

include (../unit_common.cmake)

add_executable(wasm_c_api_test basic.cc ${WAMR_RUNTIME_LIB_SOURCE})
target_link_libraries(wasm_c_api_test gtest_main)
gtest_discover_tests(wasm_c_api_test)

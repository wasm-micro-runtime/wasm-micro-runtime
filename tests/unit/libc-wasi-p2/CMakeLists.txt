 # Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
 # SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 
cmake_minimum_required(VERSION 3.28)

project (test-libc-wasi-p2)

add_definitions (-DRUN_ON_LINUX)

add_definitions (-Dattr_container_malloc=malloc)
add_definitions (-Dattr_container_free=free)

set (WAMR_BUILD_LIBC_WASI 1)
set (WAMR_BUILD_COMPONENT_MODEL 1)
set (WAMR_BUILD_APP_FRAMEWORK 1)
set (WAMR_BUILD_AOT 0)
set (WAMR_BUILD_INTERP 1)

include (../unit_common.cmake)
include (${WAMR_ROOT_DIR}/core/iwasm/libraries/libc-wasi-p2/libc_wasi_p2.cmake)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

file (GLOB_RECURSE source_all ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

set (UNIT_SOURCE ${source_all})

aux_source_directory(. SRC_LIST)

set (unit_test_sources
     ${UNIT_SOURCE}
     ../component-instantiation/helpers.cc
     ../../../product-mini/platforms/common/libc_wasi.c
     ${PLATFORM_SHARED_SOURCE}
     ${UTILS_SHARED_SOURCE}
     ${MEM_ALLOC_SHARED_SOURCE}
     ${NATIVE_INTERFACE_SOURCE}
     ${LIBC_BUILTIN_SOURCE}
     ${LIBC_WASI_SOURCE}
     ${LIBC_WASI_P2_SOURCE}
     ${IWASM_COMMON_SOURCE}
     ${IWASM_COMPONENT_SOURCE}
     ${IWASM_INTERP_SOURCE}
     ${UNCOMMON_SHARED_SOURCE}
    )

# Now simply link against gtest or gtest_main as needed. Eg
add_executable (libc-wasi-p2 ${unit_test_sources})

target_include_directories(libc-wasi-p2 PRIVATE ${LIBC_WASI_P2_INCLUDE_DIRS})

find_package(Threads REQUIRED)
target_link_libraries (libc-wasi-p2 ${LLVM_AVAILABLE_LIBS} ${WAMR_TEST_OPENSSL_LIBS} gtest_main Threads::Threads)

target_compile_definitions(libc-wasi-p2 PRIVATE WAMR_BUILD_COMPONENT_MODEL=1)

gtest_discover_tests(libc-wasi-p2)
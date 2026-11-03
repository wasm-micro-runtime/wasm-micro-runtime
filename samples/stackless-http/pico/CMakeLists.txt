# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD pico2_w CACHE STRING "Board")
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(stackless_http_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

if(NOT WAMR_ROOT OR NOT HANDLER_INCLUDE)
    message(FATAL_ERROR "Set WAMR_ROOT and HANDLER_INCLUDE (generated handler.h directory)")
endif()
include(../embedded-wamr.cmake)

add_executable(stackless_http ${WAMR_RUNTIME_LIB_SOURCE} ../p3.c ../vm.c ../task.c ../http.c pico_server.c)
target_include_directories(stackless_http PRIVATE . .. ${HANDLER_INCLUDE}
    ${WAMR_ROOT}/core/iwasm/include)
target_compile_definitions(stackless_http PRIVATE P3_WAMR PICO_STACK_SIZE=4096 PICO_HEAP_SIZE=16384)
target_link_libraries(stackless_http PRIVATE pico_stdlib pico_cyw43_arch_lwip_threadsafe_background)
set_property(TARGET stackless_http PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
# GCC's LTO pass cannot see the SDK's ld --wrap references. Keep the wrappers
# referenced by the firmware visible to the link plugin.
foreach(symbol printf vprintf puts putchar getchar snprintf vsnprintf ceil floor trunc)
    target_link_options(stackless_http PRIVATE "LINKER:-u,__wrap_${symbol}")
endforeach()
pico_enable_stdio_usb(stackless_http 1)
pico_enable_stdio_uart(stackless_http 1)
pico_add_extra_outputs(stackless_http)

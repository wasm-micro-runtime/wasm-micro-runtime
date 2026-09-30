# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.14)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(wamr_user_mode LANGUAGES C)

# The runtime is built by the wasm-micro-runtime Zephyr module and configured
# through the CONFIG_WAMR_* options in prj.conf. CONFIG_WAMR_USERSPACE_PARTITION
# places the globals of the runtime into wamr_partition, the globals of the
# files below are placed there by K_APP_BMEM()/K_APP_DMEM().
target_sources(app PRIVATE src/main.c src/wamr_lib.c)

# Option: register the partition by hand, the way it has to be done for a WAMR
# library that was built externally (e.g. copied from another build). The
# library is still built by the module, but CONFIG_WAMR_USERSPACE_PARTITION is
# left empty so that the module does not register it, and the partition is
# registered here via set_property() instead.
#
# Usage: west build ... -- -DWAMR_USE_PREBUILT_LIB=1 \
#                          -DCONFIG_WAMR_USERSPACE_PARTITION=\"\"
option(WAMR_USE_PREBUILT_LIB
       "Use pre-built library approach for app_smem partition registration" OFF)

if(WAMR_USE_PREBUILT_LIB)
  if(NOT "${CONFIG_WAMR_USERSPACE_PARTITION}" STREQUAL "")
    message(FATAL_ERROR "WAMR_USE_PREBUILT_LIB requires an empty "
            "CONFIG_WAMR_USERSPACE_PARTITION, the module would register the "
            "partition a second time")
  endif()

  # Manually replicate what zephyr_library_app_memory(wamr_partition) does:
  # tell gen_app_partitions.py to place this library's globals into wamr_partition.
  set_property(TARGET zephyr_property_target
               APPEND PROPERTY COMPILE_OPTIONS
               "-l" "libwamr.a" "wamr_partition")

  message(STATUS "WAMR: using pre-built library approach for partition registration")
endif()

include(../cmake/wasm_app.cmake)

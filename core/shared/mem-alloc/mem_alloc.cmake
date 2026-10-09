# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception


set (MEM_ALLOC_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${MEM_ALLOC_DIR})

# BH_ENABLE_GC_VERIFY is defined by build-scripts/config_common.cmake, from
# WAMR_BUILD_GC_HEAP_VERIFY (WAMR_BUILD_GC_VERIFY is a deprecated alias).

if (NOT DEFINED WAMR_BUILD_GC_CORRUPTION_CHECK)
    # The memory allocator heap corruption check is a debugging aid, so it is
    # off by default like every other feature.
    set (WAMR_BUILD_GC_CORRUPTION_CHECK 0)
endif ()

if (WAMR_BUILD_GC_CORRUPTION_CHECK EQUAL 1)
    add_definitions (-DBH_ENABLE_GC_CORRUPTION_CHECK=1)
else ()
    add_definitions (-DBH_ENABLE_GC_CORRUPTION_CHECK=0)
endif ()

if (DEFINED WAMR_BUILD_GC_HEAP_SIZE_DEFAULT)
    add_definitions ("-DGC_HEAP_SIZE_DEFAULT=${WAMR_BUILD_GC_HEAP_SIZE_DEFAULT}")
endif ()

file (GLOB_RECURSE source_all
      ${MEM_ALLOC_DIR}/ems/*.c
      ${MEM_ALLOC_DIR}/tlsf/*.c
      ${MEM_ALLOC_DIR}/mem_alloc.c)

set (MEM_ALLOC_SHARED_SOURCE ${source_all})


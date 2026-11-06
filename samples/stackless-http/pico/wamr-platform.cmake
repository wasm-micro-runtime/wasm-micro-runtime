# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
include_directories(${CMAKE_CURRENT_LIST_DIR} ${WAMR_ROOT}/core/shared/platform/include)
set(PLATFORM_SHARED_SOURCE ${CMAKE_CURRENT_LIST_DIR}/wamr-platform.c)

# Copyright (C) 2024 Amazon Inc.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# simde is a header only library

# Yes. To solve the compatibility issue with CMAKE (>= 4.0), we need to update
# our `cmake_minimum_required()` to 3.5. However, there are CMakeLists.txt
# from 3rd parties that we should not alter. Therefore, in addition to
# changing the `cmake_minimum_required()`, we should also add a configuration
# here that is compatible with earlier versions.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 FORCE)

set (LIB_SIMDE_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions (-DWASM_ENABLE_SIMDE=1)

include_directories(${LIB_SIMDE_DIR} ${LIB_SIMDE_DIR}/simde)

# Keep FetchContent's explicit source override ahead of an installed copy.
set(wamr_simde_minimum_version 0.8.2)
set(wamr_simde_include_dir "${WAMR_SIMDE_INCLUDE_DIR}")
if (NOT FETCHCONTENT_SOURCE_DIR_SIMDE AND NOT wamr_simde_include_dir)
    find_path(wamr_simde_installed_include_dir NAMES simde/wasm/simd128.h)
    set(wamr_simde_version_header "${wamr_simde_installed_include_dir}/simde/simde-common.h")
    if (EXISTS "${wamr_simde_version_header}")
        file(STRINGS "${wamr_simde_version_header}" wamr_simde_version_defines
            REGEX "^#[ \t]*define[ \t]+SIMDE_VERSION_(MAJOR|MINOR|MICRO)[ \t]+[0-9]+")
        set(wamr_simde_version_parts)
        foreach (part MAJOR MINOR MICRO)
            string(REGEX MATCH "SIMDE_VERSION_${part}[ \t]+([0-9]+)" version_define
                "${wamr_simde_version_defines}")
            if (NOT version_define)
                break()
            endif ()
            list(APPEND wamr_simde_version_parts "${CMAKE_MATCH_1}")
        endforeach ()
        list(LENGTH wamr_simde_version_parts wamr_simde_version_length)
        if (wamr_simde_version_length EQUAL 3)
            list(JOIN wamr_simde_version_parts "." wamr_simde_installed_version)
            if (wamr_simde_installed_version VERSION_GREATER_EQUAL wamr_simde_minimum_version)
                set(wamr_simde_include_dir "${wamr_simde_installed_include_dir}")
            endif ()
        endif ()
    endif ()
endif ()

if (wamr_simde_include_dir AND NOT FETCHCONTENT_SOURCE_DIR_SIMDE)
    message(STATUS "Using local SIMDe headers: ${wamr_simde_include_dir}")
    include_directories(SYSTEM "${wamr_simde_include_dir}")
else ()
    include(FetchContent)

    FetchContent_Declare(
        simde
        GIT_REPOSITORY  https://github.com/simd-everywhere/simde
        GIT_TAG v${wamr_simde_minimum_version}
    )

    message("-- Fetching simde ..")
    FetchContent_MakeAvailable(simde)
    include_directories(SYSTEM "${simde_SOURCE_DIR}")
endif ()

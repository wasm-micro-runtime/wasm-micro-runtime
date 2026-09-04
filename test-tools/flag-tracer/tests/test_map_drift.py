# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""The committed table must stay in sync with the cmake tree.

known_map.py exists so the common path costs zero IO; this test is what keeps
it honest.  When it fails, regenerate:

    python3 test-tools/flag-tracer/cmake_map.py . > /tmp/map.py
"""

import cmake_map
import known_map
from conftest import REPO_ROOT


def _fresh():
    macros, dirs, files, _ = cmake_map.build(REPO_ROOT)
    return macros, dirs, files


def test_macro_table_matches_cmake():
    macros, _, _ = _fresh()
    stale = {m: (tuple(v), known_map.KNOWN.get(m))
             for m, v in macros.items() if tuple(v) != known_map.KNOWN.get(m)}
    assert not stale, "known_map.KNOWN is stale: %s" % stale


def test_no_extra_entries():
    macros, _, _ = _fresh()
    extra = set(known_map.KNOWN) - set(macros)
    assert not extra, "known_map.KNOWN has entries cmake no longer defines: %s" % extra


def test_dir_table_matches_cmake():
    _, dirs, _ = _fresh()
    stale = {d: (tuple(v), known_map.KNOWN_DIRS.get(d))
             for d, v in dirs.items() if tuple(v) != known_map.KNOWN_DIRS.get(d)}
    assert not stale, "known_map.KNOWN_DIRS is stale: %s" % stale


def test_file_table_matches_cmake():
    _, _, files = _fresh()
    stale = {f: (tuple(v), known_map.KNOWN_FILES.get(f))
             for f, v in files.items() if tuple(v) != known_map.KNOWN_FILES.get(f)}
    assert not stale, "known_map.KNOWN_FILES is stale: %s" % stale


def test_conditionally_selected_sources_are_known():
    """wasm_mini_loader.c is picked inside a cmake `if`, not by its directory --
    without this the switch that compiles it at all would be missed."""
    assert known_map.KNOWN_FILES["core/iwasm/interpreter/wasm_mini_loader.c"] == (
        "WAMR_BUILD_INTERP", "WAMR_BUILD_MINI_LOADER")


def test_core_switches_are_present():
    """Guard against a parser regression that silently empties the table."""
    for macro, flag in (("WASM_ENABLE_GC", "WAMR_BUILD_GC"),
                        ("WASM_ENABLE_AOT", "WAMR_BUILD_AOT"),
                        ("WASM_ENABLE_INTERP", "WAMR_BUILD_INTERP"),
                        ("WASM_ENABLE_LIB_PTHREAD", "WAMR_BUILD_LIB_PTHREAD"),
                        ("WASM_ENABLE_MEMORY64", "WAMR_BUILD_MEMORY64")):
        assert flag in known_map.KNOWN[macro]


def test_conjunction_is_preserved():
    """FAST_INTERP needs INTERP too -- the case a name-substitution would miss."""
    assert known_map.KNOWN["WASM_ENABLE_FAST_INTERP"] == (
        "WAMR_BUILD_FAST_INTERP", "WAMR_BUILD_INTERP")

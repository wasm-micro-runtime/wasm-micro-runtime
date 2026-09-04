# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Regression baseline: real commits with hand-checked expectations.

Each entry was verified by reading the guard around the changed lines.  They
are pinned commits, so the expectations stay valid as the tree moves on.

`python3 test-tools/flag-tracer/tests/corpus_sweep.py 50` sweeps the last N
commits and prints what the tracer says, for extending this list.
"""

import subprocess

import pytest
from conftest import REPO_ROOT

import inputs
from tracer import Tracer


def _configs(sha):
    diff, head = inputs.from_commit(REPO_ROOT, sha)
    data = Tracer(REPO_ROOT).run(diff, head).as_dict()
    return {tuple(c["flags"]) for c in data["configs"]}, data


def _have(sha):
    return subprocess.run(["git", "-C", REPO_ROOT, "cat-file", "-e",
                           "%s^{commit}" % sha],
                          capture_output=True).returncode == 0


# A combination carries the file's own prerequisites too: a wasm_loader.c
# change only exists when WAMR_BUILD_INTERP is on.
CASES = [
    # sha, a switch combination that must appear
    ("e830d9f43", ("WAMR_BUILD_BRANCH_HINTS=1",              # branch hint check
                   "WAMR_BUILD_INTERP=1")),
    ("e571797fb", ("WAMR_BUILD_GC=1", "WAMR_BUILD_INTERP=1")),   # loader fix
    ("e571797fb", ("WAMR_BUILD_EXTENDED_CONST_EXPR=1", "WAMR_BUILD_INTERP=1")),
    ("e571797fb", ("WAMR_BUILD_INTERP=1", "WAMR_BUILD_MINI_LOADER=1",
                   "WAMR_BUILD_REF_TYPES=1")),           # mini loader variant
    ("c7be241e1", ("WAMR_BUILD_JIT=1",)),                # aot_llvm.c, feature dir
    ("1f98fb01d", ("WAMR_BUILD_LIBC_WASI=1",)),          # libc-wasi wrapper
]


TARGET_CASES = [
    ("dea03eba2", ["iwasm", "wamrc"]),   # LLVM version guards in compilation/
    ("e830d9f43", ["iwasm"]),            # interpreter loader
]


@pytest.mark.parametrize("sha,expected", CASES)
def test_expected_combination_is_found(sha, expected):
    if not _have(sha):
        pytest.skip("commit %s not in this checkout" % sha)
    combos, _ = _configs(sha)
    assert expected in combos, "got %s" % sorted(combos)


def test_header_only_change_is_unconditional():
    sha = "2b25b2047"  # add stddef.h to lib_export.h
    if not _have(sha):
        pytest.skip("commit not in this checkout")
    combos, data = _configs(sha)
    assert not combos
    assert data["unattributed_files"]


@pytest.mark.parametrize("sha,targets", TARGET_CASES)
def test_targets_are_reported(sha, targets):
    if not _have(sha):
        pytest.skip("commit %s not in this checkout" % sha)
    _, data = _configs(sha)
    assert data["targets"] == targets


def test_edit_to_a_guard_line_maps_to_that_switch():
    """e2cf59cc2 adds `|| WASM_ENABLE_SHARED_HEAP != 0` to a long #if chain."""
    sha = "e2cf59cc2"
    if not _have(sha):
        pytest.skip("commit not in this checkout")
    combos, _ = _configs(sha)
    assert any(c == ("WAMR_BUILD_SHARED_HEAP=1",) for c in combos), \
        "got %s" % sorted(combos)


def test_no_contradictory_settings_in_a_single_config():
    """A config must never ask for a switch to be both on and off."""
    for sha, _ in CASES:
        if not _have(sha):
            continue
        combos, _ = _configs(sha)
        for combo in combos:
            names = [s.partition("=")[0] for s in combo]
            assert len(names) == len(set(names)), \
                "%s produced a contradictory config: %s" % (sha, combo)

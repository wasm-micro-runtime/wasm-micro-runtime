# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""End-to-end: diff text -> switch combinations."""

import json
import subprocess

from conftest import REPO_ROOT, TOOL_DIR

from tracer import Tracer


def trace(diff, head=None):
    return Tracer(REPO_ROOT).run(diff, head).as_dict()


def _diff(path, old_start, new_start, added):
    body = "".join("+%s\n" % a for a in added)
    return ("--- a/%s\n+++ b/%s\n@@ -%d,0 +%d,%d @@\n%s"
            % (path, path, old_start, new_start, len(added), body))


def test_line_under_a_guard_maps_to_its_switch():
    # wasm_loader.c line 1403 sits inside #if WASM_ENABLE_GC != 0
    data = trace(_diff("core/iwasm/interpreter/wasm_loader.c", 1402, 1403,
                       ["    /* touched */"]), "HEAD")
    assert ["WAMR_BUILD_GC=1", "WAMR_BUILD_INTERP=1"] in [
        c["flags"] for c in data["configs"]]
    assert data["configs"][0]["confidence"] == "high"


def test_feature_directory_gives_a_medium_confidence_hit():
    data = trace(_diff("core/iwasm/libraries/lib-pthread/lib_pthread_wrapper.c",
                       1, 2, ["/* touched */"]), "HEAD")
    cfg = data["configs"][0]
    assert cfg["flags"] == ["WAMR_BUILD_LIB_PTHREAD=1"]
    assert cfg["confidence"] == "medium"
    assert cfg["source"] == "feature-dir"


def test_unconditional_file_is_reported_not_dropped():
    data = trace(_diff("core/iwasm/include/lib_export.h", 1, 2, ["/* x */"]),
                 "HEAD")
    assert data["configs"] == []
    assert data["unattributed_files"] == ["core/iwasm/include/lib_export.h"]


def test_non_source_files_are_skipped():
    data = trace(_diff("README.md", 1, 2, ["text"]), "HEAD")
    assert data["skipped_files"] == ["README.md"]
    assert data["configs"] == []


def test_cmake_diff_reads_switches_directly():
    data = trace(_diff("build-scripts/config_common.cmake", 1, 2,
                       ["if (WAMR_BUILD_MEMORY64 EQUAL 1)"]), "HEAD")
    assert ["WAMR_BUILD_MEMORY64=1"] in [c["flags"] for c in data["configs"]]


def test_nested_guards_produce_one_conjunction():
    """wasm_loader.c:1007 is `#if WASM_ENABLE_GC == 0` inside a REF_TYPES||GC
    block -- GC must come out as 0, not 1."""
    data = trace(_diff("core/iwasm/interpreter/wasm_loader.c", 1006, 1007,
                       ["    /* touched */"]), "HEAD")
    combos = [c["flags"] for c in data["configs"]]
    assert ["WAMR_BUILD_GC=0", "WAMR_BUILD_INTERP=1",
            "WAMR_BUILD_REF_TYPES=1"] in combos


def test_settings_for_disable_picks_the_specific_switch():
    """Turning FAST_INTERP off must not also turn its INTERP prerequisite off."""
    from tracer import settings_for
    mapped = ("WAMR_BUILD_FAST_INTERP", "WAMR_BUILD_INTERP")
    assert settings_for("WASM_ENABLE_FAST_INTERP", True, mapped) == [
        "WAMR_BUILD_FAST_INTERP=1", "WAMR_BUILD_INTERP=1"]
    assert settings_for("WASM_ENABLE_FAST_INTERP", False, mapped) == [
        "WAMR_BUILD_FAST_INTERP=0"]


def test_cli_emits_json():
    out = subprocess.run(
        ["python3", TOOL_DIR, "-C", REPO_ROOT, "--commit", "HEAD"],
        capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    data = json.loads(out.stdout)
    assert set(data) >= {"configs", "flags_union", "targets", "input"}


def test_cli_rejects_unknown_ref():
    out = subprocess.run(
        ["python3", TOOL_DIR, "-C", REPO_ROOT, "--commit", "no-such-ref"],
        capture_output=True, text=True)
    assert out.returncode == 2


def test_file_level_prerequisite_is_added_to_every_config():
    """wasm_mini_loader.c is only compiled with MINI_LOADER on, so that switch
    belongs in every combination the file produces."""
    data = trace(_diff("core/iwasm/interpreter/wasm_mini_loader.c", 502, 503,
                       ["    /* touched */"]), "HEAD")
    for cfg in data["configs"]:
        assert "WAMR_BUILD_MINI_LOADER=1" in cfg["flags"]


def test_change_to_the_aot_compiler_targets_wamrc():
    data = trace(_diff("core/iwasm/compilation/aot_llvm.c", 2652, 2653,
                       ["    /* touched */"]), "HEAD")
    assert data["targets"] == ["iwasm", "wamrc"]


def test_change_under_core_targets_iwasm_only():
    data = trace(_diff("core/iwasm/interpreter/wasm_loader.c", 1402, 1403,
                       ["    /* touched */"]), "HEAD")
    assert data["targets"] == ["iwasm"]


def test_editing_a_guard_line_maps_to_that_macro():
    """wasm_native.c:490 is a `|| WASM_ENABLE_...` continuation line; editing
    it means that switch, not the dozen others in the same chain."""
    data = trace(_diff("core/iwasm/common/wasm_native.c", 489, 490,
                       ["    || WASM_ENABLE_SHARED_HEAP != 0"]), "HEAD")
    cfg = data["configs"][0]
    assert cfg["source"] == "macro-edit"
    assert cfg["flags"] == ["WAMR_BUILD_SHARED_HEAP=1"]

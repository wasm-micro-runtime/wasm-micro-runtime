#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Unit-test target selection driven by compile_commands.json.

The selection happens *after configure and before anything is built or run*:
the unit build of the report's running mode is configured with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON, and its compile_commands.json is read as
cmake's *build plan*.

Every entry of that file already carries what the selection needs, so neither a
path heuristic nor a cmake-variable -> macro translation table is involved:

  * the object path -- `<build>/<suite>/CMakeFiles/<target>.dir/...` -- names
    the target and the suite build directory it belongs to; it is the entry's
    `output` when it has one and the command's `-o` argument otherwise (CMake
    itself writes only `directory`, `command` and `file`);
  * `command` records the `-DWASM_ENABLE_XXX[=1]` macros the compiler is
    actually invoked with, i.e. the configuration that target is built in.

A target's macro set is the union over its compile units (one target compiles
many files, one suite may build several targets).  With a feature set F, which
is written in the same (macro) plane (coverage_features.py):

    matched(target) = every macro the target enables is enabled by F

i.e. only the target -> F direction is constrained (E ⊆ F).  F is an upper
bound: a macro it does not mention is 0, so a target that turns on something F
does not declare is left out and the report never contains code compiled with a
configuration F does not declare.  F may declare more than the selected targets
enable -- a target covering a subset of F is admitted, and the gap is reported
by warnings_for() instead of dropping the target.

A **suite** is the unit of build, run and collection: `ctest` and the gcovr
collector both work on a suite build directory, not on a single target.  A
suite therefore joins the report only when *all* of its targets match F -- so a
report can never contain code compiled with a configuration F does not declare.
A suite of which only some targets match is excluded and reported as a warning
instead of being silently half-collected.
"""

import json
import os
import re
from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple

from coverage_features import enabled_macros

# `-DWASM_ENABLE_XXX` (bare) or `-DWASM_ENABLE_XXX=0|1` in a compile command.
_MACRO_RE = re.compile(r"-D(WASM_ENABLE_[A-Z0-9_]+)(?:=([01]))?")

# `<...>/CMakeFiles/<target>.dir/...` in the entry's object path.
_OUTPUT_RE = re.compile(r"CMakeFiles[/\\](.+?)\.dir[/\\]")

# The object file a compile command produces, i.e. its `-o` argument.  The
# trailing whitespace requirement keeps `-ofoo` (and options such as
# `-openmp`) from matching.
_OBJECT_RE = re.compile(r"(?:^|\s)-o\s+(\S+)")

# FetchContent vendors googletest/cmocka into `<build>/_deps`: those targets
# are build dependencies, not unit-test targets.
_DEPS_DIR = "_deps"


class UnitTarget:
    """One build target of the unit build, with the macros it compiles with."""

    __slots__ = ("name", "suite", "macros", "sources")

    def __init__(self, name: str, suite: str, macros: Dict[str, int],
                 sources: Set[str]):
        self.name = name        # cmake target name, e.g. "gc_test"
        self.suite = suite      # suite build dir relative to the build root
        self.macros = macros    # WASM_ENABLE_* -> 0|1 (present macros only)
        self.sources = sources  # absolute source paths of its compile units

    def enabled(self) -> Set[str]:
        """The macros this target enables (=1)."""
        return {name for name, value in self.macros.items() if value}

    def __repr__(self):
        return f"<UnitTarget {self.name} suite={self.suite}>"


def object_path(entry: dict, command: str) -> str:
    """Absolute path of the object file a compile entry produces.

    CMake writes only `directory`, `command` and `file` into
    compile_commands.json (verified with CMake 3.25 for both the Ninja and the
    Unix Makefiles generator), so the object path -- the part that names the
    target and its suite -- is the command's `-o` argument.  An `output` field
    (`ninja -t compdb`, and CMake generators that write one) wins when present.
    A relative path is relative to the entry's `directory`.
    """
    output = entry.get("output", "")
    if not output:
        match = _OBJECT_RE.search(command)
        if not match:
            return ""
        output = match.group(1)
    if not os.path.isabs(output):
        output = os.path.join(entry.get("directory", ""), output)
    return os.path.normpath(output)


def parse_compile_commands(path: str, build_dir: str) -> Dict[str, UnitTarget]:
    """Read a unit build's compile_commands.json and group it by target.

    `build_dir` is the build root of the unit build; a target's suite is
    reported as the build directory it lives in, relative to that root.
    """
    with open(path) as fh:
        entries = json.load(fh)

    root = os.path.abspath(build_dir)
    raw: Dict[str, dict] = {}
    order: List[str] = []
    for entry in entries:
        command = entry.get("command")
        if command is None:
            command = " ".join(entry.get("arguments", []))

        output = object_path(entry, command)
        if not output:
            raise SystemExit(
                f"{path}: a compile_commands.json entry names no object file "
                "(neither an 'output' field nor a '-o' argument), so the target "
                "it belongs to cannot be determined.")
        match = _OUTPUT_RE.search(output)
        if not match:
            continue
        target = match.group(1)

        # The part of the object path before CMakeFiles/ is the directory the
        # target is built in, i.e. its suite.
        prefix = output[:match.start()]
        suite = os.path.relpath(os.path.normpath(prefix), root)

        macros = {}
        for macro in _MACRO_RE.finditer(command):
            name = macro.group(1)
            value = int(macro.group(2)) if macro.group(2) is not None else 1
            # union: a macro is enabled for the target if any unit enables it
            if value == 1 or name not in macros:
                macros[name] = value

        if target not in raw:
            raw[target] = {"suite": suite, "macros": {}, "sources": set()}
            order.append(target)
        item = raw[target]
        for name, value in macros.items():
            # union across the target's compile units: enabled if any unit
            # enables it (a target's units normally agree; this keeps the
            # "the target's macro set" definition honest when they do not)
            if value == 1 or name not in item["macros"]:
                item["macros"][name] = value
        item["sources"].add(entry.get("file", ""))

    return {
        name: UnitTarget(name, raw[name]["suite"], raw[name]["macros"],
                         raw[name]["sources"])
        for name in order
    }


def is_vendored(target: UnitTarget) -> bool:
    """Is the target a FetchContent dependency rather than a unit test?"""
    return target.suite == _DEPS_DIR or target.suite.startswith(
        _DEPS_DIR + "/")


def candidates(targets: Dict[str, UnitTarget]) -> Dict[str, UnitTarget]:
    """The unit-test targets a report may be built from.

    Vendored dependencies are dropped, and so is any target that enables no
    feature macro at all: such a target carries no feature configuration and
    could only ever match the wildcard F.  A target built directly in the build
    root (suite ".") is not part of a suite either, and the unit of build, run
    and collection is the suite.
    """
    return {name: target for name, target in targets.items()
            if target.macros and target.suite != "."
            and not is_vendored(target)}


class Selection:
    """The result of matching a feature set F against a build plan.

    A `None` F is the wildcard: every candidate target matches.

    * `suites`   - suite -> its target names; the suites that join the report
    * `skipped`  - suite -> target names; no target of the suite matches F
    * `partial`  - suite -> (matched, unmatched); excluded, see the class
                   docstring of the module
    * `matched`  - every selected target name
    """

    def __init__(self, f: Optional[Dict[str, int]],
                 targets: Dict[str, UnitTarget]):
        self.f = f
        self.targets = targets
        self.suites: Dict[str, List[str]] = {}
        self.skipped: Dict[str, List[str]] = {}
        self.partial: Dict[str, Tuple[List[str], List[str]]] = {}
        self.matched: List[str] = []

    def build_dirs(self, unit_dir: str) -> List[str]:
        """The build directories to collect the report's unit data from."""
        return [os.path.join(unit_dir, suite) for suite in sorted(self.suites)]

    def facts(self) -> str:
        """Canonical serialization of what the selection picked.

        Used for the report fingerprint: it is derived from the build plan
        (target names and their macro sets), not from the user's spelling of F.
        """
        parts = []
        for suite in sorted(self.suites):
            for name in sorted(self.suites[suite]):
                macros = self.targets[name].macros
                macros = ",".join(f"{key}={macros[key]}"
                                  for key in sorted(macros))
                parts.append(f"{suite}/{name}:{macros}")
        return "|".join(parts)

    def describe(self, warning_list: List[str]) -> str:
        """Human-readable record of the selection, for the report header."""
        lines = []
        if self.f is None:
            lines.append("F = (none: every unit target belongs to the report)")
        else:
            lines.append("F = " + " ".join(
                f"-D{name}={self.f[name]}" for name in sorted(self.f)))
        lines.append("")
        lines.append(f"selected suites ({len(self.suites)}):")
        for suite in sorted(self.suites):
            lines.append(f"  {suite}")
            for name in sorted(self.suites[suite]):
                macros = self.targets[name].macros
                enabled = sorted(
                    n for n, v in macros.items() if v == 1)
                off = sorted(n for n, v in macros.items() if v == 0)
                lines.append(f"    target {name}")
                lines.append(f"      =1: {' '.join(enabled) or '(none)'}")
                lines.append(f"      =0: {' '.join(off) or '(none)'}")
        lines.append(f"skipped suites ({len(self.skipped)}): "
                     "no target matches F")
        for suite in sorted(self.skipped):
            names = ", ".join(sorted(self.skipped[suite]))
            lines.append(f"  {suite}: {names}")
        lines.append(f"excluded suites ({len(self.partial)}): only part of the "
                     "suite matches F")
        for suite in sorted(self.partial):
            hit, missed = self.partial[suite]
            lines.append(f"  {suite}: matched {', '.join(sorted(hit))}; "
                         f"not matched {', '.join(sorted(missed))}")
        lines.append("")
        lines.append(f"warnings ({len(warning_list)}):")
        for warning in warning_list:
            lines.append(f"  - {warning}")
        return "\n".join(lines)


def matches(f: Dict[str, int], target: UnitTarget) -> bool:
    """Does this target's configuration fit inside the feature set F?

    Only the target -> F direction is constrained: the target must not enable
    anything F does not declare (E ⊆ F).  F may enable macros the target leaves
    off -- covering a subset of F admits the target, and the gap is reported by
    warnings_for() rather than excluding it.
    """
    return target.enabled() <= enabled_macros(f)


def select(targets: Dict[str, UnitTarget],
           f: Optional[Dict[str, int]]) -> Selection:
    """Match F against the build plan and group the outcome by suite."""
    cand = candidates(targets)
    if f is None:
        matched = set(cand)
    else:
        matched = {name for name, target in cand.items()
                   if matches(f, target)}

    by_suite: Dict[str, List[str]] = defaultdict(list)
    for name, target in sorted(cand.items()):
        by_suite[target.suite].append(name)

    selection = Selection(f, targets)
    for suite in sorted(by_suite):
        names = sorted(by_suite[suite])
        hit = [name for name in names if name in matched]
        if len(hit) == len(names):
            selection.suites[suite] = names
            selection.matched.extend(names)
        elif hit:
            selection.partial[suite] = (
                hit, [name for name in names if name not in matched])
        else:
            selection.skipped[suite] = names
    return selection


def warnings_for(selection: Selection, known_macros: Set[str]) -> List[str]:
    """Advisory findings about F, reported once after the configure.

    Nothing here aborts the run: a feature set that does not fit the unit
    suites is a curation problem, not a build error.  Each entry is printed and
    recorded in the report header.
    """
    warnings = []
    if selection.f is None:
        return warnings

    cand = candidates(selection.targets)
    enabled_anywhere: Set[str] = set()
    for target in cand.values():
        enabled_anywhere.update(target.enabled())
    enabled_selected: Set[str] = set()
    for names in selection.suites.values():
        for name in names:
            enabled_selected.update(selection.targets[name].enabled())

    for name in sorted(selection.f):
        if selection.f[name] != 1:
            continue
        if name not in known_macros:
            warnings.append(
                f"{name}=1 is not a known feature macro: it is neither a "
                "#ifndef default of core/config.h nor used by any compile unit "
                "of this build (typo?)")
        elif name not in enabled_anywhere:
            warnings.append(
                f"{name}=1 has no unit coverage source: no unit target of this "
                "build enables it, so the unit half of the report cannot cover "
                "that feature (consider curating the feature set)")
        elif name not in enabled_selected:
            warnings.append(
                f"{name}=1 is not enabled by any selected unit target: F "
                "admits targets that cover a subset of it (E ⊆ F), so the unit "
                "half of the report does not exercise that feature")
    for suite in sorted(selection.partial):
        hit, missed = selection.partial[suite]
        warnings.append(
            f"suite '{suite}' excluded: {', '.join(missed)} do not match F "
            f"while {', '.join(hit)} do; a suite is built and run as a whole, "
            "so a partially matching suite is left out entirely")
    if not selection.matched:
        warnings.append("F selects 0 unit targets, so the unit half of this "
                        "report will be empty; " + _closest_hint(selection,
                                                                   cand))
    return warnings


def _closest_hint(selection: Selection,
                  cand: Dict[str, UnitTarget]) -> str:
    """Point at the unit target that is closest to F and say what differs.

    Under the subset rule only the macros a target enables beyond F can keep it
    out, so the hint names those; getting F right is a matter of diffing it
    against a real target's macro set, which makes the first run enough."""
    want = enabled_macros(selection.f)
    best = None
    for name, target in sorted(cand.items()):
        add = sorted(target.enabled() - want)   # the target enables, F does not
        if best is None or len(add) < best[0]:
            best = (len(add), name, target.suite, add)
    if best is None:
        return "the build plan has no unit target at all"
    _, name, suite, add = best
    hint = f"closest target: {name} (suite {suite})"
    if add:
        hint += (f"; it enables what F does not declare: {', '.join(add)} "
                 "(add it to F, or stop the suite from enabling it)")
    return hint


if __name__ == "__main__":
    import sys

    from coverage_features import config_macro_names, parse_feature_flags

    cc = sys.argv[1] if len(sys.argv) > 1 else "compile_commands.json"
    features = sys.argv[2] if len(sys.argv) > 2 else ""
    build = os.path.dirname(os.path.abspath(cc))
    parsed = parse_compile_commands(cc, build)
    f = parse_feature_flags(features) or None
    selection = select(parsed, f)
    known = config_macro_names() | {name for t in parsed.values()
                                    for name in t.macros}
    print(f"targets={len(parsed)} candidates={len(candidates(parsed))}")
    print(selection.describe(warnings_for(selection, known)))

#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""The feature set F of a coverage report, and the unit targets it selects.

F is spelled out as compile macros (`--feature "-DWASM_ENABLE_GC=1"`) and
matched against cmake's build plan (compile_commands.json).  See README.md for
the semantics of F and why the selection lives in the macro plane.
"""

import json
import os
import re
from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple

# The macro prefix F speaks; it is what keeps F in the same plane as the
# compile commands the selection is matched against.
MACRO_PREFIX = "WASM_ENABLE_"

_COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_COVERAGE_DIR)))
_CONFIG_H = os.path.join(_REPO_ROOT, "core", "config.h")
_CONFIG_MACRO_RE = re.compile(r"^#ifndef\s+(WASM_ENABLE_[A-Z0-9_]+)\s*$", re.M)

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

# Macros that spell the *running mode* rather than a feature.  The running mode
# is a report dimension of its own (`--mode`), set for the whole unit build, so
# these can never discriminate between targets; they are dropped from both
# sides of the comparison.
MODE_MACROS = frozenset({
    "WASM_ENABLE_INTERP",
    "WASM_ENABLE_FAST_INTERP",
    "WASM_ENABLE_JIT",
    "WASM_ENABLE_FAST_JIT",
    "WASM_ENABLE_LAZY_JIT",
    "WASM_ENABLE_AOT",
})


def config_macro_names() -> Set[str]:
    """Every `#ifndef WASM_ENABLE_XXX` default declared by core/config.h."""
    try:
        with open(_CONFIG_H) as fh:
            return set(_CONFIG_MACRO_RE.findall(fh.read()))
    except OSError:
        return set()


def parse_feature_flags(features: str) -> Dict[str, int]:
    """Parse '-DWASM_ENABLE_GC=1 ...' into {'WASM_ENABLE_GC': 1, ...}.

    Returns {} for an empty string, i.e. the wildcard F.  Only the syntax and
    the macro prefix are checked here, so a cmake switch fails immediately
    instead of selecting nothing; the macro *names* are validated later against
    the build the report is made of (warnings_for()).
    """
    f = {}
    for token in features.split():
        name, sep, value = token.partition("=")
        if not name.startswith("-D" + MACRO_PREFIX) or not sep \
                or value not in ("0", "1"):
            raise ValueError(
                f"Bad feature switch '{token}'; expected "
                f"-D{MACRO_PREFIX}XXX=0|1, i.e. a compile macro rather than a "
                "cmake variable")
        f[name[2:]] = int(value)
    return f


def feature_flags_for(f: Optional[Dict[str, int]]) -> str:
    """Serialize F back to '-DWASM_ENABLE_XXX=0/1 ...' (sorted), for display.

    The `=0` entries are redundant for the selection and only kept so that the
    display echoes what the user wrote.
    """
    if f is None:
        return "(none)"
    return " ".join(f"-D{name}={f[name]}" for name in sorted(f))


def feature_macros(macros) -> Set[str]:
    """The *feature* macros of a macro set: the running-mode ones dropped."""
    return {name for name in macros if name not in MODE_MACROS}


def enabled_features(f: Dict[str, int]) -> Set[str]:
    """The feature macros F enables; everything else it declares 0."""
    return feature_macros(name for name, value in f.items() if value)


class UnitTarget:
    """One build target of the unit build, with the macros it compiles with."""

    __slots__ = ("name", "suite", "macros")

    def __init__(self, name: str, suite: str, macros: Dict[str, int]):
        self.name = name        # cmake target name, e.g. "gc_test"
        self.suite = suite      # suite build dir relative to the build root
        self.macros = macros    # WASM_ENABLE_* -> 0|1 (present macros only)

    def enabled(self) -> Set[str]:
        """The macros this target enables (=1)."""
        return {name for name, value in self.macros.items() if value}

    def __repr__(self):
        return f"<UnitTarget {self.name} suite={self.suite}>"


def object_path(entry: dict, command: str) -> str:
    """Absolute path of the object file a compile entry produces.

    CMake writes only `directory`, `command` and `file` into
    compile_commands.json, so the object path is normally the command's `-o`
    argument; an `output` field (`ninja -t compdb`) wins when present.
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

    The object path `<build>/<suite>/CMakeFiles/<target>.dir/...` names the
    target and its suite (reported relative to `build_dir`), and `command`
    records the macros the compiler is invoked with.  A target's macro set is
    the union over its compile units: a macro counts as enabled when any unit
    enables it.
    """
    with open(path) as fh:
        entries = json.load(fh)

    root = os.path.abspath(build_dir)
    targets: Dict[str, UnitTarget] = {}
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
        name = match.group(1)

        target = targets.get(name)
        if target is None:
            # The part of the object path before CMakeFiles/ is the directory
            # the target is built in, i.e. its suite.
            suite = os.path.relpath(
                os.path.normpath(output[:match.start()]), root)
            target = UnitTarget(name, suite, {})
            targets[name] = target

        for macro in _MACRO_RE.finditer(command):
            macro_name = macro.group(1)
            value = int(macro.group(2)) if macro.group(2) is not None else 1
            if value == 1 or macro_name not in target.macros:
                target.macros[macro_name] = value

    return targets


def is_vendored(target: UnitTarget) -> bool:
    """Is the target a FetchContent dependency rather than a unit test?"""
    return target.suite == _DEPS_DIR or target.suite.startswith(
        _DEPS_DIR + "/")


def candidates(targets: Dict[str, UnitTarget]) -> Dict[str, UnitTarget]:
    """The unit-test targets a report may be built from.

    Vendored dependencies are dropped, and so is any target that enables no
    feature macro at all (it carries no feature configuration) or is built
    directly in the build root (it belongs to no suite, and the suite is the
    unit of build, run and collection).
    """
    return {name: target for name, target in targets.items()
            if target.macros and target.suite != "."
            and not is_vendored(target)}


class Selection:
    """The result of matching a feature set F against a build plan.

    A `None` F is the wildcard: every candidate target matches.

    * `suites`   - suite -> its target names; the suites that join the report
    * `skipped`  - suite -> target names; no target of the suite matches F
    * `partial`  - suite -> (matched, unmatched); excluded as a whole
    * `matched`  - every selected target name
    """

    def __init__(self, f: Optional[Dict[str, int]],
                 targets: Dict[str, UnitTarget],
                 cand: Dict[str, UnitTarget]):
        self.f = f
        self.targets = targets
        self.candidates = cand
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
        (target names and their macro sets), not from the spelling of F.
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
        if self.f is None:
            lines = ["F = (none: every unit target belongs to the report)"]
        else:
            lines = ["F = " + feature_flags_for(self.f)]

        lines.append("")
        lines.append(f"selected suites ({len(self.suites)}):")
        for suite in sorted(self.suites):
            lines.append(f"  {suite}")
            for name in sorted(self.suites[suite]):
                macros = self.targets[name].macros
                enabled = sorted(n for n, v in macros.items() if v == 1)
                off = sorted(n for n, v in macros.items() if v == 0)
                lines.append(f"    target {name}")
                lines.append(f"      =1: {' '.join(enabled) or '(none)'}")
                lines.append(f"      =0: {' '.join(off) or '(none)'}")

        lines.append(f"skipped suites ({len(self.skipped)}): "
                     "no target matches F")
        for suite in sorted(self.skipped):
            lines.append(f"  {suite}: {', '.join(sorted(self.skipped[suite]))}")

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
    """Does this target's configuration fit inside F?

    E ⊆ F: only the target -> F direction is constrained, so a target covering
    a subset of F is admitted while a target enabling anything F does not
    declare is left out.  Running-mode macros are dropped from both sides.
    """
    return feature_macros(target.enabled()) <= enabled_features(f)


def select(targets: Dict[str, UnitTarget],
           f: Optional[Dict[str, int]]) -> Selection:
    """Match F against the build plan and group the outcome by suite.

    A suite joins the report only when *all* of its targets match: a suite is
    what ctest runs and what the collector collects, so a partially matching
    one is excluded instead of half-collected.
    """
    cand = candidates(targets)
    if f is None:
        matched = set(cand)
    else:
        matched = {name for name, target in cand.items()
                   if matches(f, target)}

    by_suite: Dict[str, List[str]] = defaultdict(list)
    for name, target in sorted(cand.items()):
        by_suite[target.suite].append(name)

    selection = Selection(f, targets, cand)
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
    suites is a curation problem, not a build error.
    """
    warnings = []
    if selection.f is None:
        return warnings

    enabled_anywhere: Set[str] = set()
    for target in selection.candidates.values():
        enabled_anywhere.update(feature_macros(target.enabled()))
    enabled_selected: Set[str] = set()
    for names in selection.suites.values():
        for name in names:
            enabled_selected.update(
                feature_macros(selection.targets[name].enabled()))

    for name in sorted(feature_macros(selection.f)):
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
    ignored = sorted(set(selection.f) & MODE_MACROS)
    if ignored:
        warnings.append(
            "running-mode macro(s) " + ", ".join(ignored) + " in F are ignored "
            "by the selection: the running mode is the report's --mode, so "
            "these are not features F has to declare")
    if not selection.matched:
        warnings.append("F selects 0 unit targets, so the unit half of this "
                        "report will be empty; " + _closest_hint(selection))
    return warnings


def _closest_hint(selection: Selection) -> str:
    """Name the unit target closest to F and what it enables beyond F.

    Under the subset rule that is the only thing that can keep a target out, so
    the hint makes the first run enough to curate F.
    """
    want = enabled_features(selection.f)
    cand = sorted(selection.candidates.items())
    if not cand:
        return "the build plan has no unit target at all"
    # the fewest feature macros a target enables beyond F
    name, target = min(
        cand, key=lambda item: len(feature_macros(item[1].enabled()) - want))
    add = sorted(feature_macros(target.enabled()) - want)
    hint = f"closest target: {name} (suite {target.suite})"
    if add:
        hint += (f"; it enables what F does not declare: {', '.join(add)} "
                 "(add it to F, or stop the suite from enabling it)")
    return hint


if __name__ == "__main__":
    import sys

    cc = sys.argv[1] if len(sys.argv) > 1 else "compile_commands.json"
    features = sys.argv[2] if len(sys.argv) > 2 else ""
    build = os.path.dirname(os.path.abspath(cc))
    parsed = parse_compile_commands(cc, build)
    f = parse_feature_flags(features) or None
    selection = select(parsed, f)
    known = config_macro_names() | {name for t in parsed.values()
                                    for name in t.macros}
    print(f"targets={len(parsed)} candidates={len(selection.candidates)}")
    print(selection.describe(warnings_for(selection, known)))

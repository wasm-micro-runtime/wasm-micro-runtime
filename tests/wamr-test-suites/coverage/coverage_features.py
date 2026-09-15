#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""The feature set F of a coverage report object.

A "report object" is `(running mode × spec options × feature set F)`.  F is
spelled out by the user in the **compile-macro plane** -- the plane the compiler
actually sees, and the plane the coverage numbers are computed in:

    --feature "-DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_GC=1"

F is an **upper bound** for the unit-target selection: a macro it does not
mention is 0, so a unit target that enables anything F does not declare is left
out.  `-DWASM_ENABLE_XXX=0` may be written for emphasis, but it is redundant (0
is already the default and can never admit a target).  F may also declare more
than the selected targets enable -- that is a warning (coverage_targets.py),
not a reason to exclude them.  This is what lets the unit-target selection
(`coverage_targets.py`) be a plain set comparison against
compile_commands.json: no cmake-variable -> macro translation table, no
hand-maintained checklist of every feature, and no "implied feature" closure
(implications are cmake's job and are already resolved in the macros the
compiler is invoked with).

The one exception is an *empty* F, which is a wildcard: every unit target
belongs to the report, each suite keeping the values its own CMakeLists.txt
declares.  That is what a report without `--feature` means.

Spelling is checked in two steps.  `parse_feature_flags()` enforces the syntax
and the `WASM_ENABLE_` prefix while the command line is parsed, so passing cmake
switches (`-DWAMR_BUILD_GC=1`) fails immediately instead of selecting nothing.
`config_macro_names()` then supplies the repository's own list of known macros
(`core/config.h`), which the caller unions with the macros this build actually
used to tell a typo from a feature that merely has no unit coverage source.
"""

import os
import re

# The macro prefix F speaks.  Restricting F to it is what keeps it in the same
# plane as the selection criterion.
MACRO_PREFIX = "WASM_ENABLE_"

_COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(_COVERAGE_DIR)))
_CONFIG_H = os.path.join(_REPO_ROOT, "core", "config.h")
_CONFIG_MACRO_RE = re.compile(r"^#ifndef\s+(WASM_ENABLE_[A-Z0-9_]+)\s*$", re.M)


def config_macro_names() -> set:
    """Every `#ifndef WASM_ENABLE_XXX` default declared by core/config.h.

    This is a repository file, so the list cannot go stale.
    """
    try:
        with open(_CONFIG_H) as fh:
            return set(_CONFIG_MACRO_RE.findall(fh.read()))
    except OSError:
        return set()


def parse_feature_flags(features: str) -> dict:
    """Parse '-DWASM_ENABLE_GC=1 -DWASM_ENABLE_INTERP=1' into
    {'WASM_ENABLE_GC': 1, 'WASM_ENABLE_INTERP': 1}.

    Returns {} for an empty string, i.e. the wildcard F.  Only the syntax and
    the macro prefix are checked here; the macro *names* are validated later,
    against the build the report is made of.
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


def enabled_macros(f: dict) -> set:
    """The macros F enables; everything else it declares 0."""
    return {name for name, value in f.items() if value}


def feature_flags_for(f: dict) -> str:
    """Serialize F back to '-DWASM_ENABLE_XXX=0/1 ...' (sorted), for display."""
    return " ".join(f"-D{name}={f[name]}" for name in sorted(f))


if __name__ == "__main__":
    import sys

    for features in sys.argv[1:] or [""]:
        parsed = parse_feature_flags(features)
        print(f"{features or '(none)'}: "
              f"enabled={sorted(enabled_macros(parsed))}")

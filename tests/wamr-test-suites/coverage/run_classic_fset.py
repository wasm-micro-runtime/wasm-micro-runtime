#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Classic-interp feature-set coverage invocation.

One canned report: classic-interp + the feature set F below + spec and unit.
FEATURE_SET is the single knob for what the unit half of the report covers;
see README.md for what F selects (and what it does not configure).
"""

import argparse
import os

from run_coverage import launch

# The report's feature set F, spelled out the way --feature expects it: the
# *feature* macros the unit targets must have been compiled with.  BULK_MEMORY /
# BULK_MEMORY_OPT / SHRUNK_MEMORY are cmake defaults and therefore have to be
# written out here; LIBC_BUILTIN is the runtime under test.  The running mode
# (INTERP) is not a feature -- it is the report's --mode -- so it is not listed.
FEATURE_SET = " ".join([
    # always on
    "-DWASM_ENABLE_BULK_MEMORY=1",
    "-DWASM_ENABLE_BULK_MEMORY_OPT=1",
    "-DWASM_ENABLE_SHRUNK_MEMORY=1",
    # real feature set
    "-DWASM_ENABLE_LIBC_BUILTIN=1",
    "-DWASM_ENABLE_GLOBAL_HEAP_POOL=1",
    "-DWASM_ENABLE_SHARED_HEAP=1",
    "-DWASM_ENABLE_LOAD_CUSTOM_SECTION=1"
])


def main():
    parser = argparse.ArgumentParser(
        description="Run the classic-interp feature-set coverage report "
                    "(classic-interp + a fixed feature set + spec and unit).",
        epilog="The report lands in <out>/classic-fset_<fingerprint>/ "
               "(index.html, coverage.json, summary.txt, summary.json, "
               "fingerprint.txt, unit-selection.txt); the build dirs and the "
               "per-step logs stay in <out>/_work/classic-fset/.",
    )
    parser.add_argument("--out", default="build/coverage",
                        help="Output root directory for reports.  A relative "
                             "path is resolved against the directory this "
                             "command is invoked from (the resolved location "
                             "is printed at startup).")
    parser.add_argument("--llvm-dir", default="",
                        help="LLVM cmake config dir; leave empty to use "
                             "run_coverage.py's default (the bundled LLVM "
                             "build at core/deps/llvm/build/lib/cmake/llvm).")
    args = parser.parse_args()

    # launch() runs run_coverage.py with cwd=<repository root>, so a relative
    # --out forwarded unchanged would silently be re-based onto the repository
    # root instead of the directory the user typed it in.  Resolve it once.
    arguments = [
        "--report", "classic-fset",
        "--mode", "classic-interp",
        "--feature", FEATURE_SET,
        "--unit",
        "--full-test",
        "--out", os.path.abspath(args.out),
    ]
    if args.llvm_dir:
        arguments += ["--llvm-dir", args.llvm_dir]
    raise SystemExit(launch(arguments))


if __name__ == "__main__":
    main()

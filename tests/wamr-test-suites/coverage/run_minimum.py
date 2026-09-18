#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Minimum unit-test coverage invocation.

One canned report: classic-interp + the bare feature set below + spec and unit.
Only the unit suites that enable no feature of their own join it, which is the
coverage baseline of a runtime built without any feature switch -- the opposite
end of run_full.py.  See README.md for what F selects.
"""

import argparse
import os

from run_coverage import launch

# The bare feature set: what a unit target is compiled with when its suite asks
# for nothing.  BULK_MEMORY / BULK_MEMORY_OPT / SHRUNK_MEMORY are cmake's own
# defaults (build-scripts/config_common.cmake) -- no suite requests them, but
# every compile unit carries them, so F as an upper bound has to declare them;
# LIBC_BUILTIN is the minimal usable runtime.
#
# To re-derive the list after a cmake default changes, run the selection over a
# unit build's plan with an empty F and intersect the targets' `=1` lines:
#   python3 coverage_targets.py <build>/compile_commands.json
# A missing macro is self-diagnosing rather than silent: F would then select 0
# targets and run_coverage.py prints the closest target plus the macros it
# enables that F does not declare -- i.e. what to add here.
MINIMUM_FEATURE_SET = " ".join([
    # cmake defaults, on for every suite
    "-DWASM_ENABLE_BULK_MEMORY=1",
    "-DWASM_ENABLE_BULK_MEMORY_OPT=1",
    "-DWASM_ENABLE_SHRUNK_MEMORY=1",
    # the minimal runtime
    "-DWASM_ENABLE_LIBC_BUILTIN=1",
])


def main():
    parser = argparse.ArgumentParser(
        description="Run the minimum unit-test coverage report "
                    "(classic-interp + the bare feature set + spec and unit).",
        epilog="The report lands in <out>/minimum_<fingerprint>/ (index.html, "
               "coverage.json, summary.txt, summary.json, fingerprint.txt, "
               "unit-selection.txt), which also records the suites the bare F "
               "selected; the build dirs and the per-step logs stay in "
               "<out>/_work/minimum/.  The llm-enhanced-test submodule suites "
               "are left out (FULL_TEST=OFF).  The spec suite still runs in "
               "full: it is configured by test_wamr.sh, not by F.",
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
        "--report", "minimum",
        "--mode", "classic-interp",
        "--feature", MINIMUM_FEATURE_SET,
        "--unit",
        "--out", os.path.abspath(args.out),
    ]
    if args.llvm_dir:
        arguments += ["--llvm-dir", args.llvm_dir]
    raise SystemExit(launch(arguments))


if __name__ == "__main__":
    main()

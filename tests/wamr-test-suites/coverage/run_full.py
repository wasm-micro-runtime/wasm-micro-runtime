#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Full coverage run: every spec variant + every unit suite.

Drives run_coverage.py once per report object and merges all of them:

  * `spec-<variant>` - classic-interp plus one test_wamr.sh switch (-G GC,
    -e exception handling, -N extended const expression, -W memory64,
    -E multi-memory, -p threads); the `default` variant adds no switch.
  * `unit-<mode>`    - `<mode>` with no feature constraint (so every unit
    target of the build belongs to the report and each suite keeps the values
    its own CMakeLists declares) and, by default, FULL_TEST=ON, i.e. all unit
    suites including the llm-enhanced-test submodule ones.  Pass
    --no-full-test to leave FULL_TEST off and run the tests/unit suites only.

Each report is written to <out>/<name>_<fingerprint>/; the union of all of
them is written to <out>/_merged/.  (The spec reports keep a copy of each
variant's gcov files under <out>/_work/<name>/, which is what makes the merge
able to see the spec data of every variant.)

Usage (from anywhere in the repository):
  python3 tests/wamr-test-suites/coverage/run_full.py [--out DIR] [--llvm-dir DIR]
                                                      [--no-full-test]
"""

import argparse
import os
import subprocess
import sys

COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(COVERAGE_DIR)))
RUN_COVERAGE = os.path.join(COVERAGE_DIR, "run_coverage.py")

# spec variant name -> extra test_wamr.sh switches ("-s spec -b" are implicit)
SPEC_VARIANTS = [
    ("default", ""),
    ("gc", "-G"),
    ("exception-handling", "-e"),
    ("extended-const", "-N"),
    ("memory64", "-W"),
    ("multi-memory", "-E"),
    ("threads", "-p"),
]

# running modes whose unit suites are built
UNIT_MODES = ["classic-interp", "aot"]


def run_coverage(arguments):
    cmd = [sys.executable, RUN_COVERAGE] + arguments
    print("\n>>>", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description="Full WAMR coverage run.")
    parser.add_argument("--out", default="build/coverage",
                        help="Output root directory for reports.")
    parser.add_argument("--llvm-dir", default="",
                        help="LLVM cmake config dir passed on to "
                             "run_coverage.py; leave empty to use its default "
                             "(the bundled LLVM build).")
    parser.add_argument("--no-full-test", dest="full_test",
                        action="store_false", default=True,
                        help="Do not pass --full-test to the unit reports: "
                             "build the tests/unit suites only, without the "
                             "llm-enhanced-test submodule ones (FULL_TEST=OFF).")
    args = parser.parse_args()

    common = ["--out", args.out]
    if args.llvm_dir:
        common += ["--llvm-dir", args.llvm_dir]

    unit_flags = ["--unit"] + (["--full-test"] if args.full_test else [])

    reports = []
    for variant, spec_opts in SPEC_VARIANTS:
        report = f"spec-{variant}"
        reports.append(report)
        run_coverage(["--report", report, "--mode", "classic-interp",
                      "--spec", spec_opts] + common)
    for mode in UNIT_MODES:
        report = f"unit-{mode}"
        reports.append(report)
        run_coverage(["--report", report, "--mode", mode] + unit_flags
                     + common)

    merge = []
    for report in reports:
        merge += ["--merge", report]
    run_coverage(merge + common)


if __name__ == "__main__":
    main()

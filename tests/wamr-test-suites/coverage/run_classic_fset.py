#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Classic-interp feature-set coverage invocation.

Runs one canned report object:
  * running mode: classic-interp
  * feature set F: the classic interpreter with the libc-builtin runtime
  * test set: spec + unit (the unit targets whose configuration fits inside F)

F is written in the compile-macro plane, as the compiler sees it, and is an
upper bound: a macro it does not mention is 0.  The report therefore contains
only unit suites that enable nothing F does not declare -- no suite is pulled in
with a feature the report does not declare.  A target may enable a subset of F
(e.g. without the runtime under test); that is admitted, and `run_coverage.py`
warns about the F macros no selected target enables.  This list is the single
knob for what the unit half of the report covers.

Note that F describes the *unit* targets only: the spec layer is configured by
test_wamr.sh itself (the running-mode flags plus the --spec switches) and is not
gated on F.

Usage (from anywhere in the repository):
  python3 tests/wamr-test-suites/coverage/run_classic_fset.py \
      [--out DIR] [--llvm-dir DIR]
"""

import argparse
import os
import subprocess
import sys

COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(COVERAGE_DIR)))
RUN_COVERAGE = os.path.join(COVERAGE_DIR, "run_coverage.py")

# The report's feature set F, spelled out the way --feature expects it: the
# macros the unit targets must have been compiled with.  It follows the values
# the classic unit suites resolve to (INTERP is the mode, BULK_MEMORY /
# BULK_MEMORY_OPT / SHRUNK_MEMORY are cmake defaults and therefore have to be
# written out here), plus LIBC_BUILTIN for the runtime under test.
FEATURE_SET = " ".join([
    "-DWASM_ENABLE_INTERP=1",
    "-DWASM_ENABLE_LIBC_BUILTIN=1",
    "-DWASM_ENABLE_BULK_MEMORY=1",
    "-DWASM_ENABLE_BULK_MEMORY_OPT=1",
    "-DWASM_ENABLE_SHRUNK_MEMORY=1",
])


def main():
    parser = argparse.ArgumentParser(
        description="Run the classic-interp feature-set coverage report.")
    parser.add_argument("--out", default="build/coverage",
                        help="Output root directory for reports.")
    parser.add_argument("--llvm-dir", default="",
                        help="LLVM cmake config dir; leave empty to use "
                             "run_coverage.py's default (the bundled LLVM "
                             "build at core/deps/llvm/build/lib/cmake/llvm).")
    args = parser.parse_args()

    cmd = [
        sys.executable, RUN_COVERAGE,
        "--report", "classic-fset",
        "--mode", "classic-interp",
        "--feature", FEATURE_SET,
        "--unit",
        "--out", args.out,
    ]
    if args.llvm_dir:
        cmd += ["--llvm-dir", args.llvm_dir]

    print("Running:", " ".join(cmd))
    subprocess.run(cmd, cwd=REPO_ROOT, check=True)


if __name__ == "__main__":
    main()

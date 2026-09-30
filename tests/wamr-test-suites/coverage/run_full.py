#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Full coverage run: every spec variant + the unit suites of UNIT_MODES.

Drives run_coverage.py once per report (`spec-<variant>` with one test_wamr.sh
switch, `unit-<mode>` with no feature constraint) and, once every report is
written, merges them into <out>/_merged/ -- the batch's big report.
"""

import argparse
import os

from run_coverage import launch, merge_reports

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


def main():
    parser = argparse.ArgumentParser(
        description="Full WAMR coverage run: every spec variant plus the unit "
                    "suites of the supported modes, merged.",
        epilog="Each report lands in <out>/<name>/ and the merge of all of them "
               "in <out>/_merged/; the unit build dirs and the per-step logs "
               "stay in <out>/_work/<name>/.",
    )
    parser.add_argument("--out", default="build/coverage",
                        help="Output root directory for reports; a relative "
                             "path is resolved against the directory this "
                             "command is invoked from.")
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

    # launch() runs its child with cwd=<repository root>, so a relative --out
    # forwarded unchanged would be re-based onto the repository root; resolve it
    # here against the directory the user invoked this from.
    common = ["--out", os.path.abspath(args.out)]
    if args.llvm_dir:
        common += ["--llvm-dir", args.llvm_dir]

    unit_flags = ["--unit"] + (["--full-test"] if args.full_test else [])

    # A report whose spec or unit run failed is still written and still merged:
    # one broken suite should not cost the whole matrix.
    reports = []
    failed = []
    for variant, spec_opts in SPEC_VARIANTS:
        report = f"spec-{variant}"
        reports.append(report)
        if launch(["--report", report, "--mode", "classic-interp",
                   "--spec", spec_opts] + common):
            failed.append(report)
    for mode in UNIT_MODES:
        report = f"unit-{mode}"
        reports.append(report)
        if launch(["--report", report, "--mode", mode] + unit_flags + common):
            failed.append(report)

    # The batch's big report: the tracefiles of every report just written, in
    # one gcovr run.  Which reports those are is what this script knows; the
    # caller does not spell them out.
    try:
        merge_reports(reports, args.out)
    except SystemExit as exc:
        print(str(exc))
        failed.append("_merged")

    if failed:
        print()
        print("reports with a failed step (see their failures.txt): "
              + ", ".join(failed))
        raise SystemExit(1)


if __name__ == "__main__":
    main()

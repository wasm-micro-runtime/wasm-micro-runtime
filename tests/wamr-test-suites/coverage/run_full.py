#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Full coverage run: every spec variant + the unit suites of UNIT_MODES.

One report, named 'full': run_coverage.py runs once per spec variant and once per
unit mode, each of those parts writing its own report inside the batch's work
dir, and the parts are merged into the batch's report -- <out>/full/, whose
tracefiles are the parts'.  The merge_reports() step is the batch driver's job:
the caller never spells out what to merge (and partial batches are not written).
The first part that fails stops the batch.
"""

import argparse
import os
import shutil

from run_coverage import launch, merge_reports, print_path

# The batch's report name: <out>/full/ plus <out>/_work/full/.
REPORT = "full"

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
                    "suites of the supported modes, as one merged report.",
        epilog="The batch report lands in <out>/full/; everything it was made "
               "from stays in <out>/_work/full/ -- one report per part under "
               "parts/<part>/, each with its own unit build dirs and logs, plus "
               "the merge log in logs/.",
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
    # here against the directory the user invoked this from, and derive the
    # batch's work dir from the resolved path.
    out_root = os.path.abspath(args.out)
    workdir = os.path.join(out_root, "_work", REPORT)
    parts_root = os.path.join(workdir, "parts")

    print(f"full coverage run: {len(SPEC_VARIANTS) + len(UNIT_MODES)} parts "
          f"-> one report")
    print_path("output root", out_root)
    print_path("report dir", os.path.join(out_root, REPORT))
    print_path("work dir", workdir)
    print(flush=True)

    common = ["--out", parts_root]
    if args.llvm_dir:
        common += ["--llvm-dir", args.llvm_dir]

    unit_flags = ["--unit"] + (["--full-test"] if args.full_test else [])

    # The parts, in the order they run: a spec variant is one test_wamr.sh
    # switch set on classic-interp, a unit mode is one --unit report.
    parts = [(f"spec-{variant}",
              ["--mode", "classic-interp", "--spec", spec_opts])
             for variant, spec_opts in SPEC_VARIANTS]
    parts += [(f"unit-{mode}", ["--mode", mode] + unit_flags)
              for mode in UNIT_MODES]

    # Drop the previous run's part reports: they are this batch's work material
    # and are about to be rewritten, so what is under parts/ afterwards is this
    # run's.  A part's _work/<part>/ build dir is kept, like a single report's.
    for report, _extra in parts:
        shutil.rmtree(os.path.join(parts_root, report), ignore_errors=True)

    # A part that fails ends the batch: the merge would then cover only part of
    # the matrix, and a partial "full run" is worse than none.
    for report, extra in parts:
        if launch(["--report", report] + extra + common):
            raise SystemExit(f"report '{report}' failed; stopping the batch")

    # The batch's report: the tracefiles of every part just written, in one
    # gcovr run.  Which parts those are is what this script knows; the caller
    # does not spell them out.  The parts are what this batch is made of, so
    # they stay in its work dir instead of sitting next to the report.
    merge_reports(parts_root, [report for report, _extra in parts],
                  os.path.join(out_root, REPORT),
                  os.path.join(workdir, "logs"))


if __name__ == "__main__":
    main()

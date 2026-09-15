#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Collect code coverage data with gcovr and generate reports.

Replaces the former `collect_coverage.sh` (lcov + genhtml) with a gcovr-based
implementation. gcovr reads the .gcno/.gcda files produced by gcc --coverage
directly, so no lcov intermediate format is involved.

The statistics scope is restricted to the WAMR core sources: only
`core/iwasm` and `core/shared` are counted; `core/deps` and every other
directory are excluded (see the --filter/--exclude options below).

This is the *collector* layer: it only turns a list of build directories into
reports, and is called both by `run_coverage.py` (which decides which
directories belong to a report) and by `test_wamr.sh -C` (standalone and
unit/regression flows). See tests/wamr-test-suites/coverage/README.md.

Usage (mirrors the former script's build-dir oriented interface, but accepts
several build directories in one invocation):

  python3 collect_coverage_gcovr.py --out <report_dir> <build_dir> [...]

Reports generated under <report_dir>:
  * index.html / *.html  - line/branch coverage (--html-details)
  * coverage.json        - machine readable data (for local gap analysis)
  * summary.txt          - line/function/branch summary

Run inside tests/wamr-test-suites (or any directory that is a parent of the
repository so that --root can locate the sources).
"""

import argparse
import os
import subprocess
import sys

# Statistical scope: only core/iwasm and core/shared are counted.
FILTER_PATTERNS = [
    "core/(iwasm|shared)/",
]
EXCLUDE_PATTERNS = [
    "core/deps/",
    "tests/",
    "samples/",
    "product-mini/",
    "wamr-compiler/",
    "test-tools/",
]


def repo_root() -> str:
    """Find the repository root (the directory containing core/).

    Resolved from this script's location (tests/wamr-test-suites/
    spec-test-script/ -> up three levels), not from the cwd, so the script
    works no matter where it is invoked from.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))
    if os.path.isdir(os.path.join(root, "core")):
        return root
    # fall back to walking up from the cwd
    cur = os.path.abspath(os.getcwd())
    while True:
        if os.path.isdir(os.path.join(cur, "core")):
            return cur
        parent = os.path.dirname(cur)
        if parent == cur:
            raise SystemExit("Cannot find repository root (no core/ dir above cwd)")
        cur = parent


def gcovr_available() -> bool:
    """gcovr must be importable by the current interpreter (`python3 -m gcovr`).

    A `pip --user` install can land in a user site that the `gcovr` console
    script's shebang (/usr/bin/python3) cannot resolve, so
    `shutil.which("gcovr")` alone is not enough.  Check the module instead and
    fall back to `python3 -m gcovr` so the user site is found via PYTHONPATH
    (added below when needed).
    """
    try:
        import gcovr  # noqa: F401
        return True
    except ImportError:
        return False


def _gcovr_cmd() -> list:
    """Return the gcovr invocation prefix (python -m gcovr)."""
    return [sys.executable, "-m", "gcovr"]


def _ensure_user_site() -> None:
    """Put the user site-packages on PYTHONPATH when it is not enabled.

    pip --user installs into e.g. ~/.local/lib/pythonX.Y/site-packages; the
    gcovr console script's shebang points at the system python which may not
    enable user site by default.  Exporting PYTHONPATH and prepending it to
    sys.path makes both `import gcovr` and `python3 -m gcovr` work.
    """
    import site
    if not site.ENABLE_USER_SITE:
        return
    user_site = site.getusersitepackages()
    if user_site and os.path.isdir(user_site):
        if user_site not in sys.path:
            sys.path.insert(0, user_site)
        existing = os.environ.get("PYTHONPATH", "")
        if user_site not in existing.split(os.pathsep):
            os.environ["PYTHONPATH"] = (
                user_site + (os.pathsep + existing if existing else "")
            )


def run_gcovr(build_dirs, out_dir, root) -> None:
    os.makedirs(out_dir, exist_ok=True)

    _ensure_user_site()
    cmd = _gcovr_cmd() + ["--root", root]
    for pattern in FILTER_PATTERNS:
        cmd += ["--filter", pattern]
    for pattern in EXCLUDE_PATTERNS:
        cmd += ["--exclude", pattern]

    # Merge mode for functions seen on multiple lines across gcov files:
    # unit tests build several binaries that compile the same core sources,
    # so the same function may be recorded at different line numbers
    # (-O0 header inlining). 'separate' keeps those instances apart instead
    # of failing the strict assertion.
    cmd += ["--merge-mode-functions", "separate"]

    # gcc's gcov occasionally reports negative branch hit counts when .gcda
    # files are merged concurrently (gcc bugzilla #68080); warn and drop the
    # corrupted entries instead of aborting the whole report.
    cmd += ["--gcov-ignore-parse-errors", "negative_hits.warn"]

    # The build directories must be passed as gcovr *search paths*
    # (positional arguments).  Repeating --object-directory does NOT work:
    # it is a single-valued option (the working-directory hint for gcov), so
    # gcovr then falls back to scanning the whole --root tree -- which picks
    # up the .gcno/.gcda of every other report's _work directory and every
    # unrelated build (wamr-compiler, spec dirs of other modes, ...).
    search_paths = [os.path.abspath(d) for d in build_dirs if os.path.isdir(d)]
    if not search_paths:
        raise SystemExit(
            "None of the given build directories exist; nothing to collect."
        )

    # HTML report with line/branch details
    html_cmd = cmd + [
        "--branches",
        "--html-details",
        "--html-title", "WAMR Code Coverage",
        "--output", os.path.join(out_dir, "index.html"),
    ] + search_paths
    print("Running:", " ".join(html_cmd))
    subprocess.run(html_cmd, check=True)

    # Machine readable JSON (for local gap analysis)
    json_cmd = cmd + [
        "--branches",
        "--json",
        "--output", os.path.join(out_dir, "coverage.json"),
    ] + search_paths
    print("Running:", " ".join(json_cmd))
    subprocess.run(json_cmd, check=True)

    # Text summary (line/function/branch percentages)
    txt_cmd = cmd + [
        "--branches",
        "--txt",
        "--output", os.path.join(out_dir, "summary.txt"),
    ] + search_paths
    print("Running:", " ".join(txt_cmd))
    subprocess.run(txt_cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description="Collect WAMR coverage with gcovr")
    parser.add_argument(
        "--out", required=True,
        help="Output directory for the generated reports.",
    )
    parser.add_argument(
        "build_dirs", nargs="+",
        help="Build directories containing .gcno/.gcda files. "
             "Multiple directories are merged into one report.",
    )
    args = parser.parse_args()

    # Make pip --user installs importable before probing for gcovr.
    _ensure_user_site()

    if not gcovr_available():
        raise SystemExit(
            "gcovr is not installed for this interpreter. Install it with "
            "`pip install gcovr==6.0`."
        )

    build_dirs = [os.path.abspath(d) for d in args.build_dirs if os.path.isdir(d)]
    if not build_dirs:
        raise SystemExit("None of the given build directories exist; nothing to collect.")

    root = repo_root()
    print(f"Repository root: {root}")
    print(f"Build directories: {build_dirs}")
    print(f"Output directory: {os.path.abspath(args.out)}")

    # gcovr resolves relative search paths against the current working
    # directory, so run it from the repository root regardless of where this
    # script was invoked from.
    os.chdir(root)

    run_gcovr(build_dirs, args.out, root)

    print(f"Code coverage reports generated under {os.path.abspath(args.out)}")
    print("  * index.html / *.html   - HTML line/branch report")
    print("  * coverage.json         - machine readable data")
    print("  * summary.txt           - line/function/branch summary")


if __name__ == "__main__":
    main()

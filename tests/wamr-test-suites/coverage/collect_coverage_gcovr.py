#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Collect code coverage data with gcovr and generate reports.

The collector layer: it turns a list of build directories into reports and
knows nothing about running modes or feature sets.  Called by run_coverage.py
and by `test_wamr.sh -C`.  Replaces the former lcov/genhtml
`collect_coverage.sh`; gcovr reads the .gcno/.gcda of gcc --coverage directly.

  python3 collect_coverage_gcovr.py --out <report_dir> <build_dir> [...]
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

# The reports produced in one gcovr run: output option -> file name.
REPORTS = [
    ("--html-details", "index.html", "HTML line/branch report"),
    ("--json", "coverage.json", "machine readable data"),
    ("--txt", "summary.txt", "per-file branch summary"),
    # summary.txt is branch-oriented and coverage.json only carries per-line
    # data, so this is the one artifact holding line, function and branch
    # numbers together; run_coverage.py reads it back for its console summary.
    ("--json-summary", "summary.json", "line/function/branch totals"),
]


def repo_root() -> str:
    """Find the repository root (the directory containing core/).

    Resolved from this script's location, not from the cwd, so the script works
    no matter where it is invoked from.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(script_dir, "..", "..", ".."))
    if not os.path.isdir(os.path.join(root, "core")):
        raise SystemExit(f"Cannot find repository root (no core/ dir in {root})")
    return root


def repo_relative(path: str, root: str) -> str:
    """Repository-relative spelling of a path inside the repository.

    The absolute path of a checkout inside the devcontainer (/workspaces/...)
    does not exist on the host checkout and vice versa, while the
    repository-relative spelling means the same on both sides; paths outside
    the repository stay absolute."""
    absolute = os.path.abspath(path)
    rel = os.path.relpath(absolute, root)
    if rel == os.pardir or rel.startswith(os.pardir + os.sep):
        return absolute
    return rel


def ensure_user_site() -> None:
    """Put the user site-packages on PYTHONPATH when it is not enabled.

    pip --user installs into e.g. ~/.local/lib/pythonX.Y/site-packages, which
    the `gcovr` console script's shebang (/usr/bin/python3) may not enable.
    Exporting PYTHONPATH and prepending it to sys.path makes both
    `import gcovr` and `python3 -m gcovr` work.
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


def gcovr_available() -> bool:
    """gcovr must be importable by the current interpreter, since it is run as
    `python3 -m gcovr` (see ensure_user_site)."""
    try:
        import gcovr  # noqa: F401
        return True
    except ImportError:
        return False


def run_gcovr(build_dirs, out_dir, root) -> None:
    """Produce every report of REPORTS in one gcovr invocation.

    The build directories must be passed as gcovr *search paths* (positional
    arguments).  Repeating --object-directory does NOT work: it is a
    single-valued option (the working-directory hint for gcov), so gcovr then
    falls back to scanning the whole --root tree -- which picks up the
    .gcno/.gcda of every other report's _work directory and every unrelated
    build.
    """
    os.makedirs(out_dir, exist_ok=True)

    cmd = [sys.executable, "-m", "gcovr", "--root", root, "--branches"]
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

    for option, name, _description in REPORTS:
        cmd += [option, os.path.join(out_dir, name)]
    cmd += ["--html-title", "WAMR Code Coverage"]
    cmd += build_dirs

    print("Running:", " ".join(cmd))
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Collect WAMR coverage with gcovr (scope: core/iwasm + "
                    "core/shared).")
    parser.add_argument(
        "--out", required=True,
        help="Output directory for the generated reports: "
             + ", ".join(f"{name} ({description})"
                         for _option, name, description in REPORTS) + ".",
    )
    parser.add_argument(
        "build_dirs", nargs="+",
        help="Build directories containing .gcno/.gcda files. "
             "Multiple directories are merged into one report.",
    )
    args = parser.parse_args()

    # Make pip --user installs importable before probing for gcovr.
    ensure_user_site()

    if not gcovr_available():
        raise SystemExit(
            "gcovr is not installed for this interpreter. Install it with "
            "`pip install gcovr==6.0`."
        )

    build_dirs = [os.path.abspath(d) for d in args.build_dirs
                  if os.path.isdir(d)]
    if not build_dirs:
        raise SystemExit(
            "None of the given build directories exist; nothing to collect.")

    root = repo_root()
    print(f"Repository root: {root}")
    print(f"Build directories: {build_dirs}")
    print(f"Output directory: {repo_relative(args.out, root)} "
          f"({os.path.abspath(args.out)})")

    # gcovr resolves relative search paths against the current working
    # directory, so run it from the repository root regardless of where this
    # script was invoked from.
    os.chdir(root)

    run_gcovr(build_dirs, args.out, root)

    print(f"Code coverage reports generated under "
          f"{repo_relative(args.out, root)}")
    for _option, name, description in REPORTS:
        print(f"  * {name:<16} - {description}")


if __name__ == "__main__":
    main()

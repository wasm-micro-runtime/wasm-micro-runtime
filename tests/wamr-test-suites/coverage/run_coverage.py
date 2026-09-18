#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Parameterized code coverage runner for WAMR.

A report is one (running mode × spec options × feature set F) combination: the
spec suite runs through test_wamr.sh and, with --unit, the unit targets whose
configuration fits inside F (coverage_targets.py) are built and run; the gcov
data of both is collected with gcovr (collect_coverage_gcovr.py).

Regression tests are NOT part of this tool.  See README.md.
"""

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

from coverage_targets import (
    config_macro_names,
    feature_flags_for,
    parse_compile_commands,
    parse_feature_flags,
    select,
    warnings_for,
)

COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
TESTS_DIR = os.path.dirname(COVERAGE_DIR)                    # tests/wamr-test-suites
WAMR_DIR = os.path.dirname(os.path.dirname(TESTS_DIR))       # repository root
COLLECTOR = os.path.join(COVERAGE_DIR, "collect_coverage_gcovr.py")
UNIT_DIR = os.path.join(WAMR_DIR, "tests", "unit")
IWASM_PLATFORM_DIR = os.path.join(WAMR_DIR, "product-mini", "platforms")

# test_wamr.sh COMPILE_FLAGS equivalents per running mode.
MODE_BUILD_FLAGS = {
    "classic-interp": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=0"
    ),
    "fast-interp": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1 "
        "-DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=0"
    ),
    "aot": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=1"
    ),
    "jit": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_JIT=1 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_LAZY_JIT=0"
    ),
    # Same configuration as 'jit'; 'llvm-jit' is the unit-test run mode name
    # (unit_common.cmake), the spec layer still calls it 'jit'.  AOT must not be
    # combined with JIT (unit_common.cmake validates that) and the classic
    # interpreter stays on, mirroring the CI unit runs.
    "llvm-jit": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_JIT=1 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_LAZY_JIT=0"
    ),
    "fast-jit": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_FAST_JIT=1"
    ),
    "multi-tier-jit": (
        "-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0 "
        "-DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_JIT=1"
    ),
}

RUNNING_MODES = sorted(MODE_BUILD_FLAGS)

OUTPUT_HELP = """\
output layout (under --out):
  <report>_<fingerprint>/    the report: index.html (+ per-file *.html),
                             coverage.json, summary.txt, summary.json,
                             fingerprint.txt, unit-selection.txt, and
                             failures.txt when a step failed
  _work/<report>/            the build dirs the data was collected from, plus
                             logs/ with every child process' output
                             (spec-<mode>.log, unit-configure-<mode>.log,
                             unit-build.log, ctest-<suite>.log, collect.log)
  _merged/                   the result of --merge

The console carries this tool's own progress lines only: the mode, the feature
set, the spec command, the selected unit suites with their test counts, and the
report's line/function/branch totals.  A failing step echoes the tail of its
log and names the file, but does not stop the run: what did run is still
collected, the failures land in failures.txt and are repeated at the end, and
the exit status is non-zero.

Paths are printed repository-relative (plus the absolute one when the two
differ), the spelling that is valid both inside the devcontainer and on the
host checkout.

examples:
  # classic-interp with a curated feature set + spec + unit
  python3 run_coverage.py --report classic-fset --mode classic-interp \\
      --feature "-DWASM_ENABLE_LIBC_BUILTIN=1 -DWASM_ENABLE_BULK_MEMORY=1" \\
      --unit --out build/coverage

  # GC spec variant: test_wamr.sh -s spec -b -t classic-interp -C -G
  python3 run_coverage.py --report gc --mode classic-interp --spec "-G" \\
      --out build/coverage

  # no feature constraint: every unit suite keeps its own defaults, INCLUDING
  # the llm-enhanced-test submodule suites (FULL_TEST=ON)
  python3 run_coverage.py --report ci --mode classic-interp --unit \\
      --full-test --out build/coverage

  # merge two previously generated reports
  python3 run_coverage.py --merge classic-fset --merge ci --out build/coverage
"""


def repo_relative(path: str) -> str:
    """Spell a path relative to the repository root when it is inside it.

    The console is read both inside the devcontainer (/workspaces/...) and on
    the host checkout, which has a different absolute prefix; the
    repository-relative spelling is the one that means the same on both sides.
    """
    path = os.path.abspath(path)
    rel = os.path.relpath(path, WAMR_DIR)
    if rel == os.pardir or rel.startswith(os.pardir + os.sep):
        return path
    return rel


def print_path(label: str, path: str, indent: str = "  ") -> None:
    """Print `<label>: <repo-relative>`, plus the absolute path when it
    differs, so a reader on either side of the devcontainer boundary can find
    the directory."""
    path = os.path.abspath(path)
    rel = repo_relative(path)
    print(f"{indent}{label:<12}: {rel}")
    if rel != path:
        print(f"{indent}{'':<12}  ({path})")


def format_cmd(cmd) -> str:
    return " ".join(shlex.quote(str(arg)) for arg in cmd)


def echo_log_tail(log_path: str, lines: int = 30) -> None:
    """Echo the tail of a failed child's log to the console."""
    print(f"---- last {lines} lines of {repo_relative(log_path)} ----")
    with open(log_path, errors="replace") as fh:
        tail = fh.readlines()[-lines:]
    sys.stdout.writelines(tail)
    if tail and not tail[-1].endswith("\n"):
        print()
    print("----", flush=True)


def run_logged(cmd, log_path: str, cwd=None, env=None, attempts: int = 1,
               retry_delay: int = 5) -> int:
    """Run a child process with its output redirected to a log file.

    test_wamr.sh, cmake, ctest and gcovr are all chatty; the console is
    reserved for this tool's own progress lines.  A child that fails for good
    has its log tail echoed.  With attempts > 1 the child is retried (the
    network-dependent steps: test_wamr.sh re-clones the spec repo, the unit
    configure downloads googletest); every attempt is appended to the same log.

    Returns the exit status of the last attempt (0 when one succeeded).
    """
    status = 0
    for attempt in range(1, attempts + 1):
        with open(log_path, "a") as fh:
            fh.write(f"\n===== attempt {attempt}/{attempts}: "
                     f"$ {format_cmd(cmd)}\n")
            fh.flush()
            status = subprocess.run(cmd, cwd=cwd, env=env, stdout=fh,
                                    stderr=subprocess.STDOUT).returncode
        if status == 0:
            return 0
        if attempt < attempts:
            print(f"      attempt {attempt}/{attempts} failed (rc={status}); "
                  f"retrying in {retry_delay}s", flush=True)
            time.sleep(retry_delay)
    echo_log_tail(log_path)
    return status


def platform() -> str:
    return subprocess.run(
        ["uname", "-s"], capture_output=True, text=True, check=True
    ).stdout.strip().lower()


def resolve_llvm_dir(llvm_dir: str) -> str:
    """Return llvm_dir as an absolute path (relative ones are resolved
    against the repository root, since cmake subprocesses run with various
    working directories)."""
    if os.path.isabs(llvm_dir):
        return llvm_dir
    return os.path.abspath(os.path.join(WAMR_DIR, llvm_dir))


def spec_build_dir() -> str:
    """Build directory of the spec-layer iwasm product (test_wamr.sh builds
    it in place under product-mini/platforms/<platform>/build)."""
    return os.path.join(IWASM_PLATFORM_DIR, platform(), "build")


def fingerprint(combo, facts: str) -> str:
    """Normalized fingerprint of the report.

    `facts` is the selection record (Selection.facts(), empty without a unit
    build), so the fingerprint is derived from the build plan the report was
    selected from -- not from the user's spelling of F.
    """
    raw = "||".join([combo["mode"], combo["spec"], facts])
    return hashlib.sha1(raw.encode()).hexdigest()[:16]


def snapshot_coverage_data(src, dst):
    """Copy the gcov artifacts of a build tree into the report's work dir.

    test_wamr.sh builds the spec-layer iwasm *in place* and run_spec() wipes
    that directory before the next variant, so a spec variant's .gcda only
    survives if it is snapshotted right after its run.

    Only *.gcno/*.gcda are copied, with their relative layout preserved: gcov
    reads those two files and nothing else.  A real copy -- rather than a hard
    link -- keeps the snapshot immune to a later rebuild rewriting the same
    inode in place.

    Returns the number of files copied."""
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    copied = 0
    for root, _dirs, files in os.walk(src):
        rel = os.path.relpath(root, src)
        target_root = dst if rel == "." else os.path.join(dst, rel)
        for name in files:
            if not name.endswith((".gcno", ".gcda")):
                continue
            os.makedirs(target_root, exist_ok=True)
            shutil.copy2(os.path.join(root, name),
                         os.path.join(target_root, name))
            copied += 1
    return copied


def run_spec(workdir, mode, spec_opts, log_dir):
    """Run the spec test suite via test_wamr.sh and snapshot its gcov data.

    `-s spec` (spec suite) and `-b` (wabt binary release instead of compiling
    wabt) are always passed; spec_opts only carries the extra feature switches.
    The iwasm build uses test_wamr.sh's own fixed feature configuration, in a
    build dir it reuses across modes and runs -- so it is wiped before the run
    (no stale .gcno/.gcda from other configurations) and snapshotted after it.

    The spec repo is re-cloned on github before every run, which intermittently
    fails, hence the retries.

    A failing run is reported but does not stop the report: what ran before the
    failure has already written its .gcda.

    Returns (snapshot to collect from, failure message or None)."""
    script = os.path.join(TESTS_DIR, "test_wamr.sh")
    # test_wamr.sh names the LLVM-JIT mode 'jit'; 'llvm-jit' is the unit name.
    spec_mode = "jit" if mode == "llvm-jit" else mode
    cmd = ["bash", script, "-s", "spec", "-b", "-t", spec_mode, "-C"]
    cmd.extend(shlex.split(spec_opts))
    env = dict(os.environ, COLLECT_CODE_COVERAGE="1")
    build_dir = spec_build_dir()
    if os.path.isdir(build_dir):
        print(f"      wiping stale product build dir "
              f"{repo_relative(build_dir)}")
        shutil.rmtree(build_dir)
    log_path = os.path.join(log_dir, f"spec-{mode}.log")
    print(f"      $ {format_cmd(cmd)}")
    print(f"      log: {repo_relative(log_path)}", flush=True)
    status = run_logged(cmd, log_path, cwd=TESTS_DIR, env=env, attempts=3)
    failure = None
    if status != 0:
        failure = (f"spec run (rc={status}); see "
                   f"{repo_relative(log_path)}")
        print(f"      FAILED (rc={status}); collecting the data it did produce")

    if not os.path.isdir(build_dir):
        print("      WARNING: spec iwasm build dir not found: "
              f"{repo_relative(build_dir)}")
        return None, failure
    snapshot = os.path.join(workdir, f"spec-coverage-{mode}")
    copied = snapshot_coverage_data(build_dir, snapshot)
    if not copied:
        print("      WARNING: no .gcno/.gcda under "
              f"{repo_relative(build_dir)}; was the build configured with "
              "COLLECT_CODE_COVERAGE=1?")
        return None, failure
    if not failure:
        print("      ok: spec suite passed; ", end="")
    else:
        print("      ", end="")
    print(f"snapshotted {copied} gcov files to {repo_relative(snapshot)}")
    return snapshot, failure


def configure_unit(workdir, mode, llvm_dir, log_dir, full_test=False):
    """Configure the unit build of one running mode.

    Returns (build dir, failure message or None); without a build plan the
    report has no unit half, but its spec half still runs.

    Nothing is built here: the configure is what writes compile_commands.json,
    i.e. the build plan the target selection is made from.  That is also why F
    is deliberately NOT injected: every suite declares the feature values its
    own test plan needs via set(WAMR_BUILD_*), and injecting top-level flags
    would turn core code on for suites whose curated source lists do not link
    the matching wrapper (e.g. SHARED_HEAP=1 breaks llm interpreter-core).
    """
    build_dir = os.path.join(workdir, f"unittest-build-{mode}")
    cmake_args = [
        "cmake", "-S", UNIT_DIR, "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCOLLECT_CODE_COVERAGE=1",
    ]
    if shutil.which("ninja"):
        cmake_args.append("-G Ninja")
        if shutil.which("ccache"):
            # Every suite compiles the same core sources; ccache turns the
            # repeated compilations into cache hits.
            cmake_args.append("-DCMAKE_C_COMPILER_LAUNCHER=ccache")
            cmake_args.append("-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
    cmake_args.extend(MODE_BUILD_FLAGS[mode].split())
    if full_test:
        # Build every unit suite, including the llm-enhanced-test submodule
        # suites (they declare their own run-mode support).
        cmake_args.append("-DFULL_TEST=ON")
    if llvm_dir:
        cmake_args.append(f"-DLLVM_DIR={resolve_llvm_dir(llvm_dir)}")

    # Reuse already-downloaded googletest/cmocka sources when available (set
    # FETCHCONTENT_LOCAL_SRC to a build dir that contains _deps/): the unit
    # configure downloads them from github/gitlab with libcurl, which is
    # flaky on some networks (HTTP/2 stream errors).
    # TODO: the lookup below expects FETCHCONTENT_LOCAL_SRC to *be* the _deps
    #       directory (<local>/googletest-src); accept a build dir containing
    #       one, as this comment promises.
    local_deps = os.environ.get("FETCHCONTENT_LOCAL_SRC", "")
    if local_deps and os.path.isdir(local_deps):
        for name in ("googletest", "cmocka"):
            src = os.path.join(local_deps, name + "-src")
            if os.path.isdir(src):
                cmake_args.append(
                    f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={src}")

    # The googletest download is network-dependent and intermittently fails
    # (HTTP/2 framing); retry the configure a few times before giving up.
    log_path = os.path.join(log_dir, f"unit-configure-{mode}.log")
    print(f"      cmake -S {repo_relative(UNIT_DIR)} -B "
          f"{repo_relative(build_dir)} {MODE_BUILD_FLAGS[mode]}")
    print(f"      log: {repo_relative(log_path)}", flush=True)
    status = run_logged(cmake_args, log_path, attempts=3)
    if status != 0:
        print(f"      FAILED (rc={status}); this report gets no unit half")
        return build_dir, (f"unit configure (rc={status}); see "
                           f"{repo_relative(log_path)}")
    return build_dir, None


def select_unit_targets(unit_dir, f):
    """Select the unit targets of a configured build that fit inside F, and
    report the selection on the console.

    Returns (selection, warnings)."""
    targets = parse_compile_commands(
        os.path.join(unit_dir, "compile_commands.json"), unit_dir)
    selection = select(targets, f)
    known = config_macro_names() | {
        macro for target in targets.values() for macro in target.macros}
    warnings = warnings_for(selection, known)
    planned = (len(selection.suites) + len(selection.skipped)
               + len(selection.partial))
    print(f"[unit select] F = {feature_flags_for(f)}")
    print(f"[unit select] {len(selection.suites)} of {planned} suites "
          f"selected ({len(selection.matched)} targets); "
          f"{len(selection.skipped)} skipped, "
          f"{len(selection.partial)} excluded (partial match)")
    for suite in sorted(selection.suites):
        print(f"              {suite:<24} "
              f"{', '.join(sorted(selection.suites[suite]))}")
    if selection.partial:
        print("              excluded: " + ", ".join(sorted(selection.partial)))
    if selection.skipped:
        print("              skipped : " + ", ".join(sorted(selection.skipped)))
    for warning in warnings:
        print(f"              WARNING: {warning}")
    return selection, warnings


def build_and_run_unit(build_dir, selection, log_dir):
    """Build the selected targets and run the selected suites.

    Only the selected targets are built and only the selected suites are run:
    ctest is organized per suite, and a suite is selected only when all of its
    targets match F -- so the build and the run cannot disagree.

    A failing suite is reported and the remaining ones still run; after a failing
    build no suite runs, since its binaries may not exist.

    Returns the list of failure messages."""
    if not selection.matched:
        print("      F selects no target; skipping the unit build and test run")
        return []
    failures = []
    jobs = min(os.cpu_count() or 4, 8)
    build_log = os.path.join(log_dir, "unit-build.log")
    print(f"      cmake --build ({len(selection.matched)} targets, -j {jobs})"
          " ...", end="", flush=True)
    status = run_logged(
        ["cmake", "--build", build_dir, "-j", str(jobs), "--target"]
        + sorted(selection.matched), build_log)
    print("ok" if status == 0 else f"FAILED (rc={status})")
    if status != 0:
        return [f"unit build (rc={status}); see {repo_relative(build_log)}"]
    for suite in sorted(selection.suites):
        log_path = os.path.join(
            log_dir, "ctest-" + suite.replace(os.sep, "-") + ".log")
        status = run_logged(
            ["ctest", "--test-dir", os.path.join(build_dir, suite),
             "--output-on-failure"], log_path)
        print(f"      ctest {suite:<24} {ctest_summary(log_path)}")
        if status != 0:
            failures.append(f"unit suite '{suite}' (rc={status}); see "
                            f"{repo_relative(log_path)}")
    print(f"      ctest logs: {repo_relative(log_dir)}/ctest-*.log")
    return failures


def ctest_summary(log_path: str) -> str:
    """The `<n> tests, <k> failed` line ctest writes at the end of its log."""
    total = failed = None
    with open(log_path, errors="replace") as fh:
        for line in fh:
            match = re.search(r"(\d+)% tests passed, (\d+) tests failed out "
                              r"of (\d+)", line)
            if match:
                failed, total = int(match.group(2)), int(match.group(3))
    if total is None:
        return "no test summary in the log"
    return f"{total} tests, {failed} failed"


def collect(build_dirs, out_dir, log_dir, log_name="collect.log"):
    if not build_dirs:
        print("[collect] nothing to collect: no spec data and no selected unit "
              "suite")
        print("          no report was written to " + repo_relative(out_dir))
        return
    log_path = os.path.join(log_dir, log_name)
    print(f"[collect] gcovr over {len(build_dirs)} build dirs -> "
          f"{repo_relative(out_dir)}")
    cmd = [sys.executable, COLLECTOR, "--out", out_dir] + build_dirs
    status = run_logged(cmd, log_path)
    if status != 0:
        raise SystemExit(f"coverage collection failed (rc={status}); full "
                         f"output in {repo_relative(log_path)}")
    print(f"          log: {repo_relative(log_path)}")


def print_coverage_summary(report_dir, indent="  "):
    """Print the report's line/function/branch totals.

    The collector writes them to `summary.json` (gcovr --json-summary), the one
    artifact that carries all three counters together."""
    path = os.path.join(report_dir, "summary.json")
    if not os.path.isfile(path):
        print(f"{indent}WARNING: no {repo_relative(path)}; no coverage summary "
              "available")
        return
    with open(path) as fh:
        data = json.load(fh)
    print(f"{indent}coverage summary ({repo_relative(path)})")
    for label, key in (("lines", "line"), ("functions", "function"),
                       ("branches", "branch")):
        total = data.get(f"{key}_total", 0)
        covered = data.get(f"{key}_covered", 0)
        percent = data.get(f"{key}_percent")
        percent = " n/a " if percent is None else f"{percent:5.1f}%"
        print(f"{indent}  {label:<10} {percent}  ({covered} / {total})")


def run_report(name, combo, out_root, unit, llvm_dir, full_test=False):
    out_root = os.path.abspath(out_root)
    workdir = os.path.join(out_root, "_work", name)
    log_dir = os.path.join(workdir, "logs")
    os.makedirs(log_dir, exist_ok=True)

    mode = combo["mode"]
    spec_opts = combo["spec"]
    features = combo["features"]
    spec_mode = "jit" if mode == "llvm-jit" else mode

    print()
    print("=" * 78)
    print(f"report '{name}'")
    print(f"  mode        : {mode}")
    print(f"  spec suite  : test_wamr.sh -s spec -b -t {spec_mode} -C "
          f"{spec_opts or '(no extra switch)'}")
    print(f"  feature set : {features or '(none: every unit target)'}")
    print_path("output root", out_root)
    print_path("logs", log_dir)
    print("=" * 78, flush=True)

    # Phase 1: configure the unit build and select its targets.  The selection
    # reads cmake's build plan, so it happens after the configure and before
    # anything is built or run; the spec layer is not touched yet, so the
    # feature-set warnings are printed before it starts.
    failures = []
    selection = None
    unit_dir = None
    selection_report = ""
    if unit:
        print(f"[unit configure] ({mode})")
        unit_dir, failure = configure_unit(workdir, mode, llvm_dir, log_dir,
                                           full_test)
        if failure:
            # No build plan, so no unit half; the spec half still runs.
            failures.append(failure)
        else:
            selection, warnings = select_unit_targets(
                unit_dir, parse_feature_flags(features) or None)
            selection_report = (
                f"mode={mode}\nspec={spec_opts or '(none)'}\n"
                f"features={features or '(none)'}\n\n"
                + selection.describe(warnings) + "\n")

    # The fingerprint describes the build plan the report was selected from, so
    # it can only be taken here -- after the configure, still before any build.
    fp = fingerprint(combo, selection.facts() if selection else "")
    out_dir = os.path.join(out_root, f"{name}_{fp}")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "unit-selection.txt"), "w") as fh:
        fh.write(selection_report)
    print_path("report dir", out_dir)

    # Phase 2: run everything the selection kept.  test_wamr.sh builds its own
    # iwasm and runs the spec suite on it; run_spec() returns the snapshot of
    # its gcov data, which is what we collect from.
    build_dirs = []
    print(f"[spec] ({mode})")
    spec_snapshot, failure = run_spec(workdir, mode, spec_opts, log_dir)
    if spec_snapshot:
        build_dirs.append(spec_snapshot)
    if failure:
        failures.append(failure)

    if selection is not None:
        print(f"[unit run] ({mode})")
        failures.extend(build_and_run_unit(unit_dir, selection, log_dir))
        # collect from the selected suites' build dirs (a suite is the unit
        # ctest and the collector work on)
        build_dirs.extend(selection.build_dirs(unit_dir))

    collect(build_dirs, out_dir, log_dir)

    with open(os.path.join(out_dir, "fingerprint.txt"), "w") as fh:
        fh.write(f"name={name}\n")
        fh.write(f"fingerprint={fp}\n")
        fh.write("combinations:\n")
        fh.write(f"  mode={mode}\n")
        fh.write(f"  features={features or '(none)'}\n")
        fh.write(f"  spec={spec_opts or '(none)'}\n")

    # A failed step does not withhold the report; failures.txt is what says the
    # report is partial.
    failures_path = os.path.join(out_dir, "failures.txt")
    if failures:
        with open(failures_path, "w") as fh:
            fh.write("\n".join(failures) + "\n")
    elif os.path.isfile(failures_path):
        os.remove(failures_path)

    print_coverage_summary(out_dir)
    print(f"report '{name}' written to {repo_relative(out_dir)} "
          f"(fingerprint {fp})")
    print("  index.html, coverage.json, summary.txt, summary.json, "
          "fingerprint.txt, unit-selection.txt")
    if failures:
        print(f"  FAILED steps ({len(failures)}), recorded in "
              f"{repo_relative(failures_path)}:")
        for failure in failures:
            print(f"    - {failure}")
    return failures


def merge_reports(reports, out_dir):
    """Merge several previously generated reports.

    Each report's _work/<name> directory keeps the build dirs its data was
    collected from (the unit build dirs, and for the spec layer a copy of the
    gcov files of test_wamr.sh's in-place product build), so the merge
    re-collects the union of those dirs."""
    merged_out = os.path.join(out_dir, "_merged")
    os.makedirs(merged_out, exist_ok=True)
    build_dirs = []
    for report in reports:
        work = os.path.join(out_dir, "_work", report)
        if os.path.isdir(work):
            for d in os.listdir(work):
                p = os.path.join(work, d)
                # `logs/` holds the runner's child-process logs, not gcov data
                if os.path.isdir(p) and d != "logs":
                    build_dirs.append(p)
    if not build_dirs:
        raise SystemExit(
            "No _work build dirs found for the given reports; run the reports "
            "first so their .gcda data is available for merging."
        )
    log_dir = os.path.join(out_dir, "_work", "merge-logs")
    os.makedirs(log_dir, exist_ok=True)
    collect(build_dirs, merged_out, log_dir, log_name="merge-collect.log")
    with open(os.path.join(merged_out, "fingerprint.txt"), "w") as f:
        f.write(f"merged={','.join(reports)}\n")
        f.write(f"build_dirs={build_dirs}\n")
    print_coverage_summary(merged_out)
    print(f"merged report written to {repo_relative(merged_out)}")


# Options whose value may itself start with a dash.  argparse would read such a
# value as the *next option* ("expected one argument"), so `--spec -G` and
# `--feature -DWASM_ENABLE_GC=1` are rewritten into the `--opt=value` form
# before parsing (see normalize_argv).
DASH_VALUE_OPTIONS = ("--spec", "--feature")


def normalize_argv(argv):
    """Join `--spec VALUE` / `--feature VALUE` into `--spec=VALUE` so that a
    VALUE starting with '-' is not mistaken for an option.  The `--opt=value`
    spelling needs no rewriting."""
    normalized = []
    pending = None
    for arg in argv:
        if pending is not None:
            normalized.append(f"{pending}={arg}")
            pending = None
        elif arg in DASH_VALUE_OPTIONS:
            pending = arg
        else:
            normalized.append(arg)
    if pending is not None:
        normalized.append(pending)
    return normalized


def launch(arguments) -> int:
    """Run this runner as a child process from the repository root.

    The entry scripts (run_full.py, run_classic_fset.py, run_minimum.py) are
    fixed pipelines around it; they resolve --out themselves, because the child
    runs with cwd=<repository root>.

    Returns the child's exit status instead of raising, so a caller driving
    several reports can finish them (and the merge) before failing."""
    cmd = [sys.executable, os.path.abspath(__file__)] + arguments
    print("Running:", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=WAMR_DIR).returncode


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Parameterized WAMR coverage runner.  A report = "
            "(running mode × spec options × feature set)."
        ),
        epilog=OUTPUT_HELP,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--report", action="append", default=[],
        help="Report name; repeatable. Follow with --mode/--spec/--feature.",
    )
    parser.add_argument(
        "--mode", action="append", default=[],
        choices=RUNNING_MODES,
        help="Running mode for the current report; repeatable.  'jit' is an "
             "alias for 'llvm-jit' (the unit-test run mode name).  Default: "
             "classic-interp.",
    )
    parser.add_argument(
        "--feature", action="append", default=[],
        help="Feature set F of the current report, as compile macros; "
             "repeatable.  E.g. --feature \"-DWASM_ENABLE_GC=1\".  F is an "
             "upper bound: the listed macros are 1 and every other macro is 0, "
             "so a unit target belongs to the report when it enables nothing "
             "outside these (a target enabling a subset of F is admitted, and "
             "the F macros no selected target enables are warned about).  "
             "Running-mode macros (INTERP, FAST_INTERP, JIT, FAST_JIT, "
             "LAZY_JIT, AOT) are not features: the running mode is the "
             "report's --mode, and those macros are ignored by the selection.  "
             "Default (not given): no constraint -- every unit target belongs "
             "to the report, each suite keeping the values its own CMakeLists "
             "declares.  F selects the report's unit targets; it is not "
             "injected into the build.",
    )
    parser.add_argument("--unit", action="store_true",
                        help="Configure, build and run the unit tests whose "
                             "targets match F, and merge their coverage.  The "
                             "selection is recorded in unit-selection.txt.")
    parser.add_argument("--full-test", action="store_true",
                        help="With --unit, build every unit suite including "
                             "the llm-enhanced-test submodule suites "
                             "(-DFULL_TEST=ON).")
    parser.add_argument(
        "--spec", action="append", default=[],
        help="Extra test_wamr.sh switches for the current report's spec run; "
             "repeatable.  `-s spec -b` (spec suite + wabt binary release) "
             "are always passed, so only the feature switches go here, e.g. "
             "--spec \"-G\" (GC) or --spec \"-e\" (exception handling).",
    )
    parser.add_argument(
        "--llvm-dir", default="core/deps/llvm/build/lib/cmake/llvm",
        help="LLVM cmake config dir for the unit suites that need LLVM "
             "(relative paths are resolved against the repository root). "
             "Default: the bundled LLVM build "
             "(core/deps/llvm/build/lib/cmake/llvm).")
    parser.add_argument("--out", default="coverage-reports",
                        help="Output root directory; see the layout below.  A "
                             "relative path is resolved against the directory "
                             "this command is invoked from, and the resolved "
                             "location is printed at startup.")
    parser.add_argument(
        "--merge", action="append", default=[],
        help="Merge previously generated reports by name into <out>/_merged/; "
             "repeatable.",
    )
    args = parser.parse_args(normalize_argv(sys.argv[1:]))

    # Resolve --out here, once: the console and every child process then agree
    # on one absolute path, so nothing can be re-based by a later chdir (the
    # collector resolves relative paths against the repository root).
    args.out = os.path.abspath(args.out)

    if args.merge:
        merge_reports(args.merge, args.out)
        return

    if not args.report:
        parser.error("--report is required (or use --merge)")

    # 'jit' (spec/test_wamr.sh naming) is an alias for 'llvm-jit' (unit-test
    # naming); normalize early so fingerprints and build dirs are stable.
    args.mode = ["llvm-jit" if m == "jit" else m for m in args.mode]

    # Each --report starts a new group; --mode, --spec and --feature are paired
    # by position (mode[i], spec[i], feature[i]); defaults fill the rest.
    reports = []
    for i, name in enumerate(args.report):
        combo = {
            "mode": args.mode[i] if i < len(args.mode) else "classic-interp",
            "spec": args.spec[i] if i < len(args.spec) else "",
            "features": args.feature[i] if i < len(args.feature) else "",
        }
        try:
            parse_feature_flags(combo["features"])
        except ValueError as exc:
            parser.error(f"report '{name}': {exc}")
        reports.append((name, combo))

    # Every report is written even when a step failed, which is what the
    # non-zero exit status below is for.
    failed = {}
    for name, combo in reports:
        failures = run_report(name, combo, args.out, args.unit, args.llvm_dir,
                              args.full_test)
        if failures:
            failed[name] = failures

    if failed:
        print()
        print(f"{sum(len(f) for f in failed.values())} step(s) failed in "
              f"{len(failed)} report(s); the reports were still written")
        for name, failures in failed.items():
            for failure in failures:
                print(f"  {name}: {failure}")
        raise SystemExit(1)


if __name__ == "__main__":
    main()

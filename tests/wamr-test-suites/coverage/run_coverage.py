#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Code coverage runner for WAMR: one invocation, one report.

A report is one (running mode × spec options × feature set F) combination: the
spec suite runs through test_wamr.sh and, with --unit, the unit targets whose
configuration fits inside F (coverage_targets.py) are built and run; the gcov
data of both is collected with gcovr (collect_coverage_gcovr.py) into
<out>/<report>/.

Nothing is copied: the spec layer keeps one iwasm build dir per running mode
(product-mini/platforms/<platform>/build/<mode>/, which test_wamr.sh fills and
collects its own per-suite reports from), and the unit build dirs live under
<out>/_work/<report>/.  A batch (run_full.py) runs this runner once per part and
ends with merge_reports(), one gcovr run over the parts' tracefiles.

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
  <report>/                  the report: index.html (+ per-file *.html),
                             coverage.json, summary.txt, summary.json,
                             fingerprint.txt, unit-selection.txt
  _work/<report>/            this report's unit build dirs, plus logs/ with
                             every child process' output (spec-<mode>.log,
                             unit-configure-<mode>.log, unit-build.log,
                             ctest-<suite>.log, collect.log).  It is also the
                             spec layer's COVERAGE_DIR, so test_wamr.sh writes
                             its own per-suite report under _work/<report>/spec/

This layout describes one report.  run_full.py runs several reports as the parts
of one batch and merges them into a report of its own: the parts live in the
batch's work dir (<out>/_work/full/parts/) and only <out>/full/ and
<out>/_work/full/ appear next to the other reports (see its --help).

The spec half is collected in place: test_wamr.sh builds one iwasm per running
mode into product-mini/platforms/<platform>/build/<mode>/ and that build dir is
the gcov data this report is collected from -- no copy is made, and no running
mode can invalidate another's data.

The console carries this tool's own progress lines only: the mode, the feature
set, the spec command, the selected unit suites with their test counts, and the
report's line/function/branch totals.  A failing step echoes the tail of its log
and names the file, and stops the run with a non-zero status: the report is only
written when every step that feeds it succeeded, so a report is never partial and
never empty.

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


def spec_build_dir(mode: str) -> str:
    """Build directory of the spec-layer iwasm product for one running mode.

    test_wamr.sh builds one iwasm per running mode (build/<mode>/) and that build
    dir is where the mode's gcov data stays, so the modes of one invocation never
    overwrite each other and nothing has to be copied."""
    return os.path.join(IWASM_PLATFORM_DIR, platform(), "build", mode)


def canonical_features(features: str) -> str:
    """Canonical spelling of F: parsed, then re-emitted sorted by macro name, so
    macro order, whitespace and a redundant `=0` do not change it."""
    macros = parse_feature_flags(features)
    return " ".join(f"-D{name}={macros[name]}" for name in sorted(macros))


def fingerprint(combo, unit: bool, full_test: bool) -> str:
    """Fingerprint of the report: a digest of the knobs this invocation ran with
    (running mode, spec switches, feature set F, and whether the unit half ran).
    Recorded in fingerprint.txt; it does not take part in naming anything."""
    raw = "||".join([combo["mode"], combo["spec"], canonical_features(
        combo["features"]), "unit" if unit else "spec-only",
        "full-test" if full_test else "no-full-test"])
    return hashlib.sha1(raw.encode()).hexdigest()[:16]


def count_gcov_files(build_dir: str) -> int:
    """Number of .gcno/.gcda files under build_dir (0 when it does not exist)."""
    count = 0
    for _root, _dirs, files in os.walk(build_dir):
        count += sum(1 for name in files
                     if name.endswith((".gcno", ".gcda")))
    return count


def abort(message: str):
    """Say why the run cannot continue and stop it with a non-zero status."""
    raise SystemExit(f"ERROR: {message}")


def run_spec(mode, spec_opts, log_dir, coverage_dir):
    """Run the spec suite via test_wamr.sh and check that it left gcov data.

    `-s spec` (spec suite) and `-b` (wabt binary release instead of compiling
    wabt) are always passed; spec_opts only carries the extra feature switches.
    `-C` makes test_wamr.sh build with coverage and write its own per-suite
    report under COVERAGE_DIR, and it is what puts the gcov data in
    product-mini/platforms/<platform>/build/<mode>/ -- which is where this
    report collects it from, so nothing is copied here.

    The spec repo is re-cloned on github before every run, which intermittently
    fails, hence the retries in run_logged().

    A failing run aborts: those retries are the only tolerance for a flaky
    dependency (the spec corpus clone), and once they are exhausted there is
    nothing to report.
    """
    script = os.path.join(TESTS_DIR, "test_wamr.sh")
    # test_wamr.sh names the LLVM-JIT mode 'jit'; 'llvm-jit' is the unit name.
    spec_mode = "jit" if mode == "llvm-jit" else mode
    cmd = ["bash", script, "-s", "spec", "-b", "-t", spec_mode, "-C"]
    cmd.extend(shlex.split(spec_opts))
    env = dict(os.environ, COLLECT_CODE_COVERAGE="1",
               COVERAGE_DIR=coverage_dir)
    log_path = os.path.join(log_dir, f"spec-{mode}.log")
    print(f"      $ {format_cmd(cmd)}")
    print(f"      log: {repo_relative(log_path)}", flush=True)
    status = run_logged(cmd, log_path, cwd=TESTS_DIR, env=env, attempts=3)

    build_dir = spec_build_dir(mode)
    files = count_gcov_files(build_dir)
    if not files:
        abort(f"the spec run left no .gcno/.gcda under "
              f"{repo_relative(build_dir)} (rc={status}); was the build made "
              f"with COLLECT_CODE_COVERAGE=1?  Full output: "
              f"{repo_relative(log_path)}")
    if status != 0:
        abort(f"the spec run failed (rc={status}); full output in "
              f"{repo_relative(log_path)}")
    print(f"      ok: {files} gcov files under {repo_relative(build_dir)}")


def configure_unit(workdir, mode, llvm_dir, log_dir, full_test=False):
    """Configure the unit build of one running mode and return its build dir.

    Nothing is built here: the configure is what writes compile_commands.json,
    i.e. the build plan the target selection is made from.  That is also why F
    is deliberately NOT injected: every suite declares the feature values its
    own test plan needs via set(WAMR_BUILD_*), and injecting top-level flags
    would turn core code on for suites whose curated source lists do not link
    the matching wrapper (e.g. SHARED_HEAP=1 breaks llm interpreter-core).

    A configure that does not succeed aborts the run: without a build plan there
    is no report to make.
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
        abort(f"the unit configure failed (rc={status}); full output in "
              f"{repo_relative(log_path)}")
    return build_dir


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

    A failing build or a failing suite aborts the run: the report is not written
    from data that is known to be incomplete.
    """
    if not selection.matched:
        print("      F selects no target; skipping the unit build and test run")
        return
    jobs = min(os.cpu_count() or 4, 8)
    build_log = os.path.join(log_dir, "unit-build.log")
    print(f"      cmake --build ({len(selection.matched)} targets, -j {jobs})"
          " ...", end="", flush=True)
    status = run_logged(
        ["cmake", "--build", build_dir, "-j", str(jobs), "--target"]
        + sorted(selection.matched), build_log)
    print("ok" if status == 0 else f"FAILED (rc={status})")
    if status != 0:
        abort(f"the unit build failed (rc={status}); full output in "
              f"{repo_relative(build_log)}")
    # The unit build dir is reused when the same report runs again, and libgcov
    # *adds* to the counters already in a .gcda: drop them so the report
    # describes this run's test executions only.
    stale = [os.path.join(root, name)
             for root, _dirs, files in os.walk(build_dir)
             for name in files if name.endswith(".gcda")]
    for path in stale:
        os.remove(path)
    if stale:
        print(f"      reset {len(stale)} .gcda counters of an earlier run")
    for suite in sorted(selection.suites):
        log_path = os.path.join(
            log_dir, "ctest-" + suite.replace(os.sep, "-") + ".log")
        status = run_logged(
            ["ctest", "--test-dir", os.path.join(build_dir, suite),
             "--output-on-failure"], log_path)
        summary = ctest_summary(log_path)
        print(f"      ctest {suite:<24} {summary}")
        if status != 0:
            abort(f"the unit suite '{suite}' failed ({summary}); full output "
                  f"in {repo_relative(log_path)}")
    print(f"      ctest logs: {repo_relative(log_dir)}/ctest-*.log")


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
        abort(f"nothing to collect for {repo_relative(out_dir)}: neither the "
              "spec run nor the unit selection produced gcov data")
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
    out_dir = os.path.join(out_root, name)
    # Only the log dir exists up front: the report dir is created by the
    # collector, so a run that fails before it leaves no half-made report behind.
    os.makedirs(log_dir, exist_ok=True)

    mode = combo["mode"]
    spec_opts = combo["spec"]
    features = combo["features"]
    spec_mode = "jit" if mode == "llvm-jit" else mode
    # The fingerprint is a record of this invocation's knobs, so it is taken
    # from the declared configuration (no need to wait for the configure).
    fp = fingerprint(combo, unit, full_test)

    print()
    print("=" * 78)
    print(f"report '{name}' (fingerprint {fp})")
    print(f"  mode        : {mode}")
    print(f"  spec suite  : test_wamr.sh -s spec -b -t {spec_mode} -C "
          f"{spec_opts or '(no extra switch)'}")
    print(f"  feature set : {features or '(none: every unit target)'}")
    print(f"  unit half   : "
          f"{'yes' if unit else 'no'}"
          f"{', FULL_TEST=ON' if unit and full_test else ''}")
    print_path("output root", out_root)
    print_path("report dir", out_dir)
    print_path("work dir", workdir)
    print("=" * 78, flush=True)

    # Phase 1: configure the unit build and select its targets.  The selection
    # reads cmake's build plan, so it happens after the configure and before
    # anything is built or run; the spec layer is not touched yet, so the
    # feature-set warnings are printed before it starts.
    selection = None
    unit_dir = None
    selection_report = ""
    if unit:
        print(f"[unit configure] ({mode})")
        unit_dir = configure_unit(workdir, mode, llvm_dir, log_dir, full_test)
        selection, warnings = select_unit_targets(
            unit_dir, parse_feature_flags(features) or None)
        selection_report = (
            f"mode={mode}\nspec={spec_opts or '(none)'}\n"
            f"features={features or '(none)'}\n\n"
            + selection.describe(warnings) + "\n")

    # Phase 2: run everything the selection kept.  test_wamr.sh builds its own
    # iwasm (one build dir per running mode) and runs the spec suite on it; that
    # build dir is the gcov data this report is collected from.  Every step
    # aborts on failure: a report is only written when the whole run worked.
    print(f"[spec] ({mode})")
    run_spec(mode, spec_opts, log_dir, workdir)

    build_dirs = [spec_build_dir(mode)]
    if selection is not None:
        print(f"[unit run] ({mode})")
        build_and_run_unit(unit_dir, selection, log_dir)
        # collect from the selected suites' build dirs (a suite is the unit
        # ctest and the collector work on)
        build_dirs.extend(selection.build_dirs(unit_dir))

    collect(build_dirs, out_dir, log_dir)

    # Nothing was measured?  Then there is no report to make: stop instead of
    # leaving an empty <out>/<name>/ behind.
    if not os.path.isfile(os.path.join(out_dir, "summary.json")):
        abort(f"report '{name}' has no coverage summary in "
              f"{repo_relative(out_dir)}; the collection produced nothing")

    with open(os.path.join(out_dir, "fingerprint.txt"), "w") as fh:
        fh.write(f"name={name}\n")
        fh.write(f"fingerprint={fp}\n")
        fh.write(f"mode={mode}\n")
        fh.write(f"spec={spec_opts or '(none)'}\n")
        fh.write(f"features={canonical_features(features) or '(none)'}\n")
        fh.write(f"unit={'yes' if unit else 'no'}\n")
        fh.write(f"full_test={'yes' if unit and full_test else 'no'}\n")
        fh.write("spec_data=" + repo_relative(spec_build_dir(mode)) + "\n")
        if selection is not None:
            fh.write("unit_suites=" + ",".join(sorted(selection.suites)) + "\n")

    with open(os.path.join(out_dir, "unit-selection.txt"), "w") as fh:
        fh.write(selection_report)

    print_coverage_summary(out_dir)
    print(f"report '{name}' written to {repo_relative(out_dir)} "
          f"(fingerprint {fp})")
    print("  index.html, coverage.json, summary.txt, summary.json, "
          "fingerprint.txt, unit-selection.txt")


def merge_reports(parts_root, reports, out_dir, log_dir, log_name="merge.log"):
    """Merge a batch's part reports into one report directory.

    A batch (run_full.py) runs this runner once per part, each part writing its
    own report under <parts_root>/<part>/.  Every part's `coverage.json` is a
    gcovr tracefile holding the union of that part's spec and unit coverage, so
    one gcovr --add-tracefile over them is the batch's report -- the same numbers
    as re-collecting every part's raw .gcda (that is what the tests/unit suite of
    this toolchain checks).

    out_dir (the batch's report, created here exactly like a single report's
    directory by the collector) and log_dir (the batch's merge log) are passed
    explicitly: a batch's parts live in its work dir, not next to its report.
    merged-reports.txt is written into the report, so the result says what it
    covers.

    A part without a coverage.json aborts the merge: a batch report that silently
    covers fewer parts than it was asked for is partial, and partial reports are
    not written.
    """
    parts_root = os.path.abspath(parts_root)
    out_dir = os.path.abspath(out_dir)
    tracefiles = []
    for name in reports:
        path = os.path.join(parts_root, name, "coverage.json")
        if not os.path.isfile(path):
            abort(f"part '{name}' has no coverage.json under "
                  f"{repo_relative(parts_root)}; the batch report would be "
                  "partial")
        tracefiles.append(path)

    os.makedirs(log_dir, exist_ok=True)
    log_path = os.path.join(log_dir, log_name)
    print(f"[merge] {len(tracefiles)} part report(s) -> "
          f"{repo_relative(out_dir)}")
    cmd = [sys.executable, COLLECTOR, "--out", out_dir]
    for path in tracefiles:
        cmd += ["--add-tracefile", path]
    status = run_logged(cmd, log_path)
    if status != 0:
        abort(f"merging the part reports failed (rc={status}); full output in "
              f"{repo_relative(log_path)}")

    with open(os.path.join(out_dir, "merged-reports.txt"), "w") as fh:
        for path in tracefiles:
            fh.write(repo_relative(path) + "\n")
    print_coverage_summary(out_dir)
    print(f"merged report written to {repo_relative(out_dir)}")
    print(f"  log: {repo_relative(log_path)}")


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
            "WAMR coverage runner: one invocation, one report.  A report = "
            "(running mode × spec options × feature set)."
        ),
        epilog=OUTPUT_HELP,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--report", required=True,
        help="Name of the report; it is the name of <out>/<report>/ and of "
             "its work dir <out>/_work/<report>/.",
    )
    parser.add_argument(
        "--mode", default="classic-interp", choices=RUNNING_MODES,
        help="Running mode of the report.  'jit' is an alias for 'llvm-jit' "
             "(the unit-test run mode name).  Default: classic-interp.",
    )
    parser.add_argument(
        "--feature", default="",
        help="Feature set F of the report, as compile macros.  "
             "E.g. --feature \"-DWASM_ENABLE_GC=1\".  F is an "
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
        "--spec", default="",
        help="Extra test_wamr.sh switches for the report's spec run.  "
             "`-s spec -b` (spec suite + wabt binary release) "
             "are always passed, so only the feature switches go here, e.g. "
             "--spec \"-G\" (GC) or --spec \"-e\" (exception handling).",
    )
    parser.add_argument(
        "--llvm-dir", default="core/deps/llvm/build/lib/cmake/llvm",
        help="LLVM cmake config dir for the unit suites that need LLVM "
             "(relative paths are resolved against the repository root). "
             "Default: the bundled LLVM build "
             "(core/deps/llvm/build/lib/cmake/llvm).")
    parser.add_argument("--out", default="build/coverage",
                        help="Output root directory; see the layout below.  A "
                             "relative path is resolved against the directory "
                             "this command is invoked from, and the resolved "
                             "location is printed at startup.")
    args = parser.parse_args(normalize_argv(sys.argv[1:]))

    # Resolve --out here, once: the console and every child process then agree
    # on one absolute path, so nothing can be re-based by a later chdir (the
    # collector resolves relative paths against the repository root).
    args.out = os.path.abspath(args.out)

    # 'jit' (spec/test_wamr.sh naming) is an alias for 'llvm-jit' (unit-test
    # naming); normalize early so the build dir and the fingerprint are stable.
    combo = {
        "mode": "llvm-jit" if args.mode == "jit" else args.mode,
        "spec": args.spec,
        "features": args.feature,
    }
    try:
        parse_feature_flags(combo["features"])
    except ValueError as exc:
        parser.error(f"report '{args.report}': {exc}")

    run_report(args.report, combo, args.out, args.unit, args.llvm_dir,
               args.full_test)


if __name__ == "__main__":
    main()

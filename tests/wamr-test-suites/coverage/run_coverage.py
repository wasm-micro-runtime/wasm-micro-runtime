#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Parameterized code coverage runner for WAMR.

Builds iwasm (spec layer) and unit tests under a given (running mode × spec
options) combination and collects coverage with gcovr (see
collect_coverage_gcovr.py).

A "report object" is one or more (running mode × spec options × feature set)
combinations; the fingerprint of a report is the normalized serialization of
what it is made of:

    fingerprint = running modes + spec options + selected unit targets and
                  their macro sets

The unit half comes from the build plan rather than from the spelling of F, so
two spellings of the same configuration identify the same report.

The report's feature set F is what the user spells out as compile macros
(--feature), e.g. '--feature "-DWASM_ENABLE_GC=1"'.  It is an upper bound
(coverage_features.py) and is used only to decide which unit targets belong to
the report -- it is never injected into a build.

Unit target selection: the unit build of each mode is configured (nothing is
built yet) with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; the resulting
compile_commands.json is cmake's build *plan*, and coverage_targets.py reads
each target's name, suite build directory and macro set out of it.  A target
belongs to the report when every macro it enables is enabled by F (E ⊆ F), and
a suite belongs to the report when all of its targets do -- a suite is the unit
that is built, run by ctest and collected.  A target that enables a subset of F
is admitted, so the report can never contain code compiled with a configuration
F does not declare, while F may still declare more than the selected units
exercise; both kinds of mismatch are reported as warnings, never silently
filtered away.  Only then are the selected targets built and their suites
tested, and only those suite build directories are collected.

The spec layer runs through test_wamr.sh, which always gets `-s spec -b`
(spec suite, wabt binary release instead of compiling it); --spec only carries
the extra switches, e.g. '--spec "-G"' for GC, '--spec "-e"' for exception
handling.

Regression tests are NOT part of this tool.

Examples:
  # classic-interp with a curated feature set + spec + unit
  python3 run_coverage.py --report classic-fset --mode classic-interp \
      --feature "-DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_LIBC_BUILTIN=1 \
                 -DWASM_ENABLE_BULK_MEMORY=1 -DWASM_ENABLE_BULK_MEMORY_OPT=1 \
                 -DWASM_ENABLE_SHRUNK_MEMORY=1" \
      --unit --out build/coverage

  # GC spec variant: test_wamr.sh -s spec -b -t classic-interp -C -G
  python3 run_coverage.py --report gc --mode classic-interp --spec "-G" \
      --out build/coverage

  # no feature constraint: every unit suite keeps its own defaults, INCLUDING
  # the llm-enhanced-test submodule suites (FULL_TEST=ON)
  python3 run_coverage.py --report ci --mode classic-interp --unit \
      --full-test --out build/coverage

  # merge two previously generated reports
  python3 run_coverage.py --merge classic-fset --merge ci --out build/coverage
"""

import argparse
import hashlib
import os
import shlex
import shutil
import subprocess
import sys

from coverage_features import (
    config_macro_names,
    feature_flags_for,
    parse_feature_flags,
)
from coverage_targets import (
    parse_compile_commands,
    select,
    warnings_for,
)

COVERAGE_DIR = os.path.dirname(os.path.abspath(__file__))
TESTS_DIR = os.path.dirname(COVERAGE_DIR)                    # tests/wamr-test-suites
WAMR_DIR = os.path.dirname(os.path.dirname(TESTS_DIR))       # repository root
COLLECTOR = os.path.join(COVERAGE_DIR, "collect_coverage_gcovr.py")
UNIT_DIR = os.path.join(WAMR_DIR, "tests", "unit")
IWEAM_PLATFORM_DIR = os.path.join(WAMR_DIR, "product-mini", "platforms")

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
    # (unit_common.cmake), the spec layer still calls it 'jit'.  Note the
    # unit-test configure validates the mode combination: AOT must not be
    # combined with JIT (unit_common.cmake) and the classic interpreter stays
    # on (top-level default), mirroring the CI unit runs.
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
    return os.path.join(IWEAM_PLATFORM_DIR, platform(), "build")


def fingerprint(combos, facts) -> str:
    """Normalized fingerprint of the report's combinations.

    `facts` is the per-combination selection record (Selection.facts(), empty
    for a combination that has no unit build), so the fingerprint is derived
    from the build plan the report was selected from -- the selected targets
    and their macro sets -- rather than from the user's spelling of F."""
    parts = []
    for combo, fact in zip(combos, facts):
        parts.append(combo["mode"])
        parts.append(combo.get("spec", ""))
        parts.append(fact)
    raw = "||".join(parts)
    return hashlib.sha1(raw.encode()).hexdigest()[:16]


def snapshot_coverage_data(src, dst):
    """Copy the gcov artifacts of a build tree into the report's work dir.

    test_wamr.sh builds the spec-layer iwasm *in place* and run_spec() wipes
    that directory before the next variant, so a spec variant's .gcda only
    survives if it is snapshotted right after its run; without the snapshot a
    later `--merge` would silently miss all spec data.

    Only *.gcno/*.gcda are copied, with their relative layout preserved:
    gcov reads those two files and nothing else (verified against gcovr), so
    the object files and binaries of the debug build tree would only waste
    disk.  A real copy -- rather than a hard link -- also keeps the snapshot
    immune to a later rebuild rewriting the same inode in place, which a hard
    link would corrupt (deleting the directory alone would be harmless).

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


def run_spec(workdir, mode, spec_opts=""):
    """Run the spec test suite via test_wamr.sh (phase-1 approach).  The spec
    iwasm build uses test_wamr.sh's fixed feature configuration.

    `-s spec` (spec suite) and `-b` (use the wabt binary release instead of
    compiling wabt from source) are always passed; spec_opts only carries the
    extra feature switches, e.g. '-G' (GC) or '-e' (exception handling).

    test_wamr.sh names the LLVM-JIT mode 'jit'; 'llvm-jit' is the unit-test
    name, so translate it here.

    test_wamr.sh re-clones the spec repo on github before every run, which
    intermittently fails (empty replies / HTTP2 framing errors), so retry a
    few times before giving up.

    test_wamr.sh configures the product iwasm build *in place* (the same
    product-mini/platforms/<platform>/build directory is reused across
    modes and runs), so stale .gcno/.gcda from earlier configurations
    accumulate there.  Wipe it before the run so the collected spec-layer
    data belongs to this combo only.

    Returns the snapshot of the build dir to collect (see
    snapshot_coverage_data)."""
    script = os.path.join(TESTS_DIR, "test_wamr.sh")
    spec_mode = "jit" if mode == "llvm-jit" else mode
    cmd = ["bash", script, "-s", "spec", "-b", "-t", spec_mode, "-C"]
    cmd.extend(shlex.split(spec_opts))
    env = dict(os.environ, COLLECT_CODE_COVERAGE="1")
    build_dir = spec_build_dir()
    if os.path.isdir(build_dir):
        print(f"spec: wiping stale product build dir {build_dir}")
        shutil.rmtree(build_dir)
    last_exc = None
    for attempt in range(1, 4):
        try:
            subprocess.run(cmd, cwd=TESTS_DIR, env=env, check=True)
            break
        except subprocess.CalledProcessError as exc:
            last_exc = exc
            print(f"spec run failed (attempt {attempt}/3); retrying...")
            import time
            time.sleep(5)
    else:
        raise last_exc

    # The in-place build dir is wiped by the next spec variant, so snapshot its
    # gcov data now: this is what the report collects from, and what makes a
    # later `--merge` able to see this variant's spec data.
    if not os.path.isdir(build_dir):
        print(f"WARNING: spec iwasm build dir not found: {build_dir}")
        return None
    snapshot = os.path.join(workdir, f"spec-coverage-{mode}")
    copied = snapshot_coverage_data(build_dir, snapshot)
    if not copied:
        print(f"WARNING: no .gcno/.gcda under {build_dir}; was the build "
              "configured with COLLECT_CODE_COVERAGE=1?")
        return None
    print(f"spec: snapshotted {copied} gcov files to {snapshot}")
    return snapshot


def configure_unit(workdir, mode, llvm_dir, coverage, full_test=False):
    """Configure the unit build of one running mode and return its build dir.

    Nothing is built here: the configure is what writes compile_commands.json,
    i.e. the build plan the target selection is made from.  That is also why
    the report's feature set F is deliberately NOT injected into the unit
    configure: every suite (including the llm-enhanced-test ones under
    FULL_TEST=ON) declares the feature values its own test plan needs via
    set(WAMR_BUILD_*), and injecting top-level -DWAMR_BUILD_*=1 flags would
    turn core code on for suites whose curated source lists do not link the
    matching wrapper (e.g. SHARED_HEAP=1 breaks llm interpreter-core).
    compile_commands.json therefore records exactly what each suite builds, and
    the selection (coverage_targets.py) decides which of those targets belong
    to the report."""
    build_dir = os.path.join(workdir, f"unittest-build-{mode}")
    cmake_args = [
        "cmake", "-S", UNIT_DIR, "-B", build_dir,
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
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
    if coverage:
        cmake_args.append("-DCOLLECT_CODE_COVERAGE=1")
    if llvm_dir:
        cmake_args.append(f"-DLLVM_DIR={resolve_llvm_dir(llvm_dir)}")

    # Reuse already-downloaded googletest/cmocka sources when available (set
    # FETCHCONTENT_LOCAL_SRC to a build dir that contains _deps/): the unit
    # configure downloads them from github/gitlab with libcurl, which is
    # flaky on some networks (HTTP/2 stream errors).
    local_deps = os.environ.get("FETCHCONTENT_LOCAL_SRC", "")
    if local_deps and os.path.isdir(local_deps):
        for name in ("googletest", "cmocka"):
            src = os.path.join(local_deps, name + "-src")
            if os.path.isdir(src):
                cmake_args.append(
                    f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={src}")

    # The googletest download is network-dependent and intermittently fails
    # (HTTP/2 framing); retry the configure a few times before giving up.
    last_exc = None
    for _attempt in range(3):
        try:
            subprocess.run(cmake_args, check=True)
            break
        except subprocess.CalledProcessError as exc:
            last_exc = exc
            print(f"cmake configure failed (attempt "
                  f"{_attempt + 1}/3); retrying...")
    else:
        raise last_exc
    return build_dir


def build_and_run_unit(build_dir, selection):
    """Build the selected targets and run the selected suites.

    Only the selected targets are built (`cmake --build --target`), and only
    the selected suites are run: ctest is organized per suite, so each suite's
    own CTestTestfile.cmake is the run unit.  A suite is selected only when all
    of its targets match F, so "the selected targets of a suite" is the whole
    suite -- the build and the run cannot disagree."""
    if not selection.matched:
        print("unit: F selects no target; skipping the build and the test run")
        return
    jobs = os.cpu_count() or 4
    subprocess.run(
        ["cmake", "--build", build_dir, "-j", str(min(jobs, 8)),
         "--target"] + sorted(selection.matched), check=True)
    for suite in sorted(selection.suites):
        subprocess.run(
            ["ctest", "--test-dir", os.path.join(build_dir, suite),
             "--output-on-failure"], check=True)


def collect(build_dirs, out_dir):
    if not build_dirs:
        print(f"WARNING: nothing to collect for {out_dir}: no spec data and no "
              "selected unit suite.  No report was written.")
        return
    cmd = [sys.executable, COLLECTOR, "--out", out_dir] + build_dirs
    subprocess.run(cmd, check=True)


def run_report(name, combos, out_root, unit, llvm_dir, coverage,
               full_test=False):
    workdir = os.path.join(out_root, "_work", name)
    os.makedirs(workdir, exist_ok=True)

    # Phase 1: configure the unit build of every combination and select its
    # targets.  The selection reads cmake's build plan, so it happens after
    # the configure and before anything is built or run; the spec layer is not
    # touched yet, so the feature-set warnings below are printed before it
    # starts.
    prepared = []
    facts = []
    for combo in combos:
        mode = combo["mode"]
        features = combo.get("features", "")
        f = parse_feature_flags(features) or None
        print(f"\n=== report '{name}' / mode '{mode}' ===")
        if f is None:
            print("F = (none: every unit target belongs to the report)\n")
        else:
            print(f"F = {feature_flags_for(f)}")
            print("(F is an upper bound in the compile-macro plane; a macro it "
                  "does not mention is 0.  It is not injected into the unit "
                  "configure: each suite declares its own, and "
                  "compile_commands.json selects the report's unit targets "
                  "from the result)\n")

        entry = {"combo": combo, "selection": None, "unit_dir": None,
                 "report": ""}
        if unit:
            unit_dir = configure_unit(workdir, mode, llvm_dir, coverage,
                                      full_test)
            targets = parse_compile_commands(
                os.path.join(unit_dir, "compile_commands.json"), unit_dir)
            selection = select(targets, f)
            known = config_macro_names() | {
                macro for target in targets.values() for macro in target.macros}
            warnings = warnings_for(selection, known)
            print(f"unit: {len(targets)} targets in the build plan, "
                  f"{len(selection.matched)} selected in "
                  f"{len(selection.suites)} suites, "
                  f"{len(selection.skipped)} suites skipped, "
                  f"{len(selection.partial)} suites excluded (partial match)")
            for warning in warnings:
                print(f"WARNING: {warning}")
            entry["unit_dir"] = unit_dir
            entry["selection"] = selection
            entry["report"] = (
                f"mode={mode}\nspec={combo.get('spec', '') or '(none)'}\n"
                f"features={features or '(none)'}\n\n"
                + selection.describe(warnings))
        prepared.append(entry)
        facts.append(entry["selection"].facts() if entry["selection"] else "")

    # The fingerprint describes the build plan the report was selected from,
    # so it needs the selection and can only be taken here -- after the
    # configure, still before any build, test run or collection.
    fp = fingerprint(combos, facts)
    out_dir = os.path.join(out_root, f"{name}_{fp}")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "unit-selection.txt"), "w") as fh:
        fh.write("\n\n".join(entry["report"] for entry in prepared
                             if entry["report"]))
        fh.write("\n")

    # Phase 2: run everything the selection kept.
    all_build_dirs = []
    for entry in prepared:
        combo = entry["combo"]
        mode = combo["mode"]
        spec_opts = combo.get("spec", "")

        # spec layer: test_wamr.sh builds its own iwasm (fixed configuration,
        # mode flags + mandatory SPEC_TEST/BULK_MEMORY/REF_TYPES) and runs the
        # spec suite on it; its .gcda lands in the product build dir that
        # spec_build_dir() points at.  run_spec wiped the dir first (no stale
        # objects from other configurations) and returns a snapshot of it,
        # which is what we collect from.
        print(f"spec options: -s spec -b {spec_opts or '(no extra switch)'}",
              flush=True)
        spec_snapshot = run_spec(workdir, mode, spec_opts)
        if spec_snapshot:
            all_build_dirs.append(spec_snapshot)

        if entry["selection"] is not None:
            build_and_run_unit(entry["unit_dir"], entry["selection"])
            # collect from the selected suites' build dirs (a suite is the
            # unit ctest and the collector work on)
            all_build_dirs.extend(
                entry["selection"].build_dirs(entry["unit_dir"]))

    collect(all_build_dirs, out_dir)

    # Write the fingerprint file
    with open(os.path.join(out_dir, "fingerprint.txt"), "w") as f:
        f.write(f"name={name}\n")
        f.write(f"fingerprint={fp}\n")
        f.write("combinations:\n")
        for combo in combos:
            f.write(f"  mode={combo['mode']}\n")
            f.write(f"  features={combo.get('features', '') or '(none)'}\n")
            f.write(f"  spec={combo.get('spec', '') or '(none)'}\n")
    print(f"Report {name} written to {out_dir} (fingerprint {fp})")


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
                if os.path.isdir(p):
                    build_dirs.append(p)
    if not build_dirs:
        raise SystemExit(
            "No _work build dirs found for the given reports; run the reports "
            "first so their .gcda data is available for merging."
        )
    collect(build_dirs, merged_out)
    with open(os.path.join(merged_out, "fingerprint.txt"), "w") as f:
        f.write(f"merged={','.join(reports)}\n")
        f.write(f"build_dirs={build_dirs}\n")
    print(f"Merged report written to {merged_out}")


# Options whose value may itself start with a dash.  argparse would read such a
# value as the *next option* ("expected one argument"), so `--spec -G` and
# `--feature -DWAMR_BUILD_GC=1` are rewritten into the `--opt=value` form
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


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Parameterized WAMR coverage runner.  Report object = "
            "(running mode × spec options × feature set)."
        )
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
             "Default (not given): no constraint -- every unit target belongs "
             "to the report, each suite keeping the values its own CMakeLists "
             "declares.  F selects the report's unit targets; it is not "
             "injected into the build.",
    )
    parser.add_argument("--unit", action="store_true",
                        help="Configure, build and run the unit tests whose "
                             "targets match F, and merge their coverage.")
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
                        help="Output root directory for reports.")
    parser.add_argument(
        "--merge", action="append", default=[],
        help="Merge previously generated reports by name; repeatable.",
    )
    args = parser.parse_args(normalize_argv(sys.argv[1:]))

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
    n_modes = len(args.mode)
    n_specs = len(args.spec)
    n_features = len(args.feature)
    for i, report in enumerate(args.report):
        mode = args.mode[i] if i < n_modes else "classic-interp"
        spec = args.spec[i] if i < n_specs else ""
        features = args.feature[i] if i < n_features else ""
        try:
            parse_feature_flags(features)
        except ValueError as exc:
            parser.error(f"report '{report}': {exc}")
        reports.append({
            "name": report,
            "combos": [{"mode": mode, "spec": spec, "features": features}],
        })

    coverage = True  # this tool always collects coverage
    for r in reports:
        run_report(r["name"], r["combos"], args.out, args.unit,
                   args.llvm_dir, coverage, args.full_test)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

"""Build and run the BA (binary analysis) issue regression tests.

Every invocation performs both steps in one go: build the `wamrc`/`iwasm`
runtimes referenced by the selected test cases (auto-derived from
`running_config.json`, no `--runtime` option), then run the cases and compare
exit code / stdout against the expected results. There is no build-only or
run-only mode.

`--mode <mode>` narrows the selection to the test cases of one running mode, so
only the runtimes needed by those cases are built (and `wamrc` only when a
selected case references it via `compile_options`) -- the fastest way to
exercise a single execution path.

Input:
  * running_config.json  - test case configs: files, runtimes, modes and
                           expected exit code / stdout
  * issues/issue-<id>/   - the wasm test files (selected by -i/--issues)
Output:
  * console summary      - Total / Passed / Failed counts, one per run
  * issues_tests.log     - details of the failing cases

An item may list several `runs` (one iwasm invocation each, with its own
`options`, `argument` and `expected return`). The same id may appear in several
items; every (item, id) pair runs.

Examples:
  python3 build_run.py                    # build all runtimes + run all cases
  python3 build_run.py --mode classic-interp
  python3 build_run.py --mode aot -i 2847,2849
"""

import argparse
import glob
import json
import os
import platform
import re
import shlex
import subprocess
import sys
import traceback
from dataclasses import dataclass
from typing import Dict, List, NoReturn, Optional, Set

WORK_DIR = os.getcwd()
WAMR_DIR = os.path.join(WORK_DIR, "..", "..", "..")

# LLVM cmake config dir used when --llvm-dir is not given, mirroring the
# default of build-scripts/llvm_env.cmake (the LLVM build bundled in this
# repository).  Absolute: cmake runs from the per-runtime build directories.
DEFAULT_LLVM_DIR = os.path.abspath(
    os.path.join(WAMR_DIR, "core", "deps", "llvm", "build", "lib", "cmake", "llvm")
)

# WAMR_BUILD_* cmake flags per runtime name, mirroring the former
# `build_wamr.sh` build_iwasm invocations.
RUNTIME_BUILD_FLAGS: Dict[str, str] = {
    "iwasm-default": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_AOT=1 -DWAMR_BUILD_FAST_INTERP=1"
    ),
    "iwasm-default-gc-enabled": (
        "-DWAMR_BUILD_GC=1 -DWAMR_BUILD_AOT=1 -DWAMR_BUILD_FAST_INTERP=1 "
        "-DWAMR_BUILD_SPEC_TEST=1"
    ),
    "iwasm-llvm-jit": "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_JIT=1",
    "iwasm-fast-jit": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_SIMD=0"
    ),
    "iwasm-default-wasi-disabled": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_AOT=1 -DWAMR_BUILD_FAST_INTERP=1 "
        "-DWAMR_BUILD_LIBC_WASI=0"
    ),
    "iwasm-llvm-jit-wasi-disabled": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_JIT=1 -DWAMR_BUILD_LIBC_WASI=0"
    ),
    "iwasm-fast-jit-wasi-disabled": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_SIMD=0 "
        "-DWAMR_BUILD_LIBC_WASI=0"
    ),
    "iwasm-fast-jit-shared-memory-wasi-disabled": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_SIMD=0 "
        "-DWAMR_BUILD_SHARED_MEMORY=1 -DWAMR_BUILD_LIBC_WASI=0"
    ),
    "iwasm-default-branch-hints-enabled": "-DWAMR_BUILD_BRANCH_HINTS=1",
    "iwasm-default-tail-call-wasi-disabled": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_FAST_INTERP=1 "
        "-DWAMR_BUILD_TAIL_CALL=1 -DWAMR_BUILD_LIBC_WASI=0"
    ),
    "iwasm-poll-oneoff-asan": (
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_AOT=0 "
        "-DWAMR_BUILD_JIT=0 -DWAMR_BUILD_FAST_JIT=0 -DWAMR_BUILD_SIMD=0 "
        "-DWAMR_DISABLE_HW_BOUND_CHECK=1"
    ),
    # Reserved slot for the --mode multi-tier-jit mode: no test case references
    # it yet. multi-tier JIT needs JIT + FAST_JIT + LAZY_JIT all enabled, or
    # iwasm does not recognize the --multi-tier-jit option (see posix/main.c).
    "iwasm-multi-tier-wasi-disabled": (
        "-DWAMR_BUILD_LIBC_WASI=0 -DWAMR_BUILD_LIBC_BUILTIN=1 "
        "-DWAMR_BUILD_REF_TYPES=1 -DWAMR_BUILD_BULK_MEMORY=1 "
        "-DWAMR_BUILD_JIT=1 -DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_LAZY_JIT=1"
    ),
}

TEST_WASM_COMMAND = (
    "./build/build-{runtime}/iwasm {running_options} {running_mode} {file} {argument}"
)
COMPILE_AOT_COMMAND = (
    "./build/build-wamrc/{compiler} {options} -o {out_file} {in_file}"
)
TEST_AOT_COMMAND = "./build/build-{runtime}/iwasm {running_options} {file} {argument}"

LOG_FILE = "issues_tests.log"
LOG_ENTRY = """
=======================================================
Failing issue id: {}.
run with command_lists: {}
{}
{}
=======================================================
"""


# ---------------------------------------------------------------------------
# Error handling: every error branch records to the log and fails loudly.
# ---------------------------------------------------------------------------
def log_error(message: str) -> None:
    """Record an error into the log file."""
    with open(LOG_FILE, "a") as file:
        file.write(f"\n[ERROR] {message}\n")


def fail(message: str, detail: str = "") -> NoReturn:
    """Record the error, print it loudly on stderr, and stop (exit 1).

    Used for infrastructure errors (build/config/file problems) that should
    fail fast: the run must not continue once the setup is broken.
    """
    if detail:
        log_error(f"{message}\n{detail}")
    else:
        log_error(message)
    print(f"[ERROR] {message}", file=sys.stderr)
    if detail:
        print(detail, file=sys.stderr)
    sys.exit(1)


# ---------------------------------------------------------------------------
# JSON helpers
# ---------------------------------------------------------------------------
def read_json_file(file_path: str):
    with open(file_path, "r") as file:
        return json.load(file)


def get_and_check(d: dict, key: str, default=None, nullable: bool = False):
    element = d.get(key, default)
    if not nullable and element is None:
        raise Exception(f"Missing {key} in {d}")
    return element


def get_issue_ids_should_test(selected_ids: Optional[List[int]] = None) -> Set[int]:
    """Find all issue IDs that should be tested in folder issues."""
    if selected_ids:
        return set(selected_ids)

    path_pattern = "issues/issue-*"
    pattern = r"issue-(\d+)"
    issue_numbers: Set[int] = set()
    for dir_path in glob.glob(path_pattern):
        match = re.search(pattern, dir_path)
        if match:
            issue_numbers.add(int(match.group(1)))
    return issue_numbers


# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
def get_platform() -> str:
    return platform.system().lower()


def run_cmd(cmd: List[str], cwd: Optional[str] = None, what: str = "command") -> None:
    """Run a build command; on failure record it, print loudly and stop."""
    try:
        subprocess.run(cmd, cwd=cwd, check=True)
    except subprocess.CalledProcessError as exc:
        fail(f"{what} failed: {' '.join(exc.cmd)} (exit code {exc.returncode})")
    except FileNotFoundError:
        fail(f"{what} failed: executable not found: {cmd[0]}")


def build_wamrc() -> None:
    print("Build wamrc for spec test under aot compile type")
    build_dir = os.path.join(WORK_DIR, "build", "build-wamrc")
    os.makedirs(build_dir, exist_ok=True)
    # LLVM libraries are expected to be prepared beforehand (CI cache or a
    # local build_llvm.sh run). cmake's find_package(LLVM) in llvm_env.cmake
    # reports a clear error if they are missing.
    run_cmd(
        ["cmake", os.path.join(WAMR_DIR, "wamr-compiler")],
        cwd=build_dir,
        what="Configure wamrc (cmake)",
    )
    run_cmd(["make", "-j", "4"], cwd=build_dir, what="Build wamrc (make)")


def build_iwasm(runtime: str, platform: str, coverage: bool,
                llvm_dir: str = "") -> None:
    flags = RUNTIME_BUILD_FLAGS.get(runtime)
    if flags is None:
        fail(f"Unknown runtime '{runtime}'; known runtimes: "
             f"{sorted(RUNTIME_BUILD_FLAGS)}")
    print(f"Build iwasm with compile flags {flags} (runtime {runtime})")

    src_dir = os.path.join(WAMR_DIR, "product-mini", "platforms", platform)
    build_dir = os.path.join(WORK_DIR, "build", f"build-{runtime}")
    os.makedirs(build_dir, exist_ok=True)

    cmake_args = ["cmake", src_dir]
    cmake_args.extend(flags.split())
    cmake_args.extend(["-DCMAKE_BUILD_TYPE=Debug", "-DWAMR_BUILD_SANITIZER=asan"])
    if coverage:
        cmake_args.append("-DCOLLECT_CODE_COVERAGE=1")
    # runtimes with LLVM (JIT/AOT) need an explicit LLVM_DIR
    if llvm_dir:
        cmake_args.append(f"-DLLVM_DIR={llvm_dir}")
    run_cmd(cmake_args, cwd=build_dir, what=f"Configure iwasm {runtime} (cmake)")
    run_cmd(["make", "-j", "4"], cwd=build_dir, what=f"Build iwasm {runtime} (make)")


@dataclass
class Run:
    """One iwasm invocation: the unit of execution and of pass/fail counting."""
    label: str
    options: str
    argument: str
    expected: dict


@dataclass
class Group:
    """All runs of one (item, issue id) pair. compile_options, if any, are
    compiled once for the whole group. Compile-only groups have no runs."""
    issue_id: int
    item: dict
    runs: List[Run]


def make_run(issue_id: int, item: dict, options: str,
             argument: str, expected: dict) -> Run:
    get_and_check(expected, "ret code")
    get_and_check(expected, "stdout content")
    description = get_and_check(expected, "description")
    label = f"#{issue_id} [{get_and_check(item, 'mode')}] {description}"
    return Run(label, options, argument, expected)


def expand_runs(item: dict, issue_id: int, where: str) -> List[Run]:
    """Resolve the iwasm runs of one item: one run from the top-level fields,
    or one run per entry of `runs`. Compile-only items have no runs."""
    compile_options = item.get("compile_options")
    if compile_options and compile_options.get("only compile"):
        # top-level iwasm fields of legacy compile-only items are not used
        if "runs" in item:
            fail(f"{where}: an 'only compile' item must not have 'runs'")
        return []

    get_and_check(item, "runtime")
    get_and_check(item, "mode")
    get_and_check(item, "file")

    if "runs" in item and "expected return" in item:
        fail(f"{where}: use either 'expected return' or 'runs', not both")

    if "runs" not in item:
        if "expected return" not in item:
            fail(f"{where}: missing 'expected return' or 'runs'")
        return [make_run(issue_id, item,
                         options=get_and_check(item, "options"),
                         argument=get_and_check(item, "argument"),
                         expected=get_and_check(item, "expected return"))]

    for key in ("options", "argument"):
        if key in item:
            fail(f"{where}: '{key}' is not allowed at item level when 'runs' is "
                 "used; put it in each run")
    specs = get_and_check(item, "runs")
    if not specs:
        fail(f"{where}: 'runs' must be a non-empty list")

    runs: List[Run] = []
    for position, spec in enumerate(specs):
        spec_where = f"{where} runs[{position}]"
        if "options" not in spec:
            fail(f"{spec_where}: missing 'options'")
        if "expected return" not in spec:
            fail(f"{spec_where}: missing 'expected return'")
        runs.append(make_run(issue_id, item,
                             options=spec["options"],
                             argument=spec.get("argument", ""),
                             expected=spec["expected return"]))
    return runs


def expand_groups(data: dict, mode: Optional[str],
                  selected_ids: Optional[List[int]]) -> List[Group]:
    """Expand running_config.json into (item, id) groups.

    Deprecated items and items of other modes are skipped. -i/--issues keeps
    only the ids it names. Configuration errors stop here, before any build.
    """
    groups: List[Group] = []
    for index, item in enumerate(data.get("test cases", [])):
        if get_and_check(item, "deprecated"):
            print(f"test case {get_and_check(item, 'ids', default=[])} "
                  "are deprecated, continue running next one(s)")
            continue
        if mode is not None and item.get("mode") != mode:
            continue
        for issue_id in get_and_check(item, "ids", default=[]):
            if selected_ids and issue_id not in selected_ids:
                continue
            where = f"test case #{index} (id {issue_id})"
            groups.append(Group(issue_id, item,
                                expand_runs(item, issue_id, where)))
    return groups


def cases_need_wamrc(groups: List[Group]) -> bool:
    """wamrc is needed only when a selected group compiles via it
    (compile_options) -- e.g. every aot case and the wamrc-only cases."""
    for group in groups:
        compile_options = group.item.get("compile_options")
        if compile_options and compile_options.get("compiler") == "wamrc":
            return True
    return False


def build(groups: List[Group], platform: str, coverage: bool,
          llvm_dir: str = "") -> None:
    os.makedirs(os.path.join(WORK_DIR, "build"), exist_ok=True)

    if not groups:
        fail("No active test cases match --mode and -i/--issues (the mode may "
             "be reserved with no cases configured yet); nothing to build or run.")

    runtimes: Set[str] = {group.item["runtime"] for group in groups
                          if "runtime" in group.item}
    print(f"Runtimes to build: {sorted(runtimes)}")

    # wamrc is only built when a selected test case references it, so modes
    # like fast-interp skip the slow LLVM build entirely.
    if cases_need_wamrc(groups):
        print("Selected test cases reference wamrc; building it")
        build_wamrc()

    for runtime in sorted(runtimes):
        build_iwasm(runtime, platform, coverage, llvm_dir)


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
def dump_error_log(failing_issue_id, command_lists, exit_code_cmp, stdout_cmp):
    with open(LOG_FILE, "a") as file:
        file.write(
            LOG_ENTRY.format(failing_issue_id, command_lists, exit_code_cmp, stdout_cmp)
        )


def run_and_compare_results(label, cmd, ret_code, stdout_content) -> bool:
    print("####################################")
    print(f"test BA issue {label}...")
    command_list = shlex.split(cmd)
    try:
        result = subprocess.run(
            command_list,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            errors="ignore",
        )
    except FileNotFoundError:
        fail(f"Failed to run test {label}: executable not found: "
             f"{command_list[0]} (was the build skipped or did it fail?)")

    actual_exit_code = result.returncode
    actual_output = result.stdout.rstrip("\n")

    exit_code_cmp = f"exit code (actual, expected) : {actual_exit_code, ret_code}"
    stdout_cmp = f"stdout (actual, expected) : {actual_output, stdout_content}"

    if actual_exit_code == ret_code and (
        actual_output == stdout_content
        or (stdout_content == "Compile success"
            and actual_output.find(stdout_content) != -1)
        or (len(stdout_content) > 30
            and actual_output.find(stdout_content) != -1)
    ):
        print("== PASS ==")
        return True
    else:
        print(cmd)
        print(exit_code_cmp)
        print(stdout_cmp)
        print(f"== FAILED: {label} ==")
        dump_error_log(label, command_list, exit_code_cmp, stdout_cmp)
        return False


def compile_label(issue_id, compile_options) -> str:
    compiler = get_and_check(compile_options, "compiler")
    expected_return = get_and_check(compile_options, "expected return")
    return f"#{issue_id} [{compiler}] {get_and_check(expected_return, 'description')}"


def run_issue_test_wamrc(issue_id, compile_options) -> bool:
    compiler = get_and_check(compile_options, "compiler")
    in_file = get_and_check(compile_options, "in file")
    out_file = get_and_check(compile_options, "out file")
    options = get_and_check(compile_options, "options")

    expected_return = get_and_check(compile_options, "expected return")
    ret_code = get_and_check(expected_return, "ret code")
    stdout_content = get_and_check(expected_return, "stdout content")

    issue_path = os.path.join(WORK_DIR, f"issues/issue-{issue_id}/")
    actual_file = glob.glob(issue_path + in_file)
    if len(actual_file) != 1:
        fail(f"Expected exactly one file matching '{in_file}' under "
             f"issues/issue-{issue_id}/, found {len(actual_file)}")
    in_file_path = os.path.join(issue_path, actual_file[0])
    out_file_path = os.path.join(issue_path, out_file)

    cmd = COMPILE_AOT_COMMAND.format(
        compiler=compiler, options=options, out_file=out_file_path, in_file=in_file_path
    )
    label = compile_label(issue_id, compile_options)
    return run_and_compare_results(label, cmd, ret_code, stdout_content)


def run_issue_test_iwasm(issue_id, test_case, run: Run) -> bool:
    runtime = get_and_check(test_case, "runtime")
    mode = get_and_check(test_case, "mode")
    file = get_and_check(test_case, "file")

    ret_code = get_and_check(run.expected, "ret code")
    stdout_content = get_and_check(run.expected, "stdout content")

    issue_path = os.path.join(WORK_DIR, f"issues/issue-{issue_id}/")
    actual_file = glob.glob(issue_path + file)
    if len(actual_file) != 1:
        fail(f"Expected exactly one file matching '{file}' under "
             f"issues/issue-{issue_id}/, found {len(actual_file)}")
    file_path = os.path.join(issue_path, actual_file[0])

    if mode == "aot":
        cmd = TEST_AOT_COMMAND.format(
            runtime=runtime,
            file=file_path,
            running_options=run.options,
            argument=run.argument,
        )
    else:
        if mode == "classic-interp":
            running_mode = "--interp"
        elif mode == "fast-interp":
            running_mode = ""
        else:
            running_mode = f"--{mode}"

        cmd = TEST_WASM_COMMAND.format(
            runtime=runtime,
            running_mode=running_mode,
            file=file_path,
            running_options=run.options,
            argument=run.argument,
        )

    return run_and_compare_results(run.label, cmd, ret_code, stdout_content)


def run(groups: List[Group], selected_ids: Optional[List[int]]) -> None:
    folder_ids = get_issue_ids_should_test(selected_ids)

    passed: List[str] = []
    failed: List[str] = []
    ran_ids: Set[int] = set()

    # Every (item, id) group runs; the same id may appear in several items.
    for group in groups:
        ran_ids.add(group.issue_id)
        compile_options = group.item.get("compile_options")

        if compile_options:
            compiled = run_issue_test_wamrc(group.issue_id, compile_options)
            if not group.runs:
                # compile-only group: the compile is the single unit
                label = compile_label(group.issue_id, compile_options)
                (passed if compiled else failed).append(label)
                continue
            # compile first, then run iwasm on the produced .aot
            if not compiled:
                failed.extend(iwasm_run.label for iwasm_run in group.runs)
                log_error(f"issue #{group.issue_id}: wamrc compile failed, "
                          "skipped the iwasm run(s)")
                continue

        for iwasm_run in group.runs:
            if run_issue_test_iwasm(group.issue_id, group.item, iwasm_run):
                passed.append(iwasm_run.label)
            else:
                failed.append(iwasm_run.label)

    total = len(passed) + len(failed)

    def format_issue_ids(ids: Set[int]) -> str:
        return " ".join(f"#{x}" for x in sorted(ids)) if ids else "no more"

    # ids in the folder (or given by -i) that no active test case covers
    not_run_ids = folder_ids - ran_ids
    # ids of active test cases whose folder is missing
    json_ids = {group.issue_id for group in groups}
    json_only_ids = json_ids - ran_ids

    print("####################################")
    print("==== Test results ====")
    print(f"   Total: {total}")
    print(f"  Passed: {len(passed)}")
    print(f"  Failed: {len(failed)}")
    if not selected_ids:
        print(f"  Left issues in folder: {format_issue_ids(not_run_ids)}")
        print(f"  Cases in JSON but not found in folder: {format_issue_ids(json_only_ids)}")
    else:
        print(f"  Issues not run (not found in config or filtered by mode): "
              f"{format_issue_ids(not_run_ids)}")

    if failed:
        print(f"[ERROR] {len(failed)} test(s) failed, see {LOG_FILE} for details.",
              file=sys.stderr)
        sys.exit(1)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def parse_issue_ids(value: str) -> List[int]:
    """argparse type for -i/--issues: '1,2,3' -> [1, 2, 3]."""
    try:
        ids = [int(x) for x in value.split(",") if x.strip()]
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid issue id list: {value!r}")
    if not ids:
        raise argparse.ArgumentTypeError(f"empty issue id list: {value!r}")
    return ids


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Build and run the BA (binary analysis) issue regression tests. "
            "Every invocation performs both steps: build then run. Only the "
            "wamrc/iwasm runtimes referenced by the selected test cases are "
            "built, so choosing --mode keeps the build short.\n"
            "\n"
            "Input:\n"
            "  * running_config.json - test case configs: files, runtimes, modes\n"
            "                          and expected exit code / stdout\n"
            "  * issues/issue-<id>/  - the wasm test files (selected by\n"
            "                          -i/--issues)\n"
            "\n"
            "Output:\n"
            "  * console summary     - Total / Passed / Failed counts\n"
            "  * issues_tests.log    - details of the failing cases\n"
            "\n"
            "Without --mode, every active test case (all running modes) is built\n"
            "and run. Choose --mode to narrow the selection to one running mode\n"
            "and cut execution time; pick the mode matching what you changed:\n"
            "classic-interp / fast-interp for interpreter paths, llvm-jit /\n"
            "fast-jit for JIT paths, aot for the wamrc+AOT path (also builds\n"
            "wamrc). multi-tier-jit is reserved and has no test cases yet."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--mode",
        type=str,
        choices=["aot", "classic-interp", "fast-interp", "fast-jit", "llvm-jit",
                 "multi-tier-jit"],
        help="Only build/run the test cases of this running mode; the runtimes "
             "are auto-derived from running_config.json (no --runtime option) "
             "and wamrc is built only when a selected case references it. "
             "Default (not given): run every active test case (all modes). "
             "aot: wamrc-compile .wasm to .aot then run (needs wamrc); "
             "classic-interp / fast-interp: interpreter paths; "
             "llvm-jit / fast-jit: JIT paths; multi-tier-jit: reserved, no "
             "test cases yet.",
    )
    parser.add_argument(
        "-i",
        "--issues",
        type=parse_issue_ids,
        help="Comma separated list of issue ids to run, e.g. 1,2,3. Only the "
             "test cases of these ids are built and run. Default: all.",
    )
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="Build iwasm with -DCOLLECT_CODE_COVERAGE=1 so that gcov data "
             "(.gcno/.gcda) is produced and can be collected by gcovr.",
    )
    parser.add_argument(
        "--llvm-dir",
        default=DEFAULT_LLVM_DIR,
        help="LLVM cmake config dir for runtimes that need LLVM. Default: "
             f"{DEFAULT_LLVM_DIR} (the bundled LLVM build, see "
             "build-scripts/llvm_env.cmake). Pass an empty value to let cmake "
             "resolve LLVM itself.",
    )
    args = parser.parse_args()

    try:
        data = read_json_file("running_config.json")
        if data is None:
            fail("No data to process.")

        groups = expand_groups(data, args.mode, args.issues)

        platform = get_platform()
        build(groups, platform, args.coverage, llvm_dir=args.llvm_dir)

        if os.path.exists(LOG_FILE):
            os.remove(LOG_FILE)
        run(groups, args.issues)
    except SystemExit:
        raise
    except Exception as exc:
        fail(f"Unexpected error: {exc!r}", detail=traceback.format_exc())


if __name__ == "__main__":
    main()

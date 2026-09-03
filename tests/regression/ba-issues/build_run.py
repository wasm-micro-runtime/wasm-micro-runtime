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
  * issues/issue-<id>/   - the wasm test files (filtered with -i/--issues)
Output:
  * console summary      - Total / Passed / Failed counts
  * issues_tests.log     - details of the failing cases

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
from typing import Dict, List, NoReturn, Optional, Set

WORK_DIR = os.getcwd()
WAMR_DIR = os.path.join(WORK_DIR, "..", "..", "..")

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


def build_iwasm(runtime: str, platform: str, coverage: bool) -> None:
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
    run_cmd(cmake_args, cwd=build_dir, what=f"Configure iwasm {runtime} (cmake)")
    run_cmd(["make", "-j", "4"], cwd=build_dir, what=f"Build iwasm {runtime} (make)")


def select_test_cases(data: dict, mode: Optional[str]) -> List[dict]:
    """Test cases that will actually run: not deprecated and mode-matching."""
    selected: List[dict] = []
    for test_case in data.get("test cases", []):
        if get_and_check(test_case, "deprecated"):
            continue
        if mode is not None and test_case.get("mode") != mode:
            continue
        selected.append(test_case)
    return selected


def collect_needed_runtimes(test_cases: List[dict]) -> Set[str]:
    """Runtime names referenced by the selected test cases (built subset)."""
    runtimes: Set[str] = set()
    for test_case in test_cases:
        runtime = test_case.get("runtime")
        if runtime is not None:
            runtimes.add(runtime)
    return runtimes


def cases_need_wamrc(test_cases: List[dict]) -> bool:
    """wamrc is needed only when a selected test case compiles via it
    (compile_options) -- e.g. every aot case and the wamrc-only cases."""
    for test_case in test_cases:
        compile_options = test_case.get("compile_options")
        if compile_options and compile_options.get("compiler") == "wamrc":
            return True
    return False


def build(data: dict, platform: str, mode: Optional[str], coverage: bool) -> None:
    os.makedirs(os.path.join(WORK_DIR, "build"), exist_ok=True)

    test_cases = select_test_cases(data, mode)
    if not test_cases:
        fail(f"No active test cases for mode '{mode}' (the mode may be reserved "
             "with no cases configured yet); nothing to build or run.")

    runtimes = collect_needed_runtimes(test_cases)
    print(f"Runtimes to build (mode={mode}): {sorted(runtimes)}")

    # wamrc is only built when a selected test case references it, so modes
    # like fast-interp skip the slow LLVM build entirely.
    if cases_need_wamrc(test_cases):
        print("Selected test cases reference wamrc; building it")
        build_wamrc()

    for runtime in sorted(runtimes):
        build_iwasm(runtime, platform, coverage)


# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
def dump_error_log(failing_issue_id, command_lists, exit_code_cmp, stdout_cmp):
    with open(LOG_FILE, "a") as file:
        file.write(
            LOG_ENTRY.format(failing_issue_id, command_lists, exit_code_cmp, stdout_cmp)
        )


def run_and_compare_results(issue_id, cmd, description, ret_code, stdout_content) -> bool:
    print("####################################")
    print(f"test BA issue #{issue_id} `{description}`...")
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
        fail(f"Failed to run test issue #{issue_id}: executable not found: "
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
        print(f"== FAILED: {issue_id} ==")
        dump_error_log(issue_id, command_list, exit_code_cmp, stdout_cmp)
        return False


def run_issue_test_wamrc(issue_id, compile_options) -> bool:
    compiler = get_and_check(compile_options, "compiler")
    in_file = get_and_check(compile_options, "in file")
    out_file = get_and_check(compile_options, "out file")
    options = get_and_check(compile_options, "options")

    expected_return = get_and_check(compile_options, "expected return")
    ret_code = get_and_check(expected_return, "ret code")
    stdout_content = get_and_check(expected_return, "stdout content")
    description = get_and_check(expected_return, "description")

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
    return run_and_compare_results(issue_id, cmd, description, ret_code, stdout_content)


def run_issue_test_iwasm(issue_id, test_case) -> bool:
    runtime = get_and_check(test_case, "runtime")
    mode = get_and_check(test_case, "mode")
    file = get_and_check(test_case, "file")
    options = get_and_check(test_case, "options")
    argument = get_and_check(test_case, "argument")

    expected_return = get_and_check(test_case, "expected return")
    ret_code = get_and_check(expected_return, "ret code")
    stdout_content = get_and_check(expected_return, "stdout content")
    description = get_and_check(expected_return, "description")

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
            running_options=options,
            argument=argument,
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
            running_options=options,
            argument=argument,
        )

    return run_and_compare_results(issue_id, cmd, description, ret_code, stdout_content)


def run(data: dict, mode: Optional[str], selected_ids: Optional[List[int]]) -> None:
    for test_case in data.get("test cases", []):
        if get_and_check(test_case, "deprecated"):
            print(f"test case {get_and_check(test_case, 'ids', default=[])} "
                  "are deprecated, continue running next one(s)")

    test_cases = select_test_cases(data, mode)
    json_ids = {issue_id for test_case in test_cases
                for issue_id in get_and_check(test_case, "ids", default=[])}
    folder_ids = get_issue_ids_should_test(selected_ids)

    passed_ids: Set[int] = set()
    failed_ids: Set[int] = set()
    ran_ids: Set[int] = set()

    for test_case in test_cases:
        compile_options = get_and_check(test_case, "compile_options", nullable=True)
        for issue_id in get_and_check(test_case, "ids", default=[]):
            # The same id may appear in several test cases; run it only once.
            if issue_id in ran_ids:
                continue
            ran_ids.add(issue_id)

            if compile_options:
                if compile_options["only compile"]:
                    if run_issue_test_wamrc(issue_id, compile_options):
                        passed_ids.add(issue_id)
                    else:
                        failed_ids.add(issue_id)
                    continue
                # compile first, then run iwasm on the produced .aot
                if not run_issue_test_wamrc(issue_id, compile_options):
                    failed_ids.add(issue_id)
                    log_error(f"issue #{issue_id}: wamrc compile failed, "
                              "skipped the iwasm run")
                    continue

            if run_issue_test_iwasm(issue_id, test_case):
                passed_ids.add(issue_id)
            else:
                failed_ids.add(issue_id)

    total = len(passed_ids) + len(failed_ids)
    passed = len(passed_ids)
    failed = len(failed_ids)

    def format_issue_ids(ids: Set[int]) -> str:
        return " ".join(f"#{x}" for x in sorted(ids)) if ids else "no more"

    # ids in the folder (or given by -i) that no active test case covers
    not_run_ids = folder_ids - ran_ids
    # ids of active test cases whose folder is missing
    json_only_ids = json_ids - ran_ids

    print("####################################")
    print("==== Test results ====")
    print(f"   Total: {total}")
    print(f"  Passed: {passed}")
    print(f"  Failed: {failed}")
    if not selected_ids:
        print(f"  Left issues in folder: {format_issue_ids(not_run_ids)}")
        print(f"  Cases in JSON but not found in folder: {format_issue_ids(json_only_ids)}")
    else:
        print(f"  Issues not run (not found in config or filtered by mode): "
              f"{format_issue_ids(not_run_ids)}")

    if failed > 0:
        print(f"[ERROR] {failed} test(s) failed, see {LOG_FILE} for details.",
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
            "  * issues/issue-<id>/  - the wasm test files (filtered by\n"
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
        help="Comma separated list of issue ids to run, e.g. 1,2,3. Default: all.",
    )
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="Build iwasm with -DCOLLECT_CODE_COVERAGE=1 so that gcov data "
             "(.gcno/.gcda) is produced and can be collected by gcovr.",
    )
    args = parser.parse_args()

    try:
        data = read_json_file("running_config.json")
        if data is None:
            fail("No data to process.")

        platform = get_platform()
        build(data, platform, args.mode, args.coverage)

        if os.path.exists(LOG_FILE):
            os.remove(LOG_FILE)
        run(data, args.mode, args.issues)
    except SystemExit:
        raise
    except Exception as exc:
        fail(f"Unexpected error: {exc!r}", detail=traceback.format_exc())


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
#
# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

C_SOURCE_SUFFIXES = {".c", ".cc", ".cpp"}
C_HEADER_SUFFIXES = {".h"}
C_SUFFIXES = C_SOURCE_SUFFIXES | C_HEADER_SUFFIXES
CLANG_TIDY_CANDIDATES = ["clang-tidy-21", "clang-tidy"]
CLANG_TIDY_DIFF_CANDIDATES = [
    "clang-tidy-diff-21.py",
    "clang-tidy-diff-21",
    "clang-tidy-diff.py",
    "clang-tidy-diff",
]

# The flag vectors of samples/minimum/CMakePresets.json that the check scans
# with.  Every one of them is required: a preset that does not configure leaves
# a whole configuration unchecked.
PRESETS = [
    "all-on-classic-interp",
    "all-on-fast-interp",
    "all-on-aot",
    "all-on-fast-jit",
    "all-on-llvm-jit",
]

# Use WAMR_LLVM_DIR to locate LLVM, if set.  Empty means "let llvm_env.cmake
# look", which uses the WAMR-bundled LLVM under core/deps/llvm.
LLVM_DIR = os.environ.get("WAMR_LLVM_DIR", "")

# Exit codes, with the same meanings as the verdicts in coding_guidelines.yml.
# Each one answers a different question, so a check that did not run to the
# end never looks like one that did and found nothing.
EXIT_SUCCESS = 0  # ran, found no errors, nothing that should be scanned was left out
EXIT_ERRORS_FOUND = 1  # ran, found errors
EXIT_PARTIAL_SCAN = 2  # ran, scanned only part of the change
EXIT_RUN_FAILED = 3  # did not run to completion

HUNK_HEADER = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
LINE_MARKER = re.compile(r'^# (\d+) "((?:[^"\\]|\\.)*)"')
DIAGNOSTIC = re.compile(r"^.+:\d+:\d+: (?:warning|error): ")


def skip(reason: str) -> int:
    print(f"--- clang-tidy: skipped ({reason})")
    return EXIT_PARTIAL_SCAN


def unavailable(reason: str) -> int:
    """Missing tooling is a failure, on a developer's machine and in CI alike: a
    check that could not run must not look like one that ran and found nothing."""
    print(f"--- clang-tidy: {reason}")
    return EXIT_RUN_FAILED


def find_command(candidates: list) -> str:
    for candidate in candidates:
        command = shutil.which(candidate)
        if command:
            return command

    return ""


def diff_revisions(commits: str) -> list:
    """What to hand git diff: the index for a pre-commit hook, a commit range
    for a CI run over a whole pull request."""
    return [commits] if commits else ["--cached"]


def get_changed_paths(root: Path, commits: str) -> list:
    try:
        output = subprocess.check_output(
            [
                "git",
                "diff",
                *diff_revisions(commits),
                "--name-only",
                "--diff-filter=ACMR",
            ],
            cwd=root,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        # An invalid range is not "no changed files": nothing can be scanned, so
        # the caller has to fail instead of taking the skip path.
        return None

    return [line for line in output.splitlines() if line]


def get_diff(root: Path, commits: str, files: list) -> str:
    try:
        return subprocess.check_output(
            ["git", "diff", *diff_revisions(commits), "-U0", "--", *files],
            cwd=root,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        # None, not "": an empty diff means "no changed lines", and a failed git
        # must not be read as that.
        return None


def get_changed_lines(diff: str) -> set:
    """The line numbers a -U0 diff touches on the new side of one file."""
    changed = set()
    for line in diff.splitlines():
        match = HUNK_HEADER.match(line)
        if not match:
            continue

        start = int(match.group(1))
        count = 1 if match.group(2) is None else int(match.group(2))
        # count == 0 is a pure deletion: nothing left to look at.
        changed.update(range(start, start + count))

    return changed


def clang_toolchain(root: Path) -> Path:
    """Every compile DB here is configured with clang, because clang-tidy
    replays its command lines through clang's own driver: config_common adds
    -mindirect-branch-register when the compiler is GCC, correct for the build
    and an unknown argument to clang, which then abandons the whole TU."""
    return root.joinpath("build-scripts", "clang_toolchain.cmake")


def preset_build_dir(root: Path, preset: str) -> Path:
    return root.joinpath("build", preset)


def generate_preset_compile_commands(root: Path, preset: str) -> str:
    """Configure one samples/minimum preset into <repo>/build/<preset>.  No
    build step: the compile DB is a product of configure alone.  -B overrides
    the preset's binaryDir, which keeps this out of the directory a developer
    configured for themselves under samples/minimum/build/.

    Returns '' on success, otherwise why the preset could not be configured."""
    if not find_command(["cmake"]):
        return "cmake not found"

    command = [
        "cmake",
        "--preset",
        preset,
        "-B",
        str(preset_build_dir(root, preset)),
        f"-DCMAKE_TOOLCHAIN_FILE={clang_toolchain(root)}",
    ]
    if LLVM_DIR:
        command.append("-DWAMR_BUILD_WITH_CUSTOM_LLVM=1")
        command.append(f"-DLLVM_DIR={LLVM_DIR}")

    failure = f"preset {preset} failed to configure"
    try:
        result = subprocess.run(
            command,
            cwd=root.joinpath("samples", "minimum"),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
    except OSError:
        # samples/minimum is missing, as in a tree that predates the presets.
        # That is a preset that did not configure, not a crash.
        return failure

    return "" if result.returncode == 0 else failure


# The AOT compiler (wamrc) is not a samples/minimum preset: it is its own cmake
# project, and its sources are only compiled by it.
def generate_wamrc_compile_commands(root: Path) -> str:
    """Configure wamr-compiler into <repo>/build/wamrc.  Same contract as
    generate_preset_compile_commands: configure only, no build step.

    Returns '' on success, otherwise why wamrc could not be configured."""
    if not find_command(["cmake"]):
        return "cmake not found"

    command = [
        "cmake",
        "-S",
        str(root.joinpath("wamr-compiler")),
        "-B",
        str(preset_build_dir(root, "wamrc")),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        f"-DCMAKE_TOOLCHAIN_FILE={clang_toolchain(root)}",
    ]
    if LLVM_DIR:
        command.append("-DWAMR_BUILD_WITH_CUSTOM_LLVM=1")
        command.append(f"-DLLVM_DIR={LLVM_DIR}")

    failure = "wamrc failed to configure"
    try:
        result = subprocess.run(
            command,
            cwd=root.joinpath("wamr-compiler"),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
    except OSError:
        return failure

    return "" if result.returncode == 0 else failure


def resolve_build_dirs(root: Path) -> tuple:
    """The (preset, build directory) pairs to scan with, in report order.

    Every preset is re-configured on every run.  A compile DB left over from
    an earlier commit describes the source files and feature flags of that
    commit, and a change to either is exactly the kind of thing this check is
    supposed to notice.

    Every preset is required, so a preset that does not configure is reported
    as a failure: the flag vectors only cover the feature macros together, and a
    missing preset means a whole configuration went unchecked.  CI provides what
    the presets need (LLVM for all-on-llvm-jit, see LLVM_DIR).

    Returns (build_dirs, failure_reason); failure_reason is '' on success."""
    build_dirs = []
    for preset in PRESETS:
        reason = generate_preset_compile_commands(root, preset)
        if reason:
            return [], reason

        build_dir = preset_build_dir(root, preset)
        if not build_dir.joinpath("compile_commands.json").is_file():
            return [], f"preset {preset} wrote no compile DB"

        build_dirs.append((preset, build_dir))

    reason = generate_wamrc_compile_commands(root)
    if reason:
        return [], reason

    build_dir = preset_build_dir(root, "wamrc")
    if not build_dir.joinpath("compile_commands.json").is_file():
        return [], "wamrc wrote no compile DB"

    build_dirs.append(("wamrc", build_dir))
    return build_dirs, ""


def normalize_compile_db_file(root: Path, entry: dict) -> Path:
    # joinpath() keeps an absolute entry["file"] as it is.
    file_path = Path(entry.get("directory", root)).joinpath(entry["file"])
    return file_path.resolve()


def load_compile_db(root: Path, compile_db: Path) -> dict:
    """Source file -> the compile commands that build it in this DB."""
    try:
        entries = json.loads(compile_db.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as ex:
        print(f"--- clang-tidy: failed to read {compile_db}: {ex}")
        return {}

    sources = {}
    for entry in entries:
        if "file" not in entry:
            continue

        file_path = normalize_compile_db_file(root, entry)
        if file_path.suffix in C_SOURCE_SUFFIXES:
            sources.setdefault(file_path, []).append(entry)

    return sources


def preprocess_argv(entry: dict) -> list:
    """The entry's own compile command, turned into a preprocess-only one."""
    if "arguments" in entry:
        argv = list(entry["arguments"])
    elif "command" in entry:
        argv = shlex.split(entry["command"])
    else:
        return []

    stripped = []
    index = 0
    while index < len(argv):
        argument = argv[index]
        # -c and the output file have no meaning for -E, and -MD/-MF would
        # overwrite the build's own dependency files.
        if argument == "-c":
            index += 1
        elif argument in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
        elif argument in ("-MD", "-MMD"):
            index += 1
        else:
            stripped.append(argument)
            index += 1

    # -C keeps comments, so a changed comment line inside an active branch
    # counts as compiled instead of looking like dead code.
    return stripped + ["-E", "-C"]


def active_lines(entry: dict, target: Path) -> set:
    """The lines of `target` that survive preprocessing under this entry's
    flags.  A line inside an #if branch the flags turn off never appears, which
    is exactly the question "would clang-tidy even see my change here".

    None means the preprocessor could not run on the entry, for example a
    missing header.  That is not the same as every line being off, and it must
    not be reported as one."""
    argv = preprocess_argv(entry)
    if not argv:
        return None

    directory = Path(entry.get("directory", "."))
    try:
        result = subprocess.run(
            argv,
            cwd=directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            universal_newlines=True,
        )
    except OSError:
        return None

    if result.returncode != 0:
        return None

    resolved = {}
    lines = set()
    in_target = False
    current_line = 0
    for output_line in result.stdout.splitlines():
        marker = LINE_MARKER.match(output_line)
        if marker:
            current_line = int(marker.group(1))
            name = marker.group(2)
            if name not in resolved:
                resolved[name] = directory.joinpath(name).resolve() == target
            in_target = resolved[name]
            continue

        if in_target:
            if output_line.strip():
                lines.add(current_line)
            current_line += 1

    return lines


def format_line_numbers(lines: set) -> str:
    """1,2,3,7 -> 1-3,7"""
    ranges = []
    for line in sorted(lines):
        if ranges and line == ranges[-1][1] + 1:
            ranges[-1][1] = line
        else:
            ranges.append([line, line])

    return ",".join(
        str(first) if first == last else f"{first}-{last}" for first, last in ranges
    )


def scanned_lines(results: list) -> set:
    """The changed lines that at least one preset compiles."""
    scanned = set()
    for _, hit in results:
        scanned |= hit

    return scanned


def report_coverage(coverage: dict, changed_lines: dict, uncovered: set) -> None:
    print("--- clang-tidy: which presets compile the changed lines")
    for source in sorted(changed_lines):
        total = len(changed_lines[source])
        note = " -- in no compile DB at all" if source in uncovered else ""
        print(f"      {source} ({total} changed line(s)){note}")
        if source in uncovered:
            # Not checked, so it has to be visible in the pull request instead of
            # only in the log.  GitHub reads the annotation, the pre-commit hook
            # shows it as plain text.
            print(f"::warning file={source}::no preset compiles this source")
        for name, hit in coverage[source]:
            verdict = f"{len(hit)}/{total}" if hit else "not compiled"
            print(f"        {name:<24} {verdict}")

        blind = changed_lines[source] - scanned_lines(coverage[source])
        if blind:
            # Not an error: it is how a change to an #if branch nobody enables
            # looks.  Saying it out loud is the point -- silence here reads
            # exactly like a clean result.
            print(
                f"        no preset compiles line(s) {format_line_numbers(blind)}, "
                "they go unscanned"
            )


def run_clang_tidy_diff(
    root: Path,
    name: str,
    build_dir: Path,
    commits: str,
    files: list,
    clang_tidy: str,
    clang_tidy_diff: str,
) -> int:
    diff = get_diff(root, commits, files)
    if diff is None:
        print(f"--- clang-tidy: [{name}] cannot read the diff")
        return EXIT_RUN_FAILED

    if not diff:
        return EXIT_SUCCESS

    command = [
        clang_tidy_diff,
        "-p1",
        "-path",
        str(build_dir),
        "-clang-tidy-binary",
        clang_tidy,
    ]
    result = subprocess.run(
        command,
        cwd=root,
        input=diff,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    found = False
    for output_line in result.stdout.splitlines():
        print(f"[{name}] {output_line}")
        if DIAGNOSTIC.match(output_line):
            found = True

    # clang-tidy-diff.py only started propagating a non-zero exit code in
    # LLVM 15, so a diagnostic from an older driver would pass silently.  Read
    # the diagnostics out of the output instead of trusting the exit code.
    if found:
        return EXIT_ERRORS_FOUND

    # A non-zero exit without a diagnostic is the tool failing to run, not a
    # finding in the code.
    if result.returncode != 0:
        return EXIT_RUN_FAILED

    return EXIT_SUCCESS


def collect_coverage(root: Path, commits: str, sources: list, compile_dbs: list):
    """Which changed lines each preset compiles, for every changed source.

    Two questions per changed source, in this order: is the file in this DB at
    all (file level), and if it is, do any of the changed lines survive the
    preprocessor under those flags (line level)?  A file that passes the first
    and fails the second produces no clang-tidy output, which is
    indistinguishable from a clean result unless we say so here.

    Returns (changed_lines, coverage, uncovered), or None after reporting why the
    coverage could not be determined."""
    changed_lines = {}
    coverage = {}
    uncovered = set()
    for source in sources:
        path = root.joinpath(source).resolve()
        diff = get_diff(root, commits, [source])
        if diff is None:
            print(f"--- clang-tidy: cannot read the diff for {source}")
            return None

        lines = get_changed_lines(diff)
        if not lines:
            continue

        changed_lines[source] = lines
        if not any(path in db for _, _, db in compile_dbs):
            uncovered.add(source)

        coverage[source] = []
        for name, _, db in compile_dbs:
            hit = set()
            for entry in db.get(path, []):
                active = active_lines(entry, path)
                if active is None:
                    print(f"--- clang-tidy: [{name}] cannot preprocess {source}")
                    return None
                hit |= active & lines
                if hit == lines:
                    break

            coverage[source].append((name, hit))

    return changed_lines, coverage, uncovered


def process_changes(root: Path, commits: str) -> int:
    changed_paths = get_changed_paths(root, commits)
    if changed_paths is None:
        print(
            "--- clang-tidy: cannot list the changed files for "
            f"{commits or '--cached'}"
        )
        return EXIT_RUN_FAILED

    changed_files = [
        path for path in changed_paths if Path(path).suffix in C_SUFFIXES
    ]
    # Nothing clang-tidy would look at (docs, scripts, deleted files): there is
    # nothing that should have been scanned, so this is a success.
    if not changed_files:
        print("--- clang-tidy: no C/C++ files changed, nothing to check")
        return EXIT_SUCCESS

    clang_tidy = find_command(CLANG_TIDY_CANDIDATES)
    if not clang_tidy:
        return unavailable("clang-tidy not found")

    clang_tidy_diff = find_command(CLANG_TIDY_DIFF_CANDIDATES)
    if not clang_tidy_diff:
        return unavailable("clang-tidy-diff.py not found")

    sources = [
        path for path in changed_files if Path(path).suffix in C_SOURCE_SUFFIXES
    ]
    if not sources:
        return skip("no C/C++ source files changed")

    build_dirs, reason = resolve_build_dirs(root)
    if reason:
        return unavailable(reason)
    if not build_dirs:
        return unavailable("no preset produced a compile DB")

    compile_dbs = []
    for name, build_dir in build_dirs:
        db = load_compile_db(root, build_dir.joinpath("compile_commands.json"))
        if db:
            compile_dbs.append((name, build_dir, db))

    if not compile_dbs:
        return unavailable("no compile DB has C/C++ source entries")

    collected = collect_coverage(root, commits, sources, compile_dbs)
    if collected is None:
        return EXIT_RUN_FAILED
    changed_lines, coverage, uncovered = collected

    if not changed_lines:
        print("--- clang-tidy: no changed lines in the C/C++ sources, nothing to check")
        return EXIT_SUCCESS

    report_coverage(coverage, changed_lines, uncovered)

    if uncovered:
        print(
            "--- clang-tidy: no preset compiles these sources, so none of their "
            "lines were checked:"
        )
        for source in sorted(uncovered):
            print(f"      {source}")

    scan = {}
    unscanned = False
    for source, results in coverage.items():
        for name, hit in results:
            if hit:
                scan.setdefault(name, []).append(source)
        if changed_lines[source] - scanned_lines(results):
            unscanned = True

    if not scan:
        return skip("no changed line is compiled by any preset, nothing to scan")

    print(
        "--- clang-tidy: scanning with "
        + ", ".join(f"{name} ({len(files)} file(s))" for name, files in scan.items())
    )

    run_results = []
    for name, build_dir, _ in compile_dbs:
        if name not in scan:
            continue
        run_results.append(
            run_clang_tidy_diff(
                root,
                name,
                build_dir,
                commits,
                scan[name],
                clang_tidy,
                clang_tidy_diff,
            )
        )

    # Precedence: a run that did not complete outranks findings, and findings
    # outrank a partial scan.  Every preset has run by now, so the log holds
    # whatever each one reported.
    if EXIT_RUN_FAILED in run_results:
        return EXIT_RUN_FAILED

    if EXIT_ERRORS_FOUND in run_results:
        return EXIT_ERRORS_FOUND

    if unscanned:
        print("--- clang-tidy: part of the change was not scanned, see above")
        return EXIT_PARTIAL_SCAN

    return EXIT_SUCCESS


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy on the changed lines of C/C++ sources, "
        "under every feature flag vector in samples/minimum/CMakePresets.json "
        "that compiles them",
        epilog="""exit codes:
  0  success: ran, found no errors, and nothing that should be scanned was left
     out.  Changes with no C/C++ source (docs, scripts, deleted files) are here.
  1  errors found: ran, and clang-tidy reported a diagnostic.
  2  partial scan: ran, and part or all of the change that should be scanned
     was not.  A changed source that no preset compiles, headers only, or part
     or all of the changed lines belong to a configuration the presets do not
     build.  Nothing reported in what was scanned.  The log says which.
  3  run failed: did not run to completion.  A preset does not configure,
     clang-tidy or clang-tidy-diff is missing, tooling or git is missing or
     fails, a changed source does not preprocess under a preset's flags (a
     missing header, for example), clang-tidy-diff fails without a diagnostic,
     or a usage error.  The log says which.

  Precedence when several outcomes occur: 3, then 1, then 2.
""",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-c", "--commits", default=None, help="Commit range in the form: a..b"
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Check staged changes in the git index for a pre-commit hook",
    )
    # A usage error exits EXIT_RUN_FAILED, not argparse's own 2: 2 is reserved
    # for a partial scan.
    # TODO: with exit_on_error off, ArgumentError bypasses this override and
    # raises a traceback.
    parser.exit_on_error = False
    parser.error = lambda message: (
        print(f"{parser.prog}: error: {message}", file=sys.stderr),
        sys.exit(EXIT_RUN_FAILED),
    )
    options = parser.parse_args()

    if not options.staged and not options.commits:
        print("Please pass --staged or --commits")
        return EXIT_RUN_FAILED

    wamr_root = Path(__file__).parent.joinpath("..").resolve()
    return process_changes(wamr_root, options.commits)


if __name__ == "__main__":
    sys.exit(main())

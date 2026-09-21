#!/usr/bin/env python3
#
# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
import argparse
import json
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

# The flag vectors of samples/minimum/CMakePresets.json that between them put
# every feature macro in both states.  all-off is not optional: it is the only
# one that compiles the #else branches.
PRESETS = [
    "all-off",
    "all-on-classic-interp",
    "all-on-fast-interp",
    "all-on-aot",
    "all-on-fast-jit",
    "all-on-llvm-jit",
]

# The three outcomes are distinct in the log and in the exit code, because a
# check that ran nothing must not look like one that ran and found nothing.
EXIT_SUCCESS = 0
EXIT_FAILURE = 1
EXIT_SKIPPED = 2

HUNK_HEADER = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
LINE_MARKER = re.compile(r'^# (\d+) "((?:[^"\\]|\\.)*)"')
DIAGNOSTIC = re.compile(r"^.+:\d+:\d+: (?:warning|error): ")


def skip(reason: str) -> int:
    print(f"--- clang-tidy: skipped ({reason})")
    return EXIT_SKIPPED


def unavailable(reason: str, commits: str) -> int:
    """Missing tooling is a skip on a developer's machine and a failure in CI.
    A green check that ran nothing is worse than a red one: the whole point of
    this script is to stop reporting silence as success."""
    if commits:
        print(f"--- clang-tidy: {reason}")
        return EXIT_FAILURE

    return skip(reason)


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
        return []

    return [line for line in output.splitlines() if line]


def get_diff(root: Path, commits: str, files: list) -> str:
    try:
        return subprocess.check_output(
            ["git", "diff", *diff_revisions(commits), "-U0", "--", *files],
            cwd=root,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        return ""


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


def generate_preset_compile_commands(root: Path, preset: str) -> bool:
    """Configure one samples/minimum preset into <repo>/build/<preset>.  No
    build step: the compile DB is a product of configure alone.  -B overrides
    the preset's binaryDir, which keeps this out of the directory a developer
    configured for themselves under samples/minimum/build/."""
    if not find_command(["cmake"]):
        return False

    result = subprocess.run(
        [
            "cmake",
            "--preset",
            preset,
            "-B",
            str(preset_build_dir(root, preset)),
            f"-DCMAKE_TOOLCHAIN_FILE={clang_toolchain(root)}",
        ],
        cwd=root.joinpath("samples", "minimum"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    if result.returncode != 0:
        print(f"--- clang-tidy: preset {preset} failed to configure, skipping it")
        return False

    return True


def resolve_build_dirs(root: Path) -> list:
    """The (preset, build directory) pairs to scan with, in report order.

    Every preset is re-configured on every run.  A compile DB left over from
    an earlier commit describes the source files and feature flags of that
    commit, and a change to either is exactly the kind of thing this check is
    supposed to notice."""
    build_dirs = []
    for preset in PRESETS:
        if not generate_preset_compile_commands(root, preset):
            continue

        build_dir = preset_build_dir(root, preset)
        if build_dir.joinpath("compile_commands.json").is_file():
            build_dirs.append((preset, build_dir))

    return build_dirs


def normalize_compile_db_file(root: Path, entry: dict) -> Path:
    file_path = Path(entry["file"])
    if not file_path.is_absolute():
        file_path = Path(entry.get("directory", root)).joinpath(file_path)

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
        if argument in ("-c", "-o", "-MF", "-MT", "-MQ"):
            index += 2 if argument != "-c" else 1
            continue
        if argument in ("-MD", "-MMD"):
            index += 1
            continue

        stripped.append(argument)
        index += 1

    # -C keeps comments, so a changed comment line inside an active branch
    # counts as compiled instead of looking like dead code.
    return stripped + ["-E", "-C"]


def active_lines(entry: dict, target: Path) -> set:
    """The lines of `target` that survive preprocessing under this entry's
    flags.  A line inside an #if branch the flags turn off never appears, which
    is exactly the question "would clang-tidy even see my change here"."""
    argv = preprocess_argv(entry)
    if not argv:
        return set()

    try:
        result = subprocess.run(
            argv,
            cwd=entry.get("directory", "."),
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            universal_newlines=True,
        )
    except OSError:
        return set()

    if result.returncode != 0:
        return set()

    directory = Path(entry.get("directory", "."))
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


def report_coverage(coverage: dict, changed_lines: dict, in_no_db: set) -> None:
    print("--- clang-tidy: which presets compile the changed lines")
    for source in sorted(changed_lines):
        total = len(changed_lines[source])
        note = " -- in no compile DB at all" if source in in_no_db else ""
        print(f"      {source} ({total} changed line(s)){note}")
        for name, hit in coverage[source]:
            verdict = f"{len(hit)}/{total}" if hit else "not compiled"
            print(f"        {name:<24} {verdict}")

        scanned = set()
        for _, hit in coverage[source]:
            scanned |= hit

        blind = changed_lines[source] - scanned
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
) -> bool:
    diff = get_diff(root, commits, files)
    if not diff:
        return True

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
    return result.returncode == 0 and not found


def process_changes(root: Path, commits: str) -> int:
    changed_files = [
        path
        for path in get_changed_paths(root, commits)
        if Path(path).suffix in C_SUFFIXES
    ]
    if not changed_files:
        return skip("no C/C++ files changed")

    clang_tidy = find_command(CLANG_TIDY_CANDIDATES)
    if not clang_tidy:
        return unavailable("clang-tidy not found", commits)

    clang_tidy_diff = find_command(CLANG_TIDY_DIFF_CANDIDATES)
    if not clang_tidy_diff:
        return unavailable("clang-tidy-diff.py not found", commits)

    for changed_file in changed_files:
        if Path(changed_file).suffix in C_HEADER_SUFFIXES:
            print(
                f"--- clang-tidy: skipped {changed_file} "
                "(headers are not compile DB translation units)"
            )

    sources = [
        path for path in changed_files if Path(path).suffix in C_SOURCE_SUFFIXES
    ]
    if not sources:
        return skip("no C/C++ source files changed")

    build_dirs = resolve_build_dirs(root)
    if not build_dirs:
        return unavailable("no preset produced a compile DB", commits)

    compile_dbs = []
    for name, build_dir in build_dirs:
        db = load_compile_db(root, build_dir.joinpath("compile_commands.json"))
        if db:
            compile_dbs.append((name, build_dir, db))

    if not compile_dbs:
        return unavailable("no compile DB has C/C++ source entries", commits)

    # Two questions per changed source, in this order: is the file in this DB at
    # all (file level), and if it is, do any of the changed lines survive the
    # preprocessor under those flags (line level)?  A file that passes the first
    # and fails the second produces no clang-tidy output, which is
    # indistinguishable from a clean result unless we say so here.
    changed_lines = {}
    coverage = {}
    in_no_db = set()
    for source in sources:
        path = root.joinpath(source).resolve()
        lines = get_changed_lines(get_diff(root, commits, [source]))
        if not lines:
            continue

        changed_lines[source] = lines
        if not any(path in db for _, _, db in compile_dbs):
            # A different remedy from "the branch is off": this one needs
            # another platform, another target or an optional component.
            in_no_db.add(source)

        coverage[source] = []
        for name, _, db in compile_dbs:
            hit = set()
            for entry in db.get(path, []):
                hit |= active_lines(entry, path) & lines
                if hit == lines:
                    break

            coverage[source].append((name, hit))

    if not changed_lines:
        return skip("no changed lines in the C/C++ sources")

    report_coverage(coverage, changed_lines, in_no_db)

    scan = {}
    for source, results in coverage.items():
        for name, hit in results:
            if hit:
                scan.setdefault(name, []).append(source)

    if not scan:
        return skip("no changed line is compiled by any preset, nothing to scan")

    print(
        "--- clang-tidy: scanning with "
        + ", ".join(f"{name} ({len(files)} file(s))" for name, files in scan.items())
    )

    passed = True
    for name, build_dir, _ in compile_dbs:
        if name not in scan:
            continue
        if not run_clang_tidy_diff(
            root,
            name,
            build_dir,
            commits,
            scan[name],
            clang_tidy,
            clang_tidy_diff,
        ):
            passed = False

    return EXIT_SUCCESS if passed else EXIT_FAILURE


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy on the changed lines of C/C++ sources, "
        "under every feature flag vector in samples/minimum/CMakePresets.json "
        "that compiles them",
        epilog="""exit codes:
  0  scanned, clang-tidy reported nothing
  1  clang-tidy reported something, or the check could not be trusted to run
     (with --commits, missing tooling and an unusable compile DB land here:
     in CI a green check that ran nothing is worse than a red one)
  2  skipped -- nothing was scanned. No C/C++ source changed, or no preset
     compiles the changed lines, or, with --staged, the tooling is missing.
     The log says which; treat it as neither pass nor fail.
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
    # argparse exits 2 on a usage error, which is this script's "skipped".
    parser.exit_on_error = False
    parser.error = lambda message: (
        print(f"{parser.prog}: error: {message}", file=sys.stderr),
        sys.exit(EXIT_FAILURE),
    )
    options = parser.parse_args()

    if not options.staged and not options.commits:
        print("Please pass --staged or --commits")
        return EXIT_FAILURE

    wamr_root = Path(__file__).parent.joinpath("..").resolve()
    return process_changes(wamr_root, options.commits)


if __name__ == "__main__":
    sys.exit(main())

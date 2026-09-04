#!/usr/bin/env python3
#
# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
import argparse
import json
from pathlib import Path
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


def skip(reason: str) -> bool:
    print(f"--- clang-tidy: skipped ({reason})")
    return True


def find_command(candidates: list) -> str:
    for candidate in candidates:
        command = shutil.which(candidate)
        if command:
            return command

    return ""


def get_staged_paths(root: Path) -> list:
    try:
        output = subprocess.check_output(
            ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
            cwd=root,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        return []

    return [line for line in output.splitlines() if line]


def get_host_platform() -> str:
    if sys.platform.startswith("linux"):
        return "linux"
    if sys.platform == "darwin":
        return "darwin"
    if sys.platform.startswith(("win32", "cygwin", "msys")):
        return "windows"

    return ""


def generate_compile_commands(root: Path, build_dir: Path) -> bool:
    if not find_command(["cmake"]):
        skip("cmake not found")
        return False

    source_dir = build_dir.parent
    print(f"--- clang-tidy: generating compile DB in {build_dir}")
    result = subprocess.run(
        [
            "cmake",
            "-S",
            str(source_dir),
            "-B",
            str(build_dir),
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        ],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True,
    )
    if result.returncode != 0:
        print(result.stdout)
        skip("failed to generate compile_commands.json")
        return False

    return True


def normalize_compile_db_file(root: Path, entry: dict) -> Path:
    file_path = Path(entry["file"])
    if not file_path.is_absolute():
        file_path = Path(entry.get("directory", root)).joinpath(file_path)

    return file_path.resolve()


def load_compile_db_sources(root: Path, compile_db: Path) -> set:
    try:
        entries = json.loads(compile_db.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as ex:
        print(f"--- clang-tidy: failed to read {compile_db}: {ex}")
        return set()

    sources = set()
    for entry in entries:
        if "file" not in entry:
            continue

        file_path = normalize_compile_db_file(root, entry)
        if file_path.suffix in C_SOURCE_SUFFIXES:
            sources.add(file_path)

    return sources


def split_covered_files(root: Path, staged_files: list, compile_db_sources: set) -> tuple:
    covered_sources = []
    skipped_sources = []
    headers = []

    for staged_file in staged_files:
        path = root.joinpath(staged_file).resolve()
        suffix = path.suffix
        if suffix in C_SOURCE_SUFFIXES:
            if path in compile_db_sources:
                covered_sources.append(staged_file)
            else:
                skipped_sources.append(staged_file)
        elif suffix in C_HEADER_SUFFIXES:
            headers.append(staged_file)

    return covered_sources, skipped_sources, headers


def run_clang_tidy_diff(
    root: Path, build_dir: Path, files: list, clang_tidy: str, clang_tidy_diff: str
) -> bool:
    try:
        diff = subprocess.check_output(
            ["git", "diff", "--cached", "-U0", "--", *files],
            cwd=root,
            universal_newlines=True,
        )
    except subprocess.CalledProcessError:
        print("--- clang-tidy: failed to read staged diff")
        return False

    if not diff:
        return skip("no covered staged C/C++ source changes")

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
    if result.stdout:
        print(result.stdout, end="")

    return result.returncode == 0


def process_staged_changes(root: Path) -> bool:
    staged_files = [
        path for path in get_staged_paths(root) if Path(path).suffix in C_SUFFIXES
    ]
    if not staged_files:
        return skip("no staged C/C++ files")

    clang_tidy = find_command(CLANG_TIDY_CANDIDATES)
    if not clang_tidy:
        return skip("clang-tidy not found")

    clang_tidy_diff = find_command(CLANG_TIDY_DIFF_CANDIDATES)
    if not clang_tidy_diff:
        return skip("clang-tidy-diff.py not found")

    host_platform = get_host_platform()
    if not host_platform:
        return skip(f"unsupported host platform: {sys.platform}")

    platform_dir = root.joinpath("product-mini", "platforms", host_platform)
    if not platform_dir.is_dir():
        return skip(f"{platform_dir.relative_to(root)} not found")

    build_dir = platform_dir.joinpath("build-clang-tidy")
    if not generate_compile_commands(root, build_dir):
        return True

    compile_db = build_dir.joinpath("compile_commands.json")
    if not compile_db.is_file():
        return skip(f"{compile_db.relative_to(root)} not found")

    compile_db_sources = load_compile_db_sources(root, compile_db)
    if not compile_db_sources:
        return skip("compile DB has no C/C++ source entries")

    covered_sources, skipped_sources, headers = split_covered_files(
        root, staged_files, compile_db_sources
    )
    for skipped_source in skipped_sources:
        print(f"--- clang-tidy: skipped {skipped_source} (not in compile DB)")

    for header in headers:
        print(
            f"--- clang-tidy: skipped {header} "
            "(headers are not compile DB translation units)"
        )

    if not covered_sources:
        return skip("no staged C/C++ source files covered by compile DB")

    return run_clang_tidy_diff(
        root, build_dir, covered_sources, clang_tidy, clang_tidy_diff
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy-diff on staged C/C++ source changes"
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Check staged changes in the git index for a pre-commit hook",
    )
    options = parser.parse_args()

    if not options.staged:
        print("Please pass --staged")
        return 1

    wamr_root = Path(__file__).parent.joinpath("..").resolve()
    return 0 if process_staged_changes(wamr_root) else 1


if __name__ == "__main__":
    sys.exit(main())

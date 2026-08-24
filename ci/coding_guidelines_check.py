#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
import argparse
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

CLANG_FORMAT_CMD = "clang-format-21"
GIT_CLANG_FORMAT_CMD = "git-clang-format-21"

# glob style patterns
EXCLUDE_PATHS = [
    "**/.git/*",
    "**/.github/*",
    "**/.vscode/*",
    "**/build/*",
    "**/build-scripts/*",
    "**/ci/*",
    "**/core/deps/*",
    "**/doc/*",
    "**/samples/wasm-c-api/src/*.*",
    "**/samples/workload/*",
    "**/test-tools/wasi-sdk/*",
    "**/tests/wamr-test-suites/workspace/*",
]

C_SUFFIXES = [".c", ".cc", ".cpp", ".h"]
INVALID_DIR_NAME_SEGMENT = r"([a-zA-Z0-9]+\_[a-zA-Z0-9]+)"
INVALID_FILE_NAME_SEGMENT = r"([a-zA-Z0-9]+\-[a-zA-Z0-9]+)"

# License header requirements. Only the SPDX identifier and the presence of a
# copyright line are enforced: the year and the copyright holder vary per
# contributor and checking them would only produce noise.
SPDX_ID = "Apache-2.0 WITH LLVM-exception"
LICENSE_HEADER_SCAN_LINES = 10
LICENSED_SUFFIXES = C_SUFFIXES + [".py", ".sh", ".cmake"]
LICENSED_FILE_NAMES = ["CMakeLists.txt"]


def locate_command(command: str) -> bool:
    if not shutil.which(command):
        print(f"Command '{command}' not found")
        return False

    return True


def is_excluded(path: str) -> bool:
    path = Path(path).resolve()
    for exclude_path in EXCLUDE_PATHS:
        if path.match(exclude_path):
            return True
    return False


def pre_flight_check(root: Path) -> bool:
    def check_aspell(root):
        return True

    def check_clang_format(root: Path) -> bool:
        if not locate_command(CLANG_FORMAT_CMD):
            return False

        # Quick syntax check for .clang-format
        try:
            subprocess.check_output(
                shlex.split(f"{CLANG_FORMAT_CMD} --dump-config"), cwd=root
            )
        except subprocess.CalledProcessError:
            print(f"Might have a typo in .clang-format")
            return False
        return True

    def check_git_clang_format() -> bool:
        return locate_command(GIT_CLANG_FORMAT_CMD)

    return check_aspell(root) and check_clang_format(root) and check_git_clang_format()


def run_clang_format(file_path: Path, root: Path) -> bool:
    try:
        subprocess.check_call(
            shlex.split(
                f"{CLANG_FORMAT_CMD} --style=file --Werror --dry-run {file_path}"
            ),
            cwd=root,
        )
        return True
    except subprocess.CalledProcessError:
        print(f"{file_path} failed the check of {CLANG_FORMAT_CMD}")
        return False


def run_clang_format_diff(root: Path, commits: str) -> bool:
    """
    Use `clang-format-21` or `git-clang-format-21` to check code format of
    the PR, with a commit range specified. It is required to format the
    code before committing the PR, or it might fail to pass the CI check:

    1. Install clang-format-21

    You can download the package from
    https://github.com/llvm/llvm-project/releases
    and install it.

    For Debian/Ubuntu, we can probably use
    `sudo apt-get install clang-format-21`.

    Homebrew has it as a part of llvm@21.
    ```shell
    brew install llvm@21
    /usr/local/opt/llvm@21/bin/clang-format
    ```

    2. Format the C/C++ source file
    ``` shell
    cd path/to/wamr/root
    clang-format-21 --style file -i path/to/file
    ```

    The code wrapped by `/* clang-format off */` and `/* clang-format on */`
    will not be formatted, you shall use them when the formatted code is not
    readable or friendly:

    ``` cc
    /* clang-format off */
    code snippets
    /* clang-format on */
    ```

    """
    try:
        before, after = commits.split("..")
        after = after if after else "HEAD"
        COMMAND = (
            f"{GIT_CLANG_FORMAT_CMD} -v --binary "
            f"{shutil.which(CLANG_FORMAT_CMD)} --style file "
            f"--extensions c,cpp,h --diff {before} {after}"
        )

        p = subprocess.Popen(
            shlex.split(COMMAND),
            stdout=subprocess.PIPE,
            stderr=None,
            stdin=None,
            universal_newlines=True,
        )

        stdout, _ = p.communicate()
        if not stdout.startswith("diff --git"):
            return True

        diff_content = stdout.split("\n")
        found = False
        for summary in [x for x in diff_content if x.startswith("diff --git")]:
            # b/path/to/file -> path/to/file
            with_invalid_format = re.split(r"\s+", summary)[-1][2:]
            if not is_excluded(with_invalid_format):
                print(f"--- {with_invalid_format} failed on code style checking.")
                found = True
        else:
            return not found
    except subprocess.CalledProcessError:
        return False


def run_aspell(file_path: Path, root: Path) -> bool:
    return True


def check_dir_name(path: Path, root: Path) -> bool:
    m = re.search(INVALID_DIR_NAME_SEGMENT, str(path.relative_to(root).parent))
    if m:
        print(f"--- found a character '_' in {m.groups()} in {path}")

    return not m


def check_file_name(path: Path) -> bool:
    """
    file names should not contain any character '-'

    but some names are well known and use '-' as the separator, e.g.:
    - docker-compose
    - package-lock
    - vite-env.d
    - .clang-tidy (standard config file for Clang-Tidy)
    """
    if path.stem in [
        "docker-compose",
        "package-lock",
        "vite-env.d",
        "osv-scanner",
        ".clang-tidy",
    ]:
        return True

    m = re.search(INVALID_FILE_NAME_SEGMENT, path.stem)
    if m:
        print(f"--- found a character '-' in {m.groups()} in {path}")

    return not m


def needs_license_header(path: Path) -> bool:
    return path.suffix in LICENSED_SUFFIXES or path.name in LICENSED_FILE_NAMES


def has_license_header(path: Path) -> bool:
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            head = "".join(next(f, "") for _ in range(LICENSE_HEADER_SCAN_LINES))
    except OSError:
        return False

    return f"SPDX-License-Identifier: {SPDX_ID}" in head and "Copyright (C)" in head


def parse_commits_range(root: Path, commits: str) -> list:
    GIT_LOG_CMD = f"git log --pretty='%H' {commits}"
    try:
        ret = subprocess.check_output(
            shlex.split(GIT_LOG_CMD), cwd=root, universal_newlines=True
        )
        return [x for x in ret.split("\n") if x]
    except subprocess.CalledProcessError:
        print(f"can not parse any commit from the range {commits}")
        return []


def analysis_new_item_name(root: Path, commit: str) -> bool:
    """
    For any file name in the repo, it is required to use '_' to replace '-'.

    For any directory name in the repo,  it is required to use '-' to replace '_'.
    """
    GIT_DIFF_CMD = f"git diff --name-status --diff-filter=A {commit}"
    try:
        invalid_items = True
        output = subprocess.check_output(
            shlex.split(GIT_DIFF_CMD), cwd=root, universal_newlines=True
        )
        if not output:
            return True

        NEW_FILE_PATTERN = r"^A\s+(\S+)"
        for line in output.split("\n"):

            if not line:
                continue

            match = re.match(NEW_FILE_PATTERN, line)
            if not match:
                continue

            new_item = match.group(1)
            new_item = Path(new_item).resolve()

            if new_item.is_file():
                if not check_file_name(new_item):
                    invalid_items = False
                    continue

                new_item = new_item.parent

            if not check_dir_name(new_item, root):
                invalid_items = False
                continue
        else:
            return invalid_items

    except subprocess.CalledProcessError:
        return False


def analysis_new_item_license(root: Path, commits: str) -> bool:
    """
    Every new source file needs a license header within its first 10 lines:

        /*
         * Copyright (C) 2019 Intel Corporation.  All rights reserved.
         * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
         */

    Use the comment syntax of the language, '#' for .py, .sh and .cmake files.

    Only files added by this PR are checked. The copyright year and holder are
    not enforced.
    """
    GIT_DIFF_CMD = f"git diff --name-only --diff-filter=A {commits}"
    try:
        output = subprocess.check_output(
            shlex.split(GIT_DIFF_CMD), cwd=root, universal_newlines=True
        )
    except subprocess.CalledProcessError:
        return False

    missing = []
    for name in output.split("\n"):
        if not name:
            continue

        new_file = root.joinpath(name)
        if not new_file.is_file() or is_excluded(new_file):
            continue

        if needs_license_header(new_file) and not has_license_header(new_file):
            missing.append(name)

    for name in missing:
        print(f"--- missing license header in {name}")

    return not missing


def process_entire_pr(root: Path, commits: str) -> bool:
    if not commits:
        print("Please provide a commits range")
        return False

    commit_list = parse_commits_range(root, commits)
    if not commit_list:
        print(f"Quit since there is no commit to check with")
        return True

    print(f"there are {len(commit_list)} commits in the PR")

    found = False
    if not analysis_new_item_name(root, commits):
        print(f"{analysis_new_item_name.__doc__}")
        found = True

    if not analysis_new_item_license(root, commits):
        print(f"{analysis_new_item_license.__doc__}")
        found = True

    if not run_clang_format_diff(root, commits):
        print(f"{run_clang_format_diff.__doc__}")
        found = True

    return not found


# TODO: ci/pre_commit_hook_sample duplicates the clang-format rule and does not
# check license headers at all. Make the hook call this script instead, so the
# rules live in one place. See docs/2026-08-07-pr-gate-design-and-build-verification.md
# section 5.4.
def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check if change meets all coding guideline requirements"
    )
    parser.add_argument(
        "-c", "--commits", default=None, help="Commit range in the form: a..b"
    )
    options = parser.parse_args()

    wamr_root = Path(__file__).parent.joinpath("..").resolve()

    if not pre_flight_check(wamr_root):
        return False

    return process_entire_pr(wamr_root, options.commits)


# run with python3 -m unitest ci/coding_guidelines_check.py
class TestCheck(unittest.TestCase):
    def test_check_dir_name_failed(self):
        root = Path("/root/Workspace/")
        new_file_path = root.joinpath("core/shared/platform/esp_idf/espid_memmap.c")
        self.assertFalse(check_dir_name(new_file_path, root))

    def test_check_dir_name_pass(self):
        root = Path("/root/Workspace/")
        new_file_path = root.joinpath("core/shared/platform/esp-idf/espid_memmap.c")
        self.assertTrue(check_dir_name(new_file_path, root))

    def test_check_file_name_failed(self):
        new_file_path = Path(
            "/root/Workspace/core/shared/platform/esp-idf/espid-memmap.c"
        )
        self.assertFalse(check_file_name(new_file_path))

    def test_check_file_name_pass(self):
        new_file_path = Path(
            "/root/Workspace/core/shared/platform/esp-idf/espid_memmap.c"
        )
        self.assertTrue(check_file_name(new_file_path))

    def test_needs_license_header(self):
        self.assertTrue(needs_license_header(Path("core/iwasm/common/wasm_c_api.c")))
        self.assertTrue(needs_license_header(Path("product-mini/CMakeLists.txt")))
        self.assertFalse(needs_license_header(Path("README.md")))

    def test_has_license_header_pass(self):
        wamr_root = Path(__file__).parent.joinpath("..").resolve()
        self.assertTrue(
            has_license_header(wamr_root.joinpath("core/iwasm/include/wasm_export.h"))
        )

    def test_has_license_header_failed(self):
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".c", delete=False
        ) as probe_file:
            probe_file.write("int main(void) { return 0; }\n")
            probe = Path(probe_file.name)

        try:
            self.assertFalse(has_license_header(probe))
        finally:
            probe.unlink()


if __name__ == "__main__":
    sys.exit(0 if main() else 1)

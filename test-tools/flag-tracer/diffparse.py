# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Minimal unified-diff parser."""

import re

_FILE = re.compile(r"^\+\+\+ (?:b/)?(.+)$")
_OLD = re.compile(r"^--- (?:a/)?(.+)$")
_HUNK = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")

C_SUFFIXES = (".c", ".h", ".cc", ".cpp", ".hpp", ".cxx", ".inl")
CMAKE_NAMES = ("CMakeLists.txt",)
CMAKE_SUFFIXES = (".cmake",)


class FileDiff:
    def __init__(self, path, old_path):
        self.path = path
        self.old_path = old_path
        self.new_lines = []   # added/changed line numbers in the new file
        self.old_lines = []   # deleted line numbers in the old file
        self.changed = []     # raw +/- lines, for cmake diffs

    @property
    def kind(self):
        if self.path.endswith(C_SUFFIXES):
            return "c"
        if self.path.endswith(CMAKE_SUFFIXES) or \
                self.path.rsplit("/", 1)[-1] in CMAKE_NAMES:
            return "cmake"
        return "other"

    def __repr__(self):
        return "<FileDiff %s +%d -%d>" % (
            self.path, len(self.new_lines), len(self.old_lines))


def parse(text):
    """Return [FileDiff] for a unified diff."""
    files, cur, old_path = [], None, None
    new_no = old_no = 0
    for line in text.splitlines():
        m = _OLD.match(line)
        if m:
            old_path = None if m.group(1) == "/dev/null" else m.group(1)
            continue
        m = _FILE.match(line)
        if m:
            path = m.group(1).split("\t")[0]
            cur = None if path == "/dev/null" else FileDiff(path, old_path)
            if cur:
                files.append(cur)
            continue
        m = _HUNK.match(line)
        if m:
            old_no, new_no = int(m.group(1)), int(m.group(3))
            continue
        if cur is None or not line:
            continue
        if line[0] == "+":
            cur.changed.append(line[1:])
            cur.new_lines.append(new_no)
            new_no += 1
        elif line[0] == "-":
            cur.changed.append(line[1:])
            cur.old_lines.append(old_no)
            old_no += 1
        elif line[0] == " ":
            new_no += 1
            old_no += 1
    return files

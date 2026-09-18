#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Fallback: derive WASM_ENABLE_* -> WAMR_BUILD_* from the cmake config.

Walks build-scripts/runtime_lib.cmake and follows conditional include()s, so
the feature cmake files (iwasm_gc.cmake, lib_pthread.cmake, ...) are covered
by the same mechanism as config_common.cmake.

Only used when known_map.KNOWN misses a macro.  Results are cached in
.cache/flag-map.json, keyed by the hash of every file actually parsed.

Also yields directory -> flags and file -> flags tables (which feature dir is
pulled in, and which individual source is picked, under which switch), used for
file-level attribution.
"""

import hashlib
import json
import os
import re

ROOT_CMAKE = "build-scripts/runtime_lib.cmake"

_CMD = re.compile(r"^\s*([A-Za-z_][A-Za-z_0-9]*)\s*\(")
_VAR = re.compile(r"\b(WAMR_(?:BUILD|DISABLE)_[A-Z0-9_]+)\b")
_DEF = re.compile(r"-D(WASM_(?:ENABLE|DISABLE)_[A-Z0-9_]+)(?:=([0-9]+))?")
_SET = re.compile(r"^\s*([A-Za-z_][A-Za-z_0-9]*)\s+(\S+)")
# WAMR_BUILD_TARGET / WAMR_BUILD_PLATFORM select a target, not a feature; they
# would otherwise attach themselves to every macro under a platform branch.
_NOT_A_FEATURE = {"WAMR_BUILD_TARGET", "WAMR_BUILD_PLATFORM"}
_SOURCE_SUFFIXES = (".c", ".cpp", ".cc", ".S")


def _commands(text):
    """Yield (name, args) for every cmake command, joining multi-line calls."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = _CMD.match(lines[i])
        if not m:
            i += 1
            continue
        buf = lines[i][m.end():]
        depth = 1 + buf.count("(") - buf.count(")")
        while depth > 0 and i + 1 < len(lines):
            i += 1
            buf += " " + lines[i]
            depth = 1 + buf.count("(") - buf.count(")")
        buf = buf.rstrip()
        if buf.endswith(")"):
            buf = buf[:-1]  # drop the command's own closing paren
        yield m.group(1).lower(), buf
        i += 1


def _cond_flags(args):
    return {v for v in _VAR.findall(args) if v not in _NOT_A_FEATURE}


class _Walker:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.macros = {}   # macro -> set(flags)
        self.dirs = {}     # repo-relative dir -> set(flags)
        self.files = {}    # repo-relative source file -> set(flags)
        self.hash = hashlib.sha256()
        self.seen = set()

    def _expand(self, path, vars_):
        for _ in range(4):  # nested ${} references
            new = re.sub(r"\$\{([A-Za-z_][A-Za-z_0-9]*)\}",
                         lambda m: vars_.get(m.group(1), ""), path)
            if new == path:
                break
            path = new
        return path.strip().strip('"')

    def walk(self, rel, vars_, inherited):
        path = os.path.join(self.root, rel)
        if not os.path.exists(path) or path in self.seen:
            return
        self.seen.add(path)
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        self.hash.update(rel.encode())
        self.hash.update(text.encode())

        vars_ = dict(vars_)
        vars_["CMAKE_CURRENT_LIST_DIR"] = os.path.dirname(path)
        stack = []  # [flags, in_else]

        def active():
            flags = set(inherited)
            for f_, in_else in stack:
                if not in_else:
                    flags |= f_
            return flags

        for name, args in _commands(text):
            if name == "if":
                stack.append([_cond_flags(args), False])
            elif name == "elseif":
                if stack:
                    stack[-1] = [_cond_flags(args), False]
            elif name == "else":
                if stack:
                    stack[-1][1] = True
            elif name == "endif":
                if stack:
                    stack.pop()
            elif name == "set":
                m = _SET.match(args)
                if m and "${" not in m.group(1):
                    value = self._expand(m.group(2), vars_)
                    vars_[m.group(1)] = value
                    # `if (WAMR_BUILD_MINI_LOADER) set (LOADER wasm_mini_loader.c)`
                    # -- the switch decides which source is compiled at all
                    if value.endswith(_SOURCE_SUFFIXES):
                        self._note_source(value, os.path.dirname(path), active())
            elif name == "add_definitions":
                for macro, value in _DEF.findall(args):
                    if value == "0":
                        continue  # the "feature off" branch carries no info
                    flags = active()
                    if flags:
                        self.macros.setdefault(macro, set()).update(flags)
            elif name == "include":
                parts = args.split()
                inc = self._expand(parts[0] if parts else "", vars_)
                if not inc.endswith(".cmake"):
                    continue
                inc = os.path.normpath(inc)
                if not os.path.isabs(inc):
                    inc = os.path.normpath(os.path.join(os.path.dirname(path), inc))
                rel_inc = os.path.relpath(inc, self.root)
                if rel_inc.startswith(".."):
                    continue
                flags = active()
                if flags:
                    inc_dir = os.path.dirname(rel_inc)
                    self.dirs.setdefault(inc_dir, set()).update(flags)
                    # wasi-nn keeps its cmake in a cmake/ subdir; the sources it
                    # guards live one level up
                    if os.path.basename(inc_dir) == "cmake":
                        self.dirs.setdefault(os.path.dirname(inc_dir),
                                             set()).update(flags)
                self.walk(rel_inc, vars_, flags)

    def _note_source(self, name, cmake_dir, flags):
        if not flags:
            return
        candidate = os.path.join(cmake_dir, name)
        if not os.path.exists(candidate):
            return
        rel = os.path.relpath(candidate, self.root)
        self.files.setdefault(rel, set()).update(flags)


def build(root):
    """Parse the cmake tree; return (macro_map, dir_map, file_map, digest)."""
    w = _Walker(root)
    w.walk(ROOT_CMAKE, {"WAMR_ROOT_DIR": os.path.abspath(root)}, set())
    return ({k: sorted(v) for k, v in w.macros.items()},
            {k: sorted(v) for k, v in w.dirs.items()},
            {k: sorted(v) for k, v in w.files.items()},
            w.hash.hexdigest()[:16])


def load(root, cache_dir=None):
    """Cached variant of build(); returns (macro_map, dir_map, file_map)."""
    cache_dir = cache_dir or os.path.join(root, ".cache")
    cache = os.path.join(cache_dir, "flag-map.json")
    macros, dirs, files, digest = build(root)
    try:
        with open(cache, encoding="utf-8") as f:
            blob = json.load(f)
        if blob.get("digest") == digest:
            return blob["macros"], blob["dirs"], blob["files"]
    except (OSError, ValueError, KeyError):
        pass
    try:
        os.makedirs(cache_dir, exist_ok=True)
        with open(cache, "w", encoding="utf-8") as f:
            json.dump({"digest": digest, "macros": macros, "dirs": dirs,
                       "files": files}, f, indent=1, sort_keys=True)
    except OSError:
        pass
    return macros, dirs, files


if __name__ == "__main__":
    import sys

    here = os.path.dirname(os.path.abspath(__file__))
    macros, dirs, files, _ = build(sys.argv[1] if len(sys.argv) > 1
                                   else os.path.join(here, "..", ".."))
    print("KNOWN = {")
    for macro in sorted(macros):
        print('    "%s": (%s),' % (
            macro, "".join('"%s", ' % f for f in macros[macro])))
    print("}\n")
    print("KNOWN_DIRS = {")
    for d in sorted(dirs):
        print('    "%s": (%s),' % (d, "".join('"%s", ' % f for f in dirs[d])))
    print("}\n")
    print("KNOWN_FILES = {")
    for f_ in sorted(files):
        print('    "%s": (%s),' % (f_, "".join('"%s", ' % g for g in files[f_])))
    print("}")

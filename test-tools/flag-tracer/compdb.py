# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""cmake configure (no build) -> compile_commands.json.

Answers, for one switch configuration:
  * is this file compiled at all?
  * what is the exact command line used to compile it?

Configure results are cached per switch set, so the positive and negative
rounds of a verification pay for one configure each at most.
"""

import hashlib
import json
import os
import shlex
import subprocess

DEFAULT_SOURCE = "product-mini/platforms/linux"


class ConfigureError(Exception):
    pass


class CompDB(object):
    def __init__(self, root, flags_on=(), flags_off=(), source=None,
                 cache_root=None):
        self.root = root
        self.source = source or DEFAULT_SOURCE
        self.args = (tuple("-D%s=1" % f for f in sorted(flags_on))
                     + tuple("-D%s=0" % f for f in sorted(flags_off)))
        self.build_dir = os.path.join(
            cache_root or os.path.join(root, ".cache", "flag-tracer"),
            "cfg-" + _slug(self.args))
        self._db = None

    def configure(self):
        stamp = os.path.join(self.build_dir, "compile_commands.json")
        if os.path.exists(stamp):
            return
        os.makedirs(self.build_dir, exist_ok=True)
        cmd = ["cmake", "-B", self.build_dir,
               "-S", os.path.join(self.root, self.source),
               "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"] + list(self.args)
        p = subprocess.run(cmd, capture_output=True, text=True,
                           errors="replace")
        if p.returncode != 0 or not os.path.exists(stamp):
            raise ConfigureError("cmake %s failed:\n%s"
                                 % (" ".join(self.args), p.stderr.strip()[-2000:]))

    @property
    def db(self):
        if self._db is None:
            self.configure()
            with open(os.path.join(self.build_dir, "compile_commands.json"),
                      encoding="utf-8") as f:
                entries = json.load(f)
            self._db = {}
            for e in entries:
                path = os.path.realpath(
                    os.path.join(e.get("directory", "."), e["file"]))
                self._db[path] = e
        return self._db

    def entry(self, rel_path):
        return self.db.get(os.path.realpath(os.path.join(self.root, rel_path)))

    def compiles(self, rel_path):
        return self.entry(rel_path) is not None

    def macros(self, rel_path):
        """The -DWASM_* actually passed to this file in this configuration."""
        e = self.entry(rel_path)
        if e is None:
            return {}
        out = {}
        for tok in _argv(e):
            if tok.startswith("-DWASM_"):
                name, _, value = tok[2:].partition("=")
                out[name] = value or "1"
        return out

    def preprocess_argv(self, rel_path, replacement_source):
        """Command line that preprocesses `replacement_source` in this file's
        place: same -D/-I, output discarded, no code generation."""
        e = self.entry(rel_path)
        if e is None:
            return None
        argv, out, skip = _argv(e), [], False
        src_dir = os.path.dirname(
            os.path.realpath(os.path.join(self.root, rel_path)))
        for tok in argv:
            if skip:
                skip = False
                continue
            if tok in ("-o", "-c"):
                skip = tok == "-o"
                continue
            if os.path.realpath(
                    os.path.join(e.get("directory", "."), tok)) == \
                    os.path.realpath(os.path.join(self.root, rel_path)):
                continue
            out.append(tok)
        # the probe copy lives in a temp dir, so quoted includes need the
        # original directory back on the search path
        out += ["-I", src_dir, "-E", "-o", os.devnull, replacement_source]
        return out, e.get("directory", self.root)


def _argv(entry):
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry.get("command", ""))


def _slug(args):
    return hashlib.sha1(" ".join(args).encode()).hexdigest()[:12]

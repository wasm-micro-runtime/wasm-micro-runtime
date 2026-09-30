# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Turn a diff into the set of build-switch combinations it belongs to.

L1  preprocessor scope of every changed line   -> confidence high
L2  feature directory the file lives in        -> confidence medium
    (static table from cmake; --verify replaces it with compile_commands.json)
cmake diffs are read directly for WAMR_BUILD_* / WASM_ENABLE_* mentions.
"""

import os
import re

import cmake_map
import cpp_scope
import diffparse
import inputs
import known_map

_WAMR = re.compile(r"\b(WAMR_(?:BUILD|DISABLE)_[A-Z0-9_]+)\b")
_WASM = re.compile(r"\b(WASM_(?:ENABLE|DISABLE)_[A-Z0-9_]+)\b")


def settings_for(macro, on, mapped):
    """Macro polarity -> ["WAMR_BUILD_X=1", ...].

    Enabling a macro means every switch it depends on must be on (a
    conjunction).  Disabling it only needs the most specific one off -- the
    others are prerequisites, not the feature itself.
    """
    if on:
        return ["%s=1" % f for f in mapped]
    exact = [f for f in mapped
             if f.split("_", 2)[-1] == macro.split("_", 2)[-1]]
    return ["%s=0" % f for f in (exact or mapped)]


def _merge(base, flags):
    """Line guards win over file prerequisites when they disagree."""
    named = {s.partition("=")[0] for s in flags}
    return set(flags) | {b for b in base if b.partition("=")[0] not in named}


def targets_for(path):
    """Which build a change to `path` shows up in."""
    if path.startswith(known_map.WAMRC_ONLY_DIRS):
        return ("wamrc",)
    if path.startswith(known_map.WAMRC_DIRS):
        return ("wamrc", "iwasm")
    return ("iwasm",)


class Config(object):
    """One conjunction of switch settings that must hold together."""

    def __init__(self, flags, confidence, source):
        self.flags = tuple(sorted(flags))
        self.confidence = confidence
        self.source = source
        self.targets = set()
        self.evidence = []
        self._seen = set()

    def add_evidence(self, ev):
        key = (ev.get("file"), ev.get("line"), ev.get("flag"))
        if key not in self._seen:
            self._seen.add(key)
            self.evidence.append(ev)

    def as_dict(self):
        return {"flags": list(self.flags), "confidence": self.confidence,
                "source": self.source,
                "targets": sorted(self.targets),
                "evidence": self.evidence}


class Result(object):
    def __init__(self):
        self.configs = {}          # flags -> Config
        self.unmapped_macros = set()
        self.unattributed = []
        self.skipped = []
        self.warnings = []

    def add(self, flags, confidence, source, evidence):
        if not flags:
            return
        key = tuple(sorted(flags))
        cfg = self.configs.get(key)
        if cfg is None:
            cfg = self.configs[key] = Config(flags, confidence, source)
        elif _RANK[confidence] > _RANK[cfg.confidence]:
            cfg.confidence, cfg.source = confidence, source
        cfg.targets.update(targets_for(evidence["file"]))
        cfg.add_evidence(evidence)

    def as_dict(self, head=None, kind=None, ref=None):
        configs = sorted(self.configs.values(),
                         key=lambda c: (-_RANK[c.confidence], c.flags))
        union = sorted({f for c in configs for f in c.flags})
        return {
            "input": {"kind": kind, "ref": ref, "head": head},
            "configs": [c.as_dict() for c in configs],
            "flags_union": union,
            "targets": sorted({t for c in configs for t in c.targets}),
            "unmapped_macros": sorted(self.unmapped_macros),
            "unattributed_files": self.unattributed,
            "skipped_files": self.skipped,
            "warnings": self.warnings,
        }


_RANK = {"high": 3, "medium": 2, "low": 1}


class Tracer(object):
    def __init__(self, root, use_cmake_fallback=True):
        self.root = root
        self.macro_map = dict(known_map.KNOWN)
        self.dir_map = dict(known_map.KNOWN_DIRS)
        self.file_map = dict(known_map.KNOWN_FILES)
        self.use_cmake_fallback = use_cmake_fallback
        self._cmake_loaded = False

    def _flags_for(self, macro, result):
        """Built-in table first; parse cmake only on a miss."""
        flags = self.macro_map.get(macro)
        if flags is not None:
            return flags
        if not self.use_cmake_fallback or self._cmake_loaded:
            return None
        self._cmake_loaded = True
        try:
            macros, dirs, files = cmake_map.load(self.root)
        except Exception as e:                # never fail the whole run
            result.warnings.append("cmake fallback failed: %s" % e)
            return None
        for target, parsed in ((self.macro_map, macros), (self.dir_map, dirs),
                               (self.file_map, files)):
            for k, v in parsed.items():
                target.setdefault(k, tuple(v))
        result.warnings.append(
            "known_map miss (%s); parsed cmake -- consider regenerating "
            "known_map.py" % macro)
        return self.macro_map.get(macro)

    def _file_flags(self, path):
        """Switches a file needs just to be compiled at all.

        A source picked inside a cmake conditional (wasm_mini_loader.c) is more
        specific than the directory it lives in, so it wins.
        """
        if path in self.file_map:
            return self.file_map[path]
        d = os.path.dirname(path)
        while d:
            if d in self.dir_map:
                return self.dir_map[d]
            d = os.path.dirname(d)
        return None

    def run(self, diff_text, head_ref):
        result = Result()
        for fd in diffparse.parse(diff_text):
            if fd.kind == "other":
                result.skipped.append(fd.path)
            elif fd.kind == "cmake":
                self._do_cmake(fd, result)
            else:
                self._do_source(fd, head_ref, result)
        return result

    def _do_source(self, fd, head_ref, result):
        bare = []          # changed lines with no #if around them
        # prerequisites of the file itself; they hold for every line in it
        base = {"%s=1" % f for f in (self._file_flags(fd.path) or ())}
        for ref, path, lines in ((head_ref, fd.path, fd.new_lines),
                                 ("%s^" % head_ref if head_ref else None,
                                  fd.old_path or fd.path, fd.old_lines)):
            if not lines:
                continue
            text = inputs.read_file(self.root, ref, path)
            if text is None:
                continue
            for line, (alts, src) in cpp_scope.macros_at(text, lines).items():
                hit = False
                for guards in alts:
                    # guards are ordered outermost first; the innermost one
                    # wins when the same switch is constrained twice
                    settings = {}
                    for macro, on in guards:
                        mapped = self._flags_for(macro, result)
                        if not mapped:
                            result.unmapped_macros.add(macro)
                            continue
                        for setting in settings_for(macro, on, mapped):
                            name, _, value = setting.partition("=")
                            settings[name] = value
                    flags = {"%s=%s" % kv for kv in settings.items()}
                    if flags:
                        hit = True
                        result.add(_merge(base, flags), "high", src,
                                   {"file": fd.path, "line": line,
                                    "macros": sorted(m for m, _ in guards)})
                if not hit:
                    bare.append(line)
        if not bare:
            return
        if base:
            # carry a line along so --verify can probe this claim too
            result.add(base, "medium", "feature-dir",
                       {"file": fd.path, "dir": os.path.dirname(fd.path),
                        "line": bare[0]})
        elif fd.path not in result.unattributed:
            result.unattributed.append(fd.path)

    def _do_cmake(self, fd, result):
        """cmake changes name their switches directly in the diff."""
        flags = set()
        for line in fd.changed:
            flags.update(_WAMR.findall(line))
            for macro in _WASM.findall(line):
                mapped = self._flags_for(macro, result)
                if mapped:
                    flags.update(mapped)  # a cmake edit names the switch itself
        if not flags:
            result.unattributed.append(fd.path)
            return
        for f in sorted(flags):
            result.add({"%s=1" % f}, "high", "cmake-diff",
                       {"file": fd.path, "flag": f})

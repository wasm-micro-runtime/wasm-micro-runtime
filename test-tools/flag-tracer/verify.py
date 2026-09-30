# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Two-way verification of an inferred switch set.

An `#error WAMR_FLAG_PROBE:<id>` line is spliced in above a changed line and
the file is preprocessed.  The directive fires only when that line is really
being compiled -- and it cannot fire at all when the file is not part of the
build, so one probe checks both the line-level and the file-level claim.

  switches ON   -> the probe must fire      (otherwise: over_broad)
  switches OFF  -> the probe must not fire  (otherwise: unconditional)

Nothing is written into the work tree: the probed copy goes to a temp dir and
the original directory is put back on the include path.
"""

import os
import re
import subprocess
import tempfile

import compdb
import known_map

PROBE = "WAMR_FLAG_PROBE"
_HIT = re.compile(PROBE + r":([A-Za-z0-9_]+)")

VERIFIED = "verified"
OVER_BROAD = "over_broad"          # switches on, yet the code is not compiled
UNCONDITIONAL = "unconditional"    # switches off, yet the code is compiled
NOT_IN_BUILD = "not_in_build"      # file absent from this cmake project
ERROR = "error"
SKIPPED = "skipped"                # over --verify-max


def _split(flags):
    """["WAMR_BUILD_GC=1", "WAMR_BUILD_X=0"] -> (["...GC"], ["...X"])"""
    on, off = [], []
    for setting in flags:
        name, _, value = setting.partition("=")
        (on if value == "1" else off).append(name)
    return on, off


def _probe_source(text, line, probe_id):
    lines = text.splitlines(True)
    idx = max(0, min(line - 1, len(lines)))
    lines.insert(idx, '#error "%s:%s"\n' % (PROBE, probe_id))
    return "".join(lines)


def _run(argv, cwd):
    p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True,
                       errors="replace")
    return p.stderr + p.stdout


class Verifier(object):
    """Verifies configs against a real cmake configuration."""

    def __init__(self, root, source=None, read_file=None, limit=8):
        self.root = root
        self.source = source
        self.read_file = read_file or self._read_disk
        # each config costs two cmake configures; cap the bill on huge diffs
        self.limit = limit

    def _read_disk(self, path):
        try:
            with open(os.path.join(self.root, path), encoding="utf-8",
                      errors="replace") as f:
                return f.read()
        except OSError:
            return None

    def _round(self, db, probes, tmp):
        """Return the set of probe ids that fired."""
        fired = set()
        for probe_id, path, line in probes:
            if not db.compiles(path):
                continue
            text = self.read_file(path)
            if text is None:
                continue
            probed = os.path.join(tmp, "%s_%s" % (probe_id,
                                                  os.path.basename(path)))
            with open(probed, "w", encoding="utf-8") as f:
                f.write(_probe_source(text, line, probe_id))
            argv, cwd = db.preprocess_argv(path, probed)
            for hit in _HIT.findall(_run(argv, cwd)):
                fired.add(hit)
        return fired

    def verify(self, data):
        """Annotate every config in `data` with a verdict; returns `data`."""
        if len(data["configs"]) > self.limit:
            data.setdefault("warnings", []).append(
                "verified the first %d of %d configurations (--verify-max)"
                % (self.limit, len(data["configs"])))
        for i, cfg in enumerate(data["configs"]):
            if i >= self.limit:
                cfg["verdict"], cfg["verified_with"] = SKIPPED, None
                continue
            probes, seen = [], set()
            for j, ev in enumerate(cfg["evidence"]):
                if "line" not in ev or (ev["file"], ev["line"]) in seen:
                    continue
                seen.add((ev["file"], ev["line"]))
                probes.append(("c%dp%d" % (i, j), ev["file"], ev["line"]))
            if not probes:
                cfg["verdict"], cfg["verified_with"] = None, None
                continue
            want_on, want_off = _split(cfg["flags"])
            cfg["verdict"], cfg["verified_with"] = self._verify_one(
                want_on, want_off, probes,
                cfg.get("targets") or ["iwasm"], data)
        return data

    def _verify_one(self, want_on, want_off, probes, targets, data):
        """Try each target this change lands in; the first one that actually
        builds the file decides.  Returns (verdict, target used)."""
        sources = ([(None, self.source)] if self.source
                   else [(t, known_map.TARGETS[t]) for t in targets
                         if t in known_map.TARGETS])
        verdict = NOT_IN_BUILD
        for target, source in sources:
            verdict = self._one_source(want_on, want_off, probes, source, data)
            if verdict != NOT_IN_BUILD:
                return verdict, target
        return verdict, None

    def _one_source(self, want_on, want_off, probes, source, data):
        try:
            # positive round: the configuration the inference claims;
            # negative round: every switch flipped
            on = compdb.CompDB(self.root, flags_on=want_on,
                               flags_off=want_off, source=source)
            off = compdb.CompDB(self.root, flags_on=want_off,
                                flags_off=want_on, source=source)
            if not any(on.compiles(p[1]) for p in probes):
                return NOT_IN_BUILD
            with tempfile.TemporaryDirectory(prefix="flag-tracer-") as tmp:
                fired_on = self._round(on, probes, tmp)
                fired_off = self._round(off, probes, tmp)
        except compdb.ConfigureError as e:
            data.setdefault("warnings", []).append(str(e).splitlines()[0])
            return ERROR
        expected = {p[0] for p in probes}
        if fired_off & expected:
            return UNCONDITIONAL
        if not (fired_on & expected):
            return OVER_BROAD
        return VERIFIED

#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Sweep the last N commits and print what the tracer infers for each.

Not a test -- a helper for eyeballing accuracy and for picking new entries for
test_corpus.py.

    python3 test-tools/flag-tracer/tests/corpus_sweep.py 50
"""

import subprocess
import sys

from conftest import REPO_ROOT

import inputs
from tracer import Tracer


def main(count):
    log = subprocess.run(
        ["git", "-C", REPO_ROOT, "log", "--format=%h %s", "-n", str(count),
         "--", "core/"],
        capture_output=True, text=True).stdout.splitlines()
    stats = {"with_flags": 0, "unconditional": 0, "unmapped": 0}
    for entry in log:
        sha, _, subject = entry.partition(" ")
        diff, head = inputs.from_commit(REPO_ROOT, sha)
        data = Tracer(REPO_ROOT).run(diff, head).as_dict()
        combos = [" ".join(c["flags"]) for c in data["configs"]]
        print("%s  %s  <%s>" % (sha, subject[:56],
                                ",".join(data["targets"]) or "-"))
        for c, cfg in zip(combos, data["configs"]):
            print("    [%-6s %-12s] %s" % (cfg["confidence"], cfg["source"], c))
        if data["unattributed_files"]:
            print("    [always] %s" % ", ".join(data["unattributed_files"][:3]))
        if data["unmapped_macros"]:
            print("    [UNMAPPED] %s" % ", ".join(data["unmapped_macros"]))
            stats["unmapped"] += 1
        stats["with_flags" if combos else "unconditional"] += 1
    print("\n%d with switches, %d unconditional, %d with unmapped macros"
          % (stats["with_flags"], stats["unconditional"], stats["unmapped"]))


if __name__ == "__main__":
    main(int(sys.argv[1]) if len(sys.argv) > 1 else 30)

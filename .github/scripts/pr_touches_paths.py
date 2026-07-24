#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Exit 0 if any changed file matches the calling workflow's on.push.paths
globs (honoring ! negations), else exit 1.

The `gate` job uses this to restore the path filtering that `pull_request`
supported but `pull_request_review` does not, so an approval only launches
CI for workflows whose relevant files actually changed.
"""
import argparse
import re
import sys

import yaml


def glob_to_regex(pat):
    # GitHub Actions path glob -> regex. `**` spans directories, `*` does not
    # cross `/`, `?` matches a single non-`/` char.
    i, n, out = 0, len(pat), ["^"]
    while i < n:
        c = pat[i]
        if c == "*":
            if pat[i + 1 : i + 2] == "*":
                out.append(".*")
                i += 2
                if pat[i : i + 1] == "/":
                    i += 1  # `**/` also matches zero directories
            else:
                out.append("[^/]*")
                i += 1
        elif c == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(c))
            i += 1
    out.append("$")
    return "".join(out)


def workflow_path(ref):
    # "owner/repo/.github/workflows/x.yml@refs/heads/main" -> ".github/workflows/x.yml"
    path = ref.split("@", 1)[0]
    parts = path.split("/", 2)
    return parts[2] if len(parts) == 3 else path


def read_patterns(paths_file, workflow_ref):
    # An explicit paths file wins: it lets a caller gate on a narrower set than
    # its own push.paths (e.g. run only when the pipeline definition changed).
    if paths_file:
        with open(paths_file) as f:
            return [ln.strip() for ln in f if ln.strip()]
    with open(workflow_path(workflow_ref)) as f:
        spec = yaml.safe_load(f)
    # YAML 1.1 parses the bare `on:` key as the boolean True.
    on = spec.get("on", spec.get(True, {})) or {}
    push = on.get("push") or {}
    return push.get("paths")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--workflow-ref")
    ap.add_argument("--paths-file")
    ap.add_argument("--changed-files", required=True)
    args = ap.parse_args()

    if not args.paths_file and not args.workflow_ref:
        ap.error("one of --paths-file or --workflow-ref is required")

    patterns = read_patterns(args.paths_file, args.workflow_ref)
    if not patterns:
        return 0  # no path filter -> always relevant

    pos = [re.compile(glob_to_regex(p)) for p in patterns if not p.startswith("!")]
    neg = [re.compile(glob_to_regex(p[1:])) for p in patterns if p.startswith("!")]

    with open(args.changed_files) as f:
        files = [ln.strip() for ln in f if ln.strip()]

    for path in files:
        if any(r.match(path) for r in pos) and not any(r.match(path) for r in neg):
            return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())

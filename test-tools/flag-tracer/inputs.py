# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Normalise --diff / --commit / --pr into (diff_text, head_ref).

Only git is required; `gh` is never invoked.  In CI the PR number is turned
into a merge-base range against the base branch, which works on any checkout
with full history (actions/checkout fetch-depth: 0).
"""

import os
import subprocess
import sys


class InputError(Exception):
    pass


def _git(root, *args):
    p = subprocess.run(("git", "-C", root) + args, capture_output=True,
                       text=True, errors="replace")
    if p.returncode != 0:
        raise InputError("git %s failed: %s" % (" ".join(args), p.stderr.strip()))
    return p.stdout


def read_file(root, ref, path):
    """Read `path` at `ref` (None = working tree); None if it does not exist."""
    if ref is None:
        try:
            with open(os.path.join(root, path), encoding="utf-8",
                      errors="replace") as f:
                return f.read()
        except OSError:
            return None
    try:
        return _git(root, "show", "%s:%s" % (ref, path))
    except InputError:
        return None


def from_diff(root, path):
    if path == "-":
        return sys.stdin.read(), None
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read(), None


def from_commit(root, sha):
    return _git(root, "show", "--unified=0", "--no-color", sha), sha


def from_pr(root, number, base=None):
    """PR -> diff against the merge base of the base branch."""
    base = base or os.environ.get("GITHUB_BASE_REF") or "main"
    head = "HEAD"
    for ref in ("origin/%s" % base, base):
        try:
            _git(root, "rev-parse", "--verify", ref)
        except InputError:
            continue
        merge_base = _git(root, "merge-base", ref, head).strip()
        return (_git(root, "diff", "--unified=0", "--no-color",
                     merge_base, head),
                _git(root, "rev-parse", head).strip())
    raise InputError("cannot resolve base branch %r for PR #%s" % (base, number))


def working_tree(root):
    """Uncommitted changes; handy for local use."""
    return _git(root, "diff", "--unified=0", "--no-color", "HEAD"), None

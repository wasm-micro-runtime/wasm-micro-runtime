#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""flag-tracer -- which build switches guard the code this diff touches?

  python3 test-tools/flag-tracer --commit HEAD
  python3 test-tools/flag-tracer --pr 5034
  git diff | python3 test-tools/flag-tracer --diff -

Exit codes: 0 ok, 2 tool error, 3 --strict verification failure.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import inputs   # noqa: E402
from tracer import Tracer  # noqa: E402


def _repo_root(start):
    d = os.path.abspath(start)
    while d != "/":
        if os.path.isdir(os.path.join(d, ".git")):
            return d
        d = os.path.dirname(d)
    return os.path.abspath(start)


def main(argv=None):
    p = argparse.ArgumentParser(prog="flag-tracer", description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group()
    src.add_argument("--diff", metavar="PATH", help="unified diff file, or - for stdin")
    src.add_argument("--commit", metavar="SHA")
    src.add_argument("--pr", metavar="NUMBER")
    p.add_argument("--base", help="base branch for --pr (default $GITHUB_BASE_REF or main)")
    p.add_argument("-C", "--root", default=".", help="repository root")
    p.add_argument("--no-cmake-fallback", action="store_true",
                   help="rely on the built-in table only")
    p.add_argument("--verify", action="store_true",
                   help="confirm each result by compiling a probe with the "
                        "switches on and off (needs cmake)")
    p.add_argument("--cmake-source", default=None,
                   help="cmake project used by --verify "
                        "(default product-mini/platforms/linux)")
    p.add_argument("--verify-max", type=int, default=8, metavar="N",
                   help="stop verifying after N configurations (default 8)")
    p.add_argument("--strict", action="store_true",
                   help="exit 3 when --verify rejects a result")
    p.add_argument("--github-output", action="store_true",
                   help="also write $GITHUB_OUTPUT / $GITHUB_STEP_SUMMARY")
    args = p.parse_args(argv)

    root = _repo_root(args.root)
    try:
        if args.diff:
            diff, head = inputs.from_diff(root, args.diff)
            kind, ref = "diff", args.diff
        elif args.commit:
            diff, head = inputs.from_commit(root, args.commit)
            kind, ref = "commit", args.commit
        elif args.pr:
            diff, head = inputs.from_pr(root, args.pr, args.base)
            kind, ref = "pr", args.pr
        else:
            diff, head = inputs.working_tree(root)
            kind, ref = "worktree", None
    except inputs.InputError as e:
        sys.stderr.write("flag-tracer: %s\n" % e)
        return 2

    data = Tracer(root, use_cmake_fallback=not args.no_cmake_fallback) \
        .run(diff, head).as_dict(head=head, kind=kind, ref=ref)

    if args.verify:
        import verify

        def reader(path):
            return inputs.read_file(root, head, path)

        verify.Verifier(root, args.cmake_source, reader,
                        args.verify_max).verify(data)

    sys.stdout.write(json.dumps(data, indent=2) + "\n")

    if args.github_output:
        _github(data)
    rejected = ("over_broad", "unconditional")
    if args.strict and any(c.get("verdict") in rejected
                           for c in data["configs"]):
        sys.stderr.write("flag-tracer: verification rejected a result\n")
        return 3
    return 0


def _github(data):
    out = os.environ.get("GITHUB_OUTPUT")
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write("flags=%s\n" % " ".join(data["flags_union"]))
            f.write("targets=%s\n" % " ".join(data["targets"]))
            f.write("configs=%s\n" % json.dumps(data["configs"]))
            f.write("count=%d\n" % len(data["configs"]))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write("### build switches touched by this change\n\n```json\n"
                    + json.dumps(data, indent=2) + "\n```\n")


if __name__ == "__main__":
    sys.exit(main())

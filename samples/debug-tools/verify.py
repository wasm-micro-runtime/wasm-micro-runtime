#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Verify symbolicated output for samples/debug-tools (ctest replacement for
# the removed verify.sh / symbolicate.sh).
#
# What it does
# ------------
# Captures the WAMR call stacks produced by running wasm-apps/trap.wasm and
# wasm-apps/trap.aot under iwasm, symbolicates them with
# test-tools/addr2line/addr2line.py (--mode=interp / --no-addr / --mode=aot),
# and asserts that the user frames (c/b/a/main) and the inline expansion of
# trap_helper (always_inline) appear in the symbolicated output.
#
# When to use
# -----------
# Standalone:  python3 verify.py [build-dir]
# Via ctest:   the debug-tools CMakeLists.txt registers a `debug_tools` test
#              that runs this script. Requires wasi-sdk >= 29 for
#              llvm-symbolizer (see samples/README.md).
#
# Inputs
# ------
#   [build-dir]  the sample build directory containing iwasm and the .wasm
#                apps (default: ./build, or the BUILD_DIR env var)
#
# Outputs
# -------
#   stdout: "PASS [debug-tools ...]" on success, failure diagnostics plus
#           the symbolicated output otherwise; exit code 0/1.

import argparse
import os
import re
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
WAMR_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "../.."))
ADDR2LINE = os.path.join(WAMR_ROOT, "test-tools/addr2line/addr2line.py")
WASI_SDK = os.environ.get("WASI_SDK_PATH", "/opt/wasi-sdk")
WABT = os.environ.get("WABT_PATH", "/opt/wabt")

assert_re = re.compile  # used as the extended-regex helper below


def run(cmd, cwd=None, check=False):
    prc = subprocess.run(cmd, cwd=cwd, check=check, capture_output=True,
                         text=True)
    return prc


def main():
    parser = argparse.ArgumentParser(
        description=("Verify debug-tools symbolication: capture the call "
                     "stacks of trap.wasm / trap.aot, symbolicate them with "
                     "addr2line.py and assert the expected frames. See the "
                     "module docstring for details."),
        epilog="exit code 0 = PASS, 1 = FAIL")
    parser.add_argument("build_dir", nargs="?", type=str, default=None,
                        help="sample build directory containing iwasm and "
                             "the .wasm apps (default: ./build or $BUILD_DIR)")
    args = parser.parse_args()
    build_dir = args.build_dir or os.environ.get(
        "BUILD_DIR", os.path.join(SCRIPT_DIR, "build"))
    iwasm = os.path.join(build_dir, "iwasm")
    if not os.path.isfile(iwasm) or not os.access(iwasm, os.X_OK):
        print("iwasm not found at {}; build the sample first".format(iwasm),
              file=sys.stderr)
        return 1

    # Capture call stacks for .wasm and .aot (the apps trap by design).
    call_stack = os.path.join(build_dir, "call_stack.txt")
    call_stack_aot = os.path.join(build_dir, "call_stack_aot.txt")
    for out_file, wasm_file in (
            (call_stack, "wasm-apps/trap.wasm"),
            (call_stack_aot, "wasm-apps/trap.aot")):
        prc = run(["./iwasm", wasm_file], cwd=build_dir)
        lines = [l for l in prc.stdout.splitlines() if l.startswith("#")]
        with open(out_file, "w") as f:
            f.write("\n".join(lines) + "\n")

    wasm_file = os.path.join(build_dir, "wasm-apps/trap.wasm")
    out = ""
    for args in ([], ["--no-addr"], ["--mode", "aot"]):
        cmd = ["python3", ADDR2LINE, "--wasi-sdk", WASI_SDK, "--wabt", WABT,
               "--wasm-file", wasm_file] + args
        if args == ["--mode", "aot"]:
            cmd.append(os.path.join(build_dir, "call_stack_aot.txt"))
        else:
            cmd.append(os.path.join(build_dir, "call_stack.txt"))
        prc = run(cmd)
        out += prc.stdout + "\n"

    failures = []

    def assert_in(pattern, where):
        if pattern not in out:
            failures.append("pattern '{}' ({}) not found".format(pattern,
                                                                  where))

    def assert_re(pattern, where):
        if not re.search(pattern, out):
            failures.append("regex '{}' ({}) did not match".format(pattern,
                                                                   where))

    assert_re(r'^[0-9]+: c$', "user frame: c")
    assert_re(r'^[0-9]+: b$', "user frame: b")
    assert_re(r'^[0-9]+: a$', "user frame: a")
    assert_re(r'^[0-9]+: main$', "user frame: main")
    assert_in("trap.c", "source file")
    assert_re(r'^[ \t]*0:[ \t]+trap_helper[ \t]+\(inlined into c\)$',
              "inline expansion: trap_helper (inlined into c)")
    if not re.search(r'^[ \t]*0:[ \t]+trap_helper[ \t]+\(inlined into c\)$',
                     "\n".join(out.splitlines()[-25:])):
        failures.append("AOT block (last 25 lines) missing "
                        "'trap_helper (inlined into c)'")

    if failures:
        print("FAIL [debug-tools symbolication]")
        for f in failures:
            print("  " + f)
        print(out)
        return 1
    print("PASS [debug-tools symbolication: precise frames + inline "
          "expansion verified for both wasm and aot]")
    return 0


if __name__ == "__main__":
    sys.exit(main())

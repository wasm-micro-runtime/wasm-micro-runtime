#!/usr/bin/env python3
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Verify symbolicated output for one (app, mode) combination of
# samples/debug-tools-optimized (ctest replacement for the removed
# verify.sh / symbolicate.sh).
#
# What it does
# ------------
# Runs the production binary (oob or stackoverflow, .prod.wasm or .prod.aot)
# under iwasm, captures the WAMR call stack, symbolicates it with
# test-tools/addr2line/addr2line.py using the .debug.wasm companion, and
# asserts the expected exception type, source files and (inlined) function
# frames. Auto-detects classic vs fast interpreter.
#
# When to use
# -----------
# Standalone:  python3 verify.py <oob|stackoverflow> <wasm|aot> [build-dir]
# Via ctest:   the debug-tools-optimized CMakeLists.txt registers
#              debug_tools_optimized_<app>_<mode> tests. Requires binaryen +
#              wasi-sdk >= 29 + wamrc (see samples/README.md).
#
# Inputs
# ------
#   <app>        oob | stackoverflow
#   <mode>       wasm | aot
#   [build-dir]  sample build directory containing iwasm and the wasm apps
#                (default: ./build, or the BUILD_DIR env var)
#
# Outputs
# -------
#   stdout: "PASS [<interp>/<app>/<mode>]" on success, failure diagnostics
#           plus the symbolicated output otherwise; exit code 0/1.

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


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(
        description=("Verify debug-tools-optimized symbolication for one "
                     "(app, mode) combination: run the production binary "
                     "under iwasm, symbolicate the captured call stack and "
                     "assert the expected frames. See the module docstring "
                     "for details."),
        epilog="exit code 0 = PASS, 1 = FAIL, 2 = bad arguments")
    parser.add_argument("app", choices=["oob", "stackoverflow"],
                        help="which demo to run")
    parser.add_argument("mode", choices=["wasm", "aot"],
                        help="binary format to run")
    parser.add_argument("build_dir", nargs="?", type=str, default=None,
                        help="sample build directory containing iwasm and "
                             "the wasm apps (default: ./build or $BUILD_DIR)")
    args = parser.parse_args()
    app = args.app
    mode = args.mode
    build_dir = args.build_dir or os.environ.get(
        "BUILD_DIR", os.path.join(SCRIPT_DIR, "build"))

    iwasm = os.path.join(build_dir, "iwasm")
    if not os.path.isfile(iwasm) or not os.access(iwasm, os.X_OK):
        print("iwasm not found at {}; build the sample first".format(iwasm),
              file=sys.stderr)
        return 1

    # Detect interpreter mode like the removed symbolicate.sh: fast-interp
    # keeps the wasm_interp_fast.c symbol in the binary.
    interp = "fast" if "wasm_interp_fast.c" in run(
        ["strings", iwasm]).stdout else "classic"

    prod_file = os.path.join(build_dir,
                             "wasm-apps/{}.prod.{}".format(app, mode))
    debug_wasm = os.path.join(build_dir, "wasm-apps/{}.debug.wasm".format(app))
    if not os.path.isfile(prod_file):
        print("Production binary not found at {}".format(prod_file),
              file=sys.stderr)
        return 1
    if not os.path.isfile(debug_wasm):
        print("Debug companion not found at {}".format(debug_wasm),
              file=sys.stderr)
        return 1

    # -f app_main calls the exported app_main directly, bypassing wasi _start
    # (see the removed symbolicate.sh). stackoverflow needs --stack-size=4096.
    iwasm_args = []
    if app == "stackoverflow":
        iwasm_args += ["--stack-size=4096"]
    prc = run(["./iwasm"] + iwasm_args + ["-f", "app_main", prod_file],
              cwd=build_dir)
    log = prc.stdout
    call_stack = [l for l in log.splitlines() if re.match(r"^#[0-9]+:", l)]
    if not call_stack:
        print("(no call stack captured)", file=sys.stderr)
        print(log, file=sys.stderr)
        return 1

    # Pick the addr2line.py mode (mirrors the removed symbolicate.sh).
    if mode == "aot":
        a2l_mode = "aot"
    elif interp == "fast":
        a2l_mode = "fast-interp"
    else:
        a2l_mode = "interp"

    out = run(["python3", ADDR2LINE, "--wasi-sdk", WASI_SDK, "--wabt", WABT,
               "--wasm-file", debug_wasm, "--mode", a2l_mode] + call_stack).stdout

    failures = []

    def assert_in(pattern):
        if pattern not in out:
            failures.append("pattern '{}' not found in output".format(pattern))

    def assert_re(pattern):
        if not re.search(pattern, out):
            failures.append("regex '{}' did not match output".format(pattern))

    if app == "oob":
        assert_in("out of bounds memory access")
        if interp == "fast" and mode == "wasm":
            assert_re(r'^0: app_main$')
        else:
            assert_re(r'^0: do_bad_access \(inlined into trigger_oob\)$')
            assert_re(r'^[ \t]+trigger_oob \(inlined into app_main\)$')
            assert_re(r'^[ \t]+app_main$')
            assert_in("oob_access.c")
            assert_in("oob_main.c")
    else:  # stackoverflow
        assert_in("wasm operand stack overflow")
        assert_re(r'^[0-9]+: recurse$')
        assert_in("stackoverflow_recurse.c")
        assert_re(r'^[0-9]+: app_main$')
        if not (interp == "fast" and mode == "wasm"):
            assert_in("stackoverflow_main.c")

    if failures:
        print("FAIL [{}/{}]".format(interp, app))
        for f in failures:
            print("  " + f)
        print(out)
        return 1
    print("PASS [{}/{}/{}]".format(interp, app, mode))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# Copyright (C) 2026, RT-Thread Development Team
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Fault-injection tests using the real RT platform source and a small RT mock."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="gcc")
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[2]
with tempfile.TemporaryDirectory(prefix="wamr-osal-test-") as tmp:
    base_command = [
        args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-DWASM_ENABLE_INTERP=1", "-DBUILD_TARGET_X86_64",
        "-D__USE_MINGW_ANSI_STDIO=1",
        "-I" + str(here / "mock"),
        "-I" + str(root / "core/shared/platform/rt-thread"),
        "-I" + str(root / "core/shared/platform/include"),
        str(here / "test_osal.c"),
        str(root / "core/shared/platform/rt-thread/rtt_platform.c"),
    ]
    for cache_enabled in (False, True):
        mode = "cache_on" if cache_enabled else "cache_off"
        exe = Path(tmp) / ("test_osal_" + mode + ".exe")
        command = base_command + ["-o", str(exe)]
        if cache_enabled:
            command.append("-DRT_USING_CACHE=1")
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)

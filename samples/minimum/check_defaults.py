#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
"""Assert that the all-off preset really turns every feature off.

Every feature switch that the build sets reaches the compiler as -DNAME=value,
so the compile command is the record of what is on.  A switch that is absent
from it takes its core/config.h fallback, which this does not look at.

Note this checks the preset, not the cmake defaults: WAMR_BUILD_BULK_MEMORY and
WAMR_BUILD_SHRUNK_MEMORY ship defaulting to on, on purpose, and the preset turns
them off explicitly.

    cmake --toolchain ../../build-scripts/clang_toolchain.cmake  --preset all-off
    python3 check_defaults.py build/all-off/compile_commands.json
"""

import json
import re
import shlex
import sys

# What counts as a feature switch: the three prefixes WAMR uses for them.  This
# deliberately leaves out the sizes and the platform selectors (WASM_TABLE_MAX_SIZE,
# BH_PLATFORM_LINUX, ...), which say nothing about which features are on.
SWITCH = re.compile(r"^(?:WASM_ENABLE|WASM_DISABLE|BH_ENABLE)_[A-Z0-9_]+$")

# Switches which are legitimately on in an all-off build: the running mode the
# sample asks for.
ALLOWED_ON = {"WASM_ENABLE_INTERP"}


def switch_values(entries):
    """Each switch named by a -D in any compile command, mapped to its value.
    A switch set in several commands is on if any of them turns it on."""
    values = {}
    for entry in entries:
        for arg in shlex.split(entry["command"]):
            if not arg.startswith("-D"):
                continue
            name, _, value = arg[2:].partition("=")
            if SWITCH.match(name):
                on = (value or "1") != "0"
                values[name] = values.get(name, False) or on
    return values


def main(path: str) -> int:
    with open(path, encoding="utf-8") as f:
        entries = json.load(f)

    values = switch_values(entries)
    if not values:
        print("no feature switch found in compile_commands.json, nothing was checked")
        return 1

    unexpected = sorted(
        name for name, on in values.items() if on and name not in ALLOWED_ON
    )

    if unexpected:
        print("The all-off preset should leave every feature off, but found:")
        for name in unexpected:
            print(f"  {name}")
        return 1

    print(f"all-off build: {len(values)} switches, all off except the expected ones")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

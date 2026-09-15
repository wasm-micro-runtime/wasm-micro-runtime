#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
"""Assert that the all-off preset really turns every feature off.

Most switches only reach the compiler when they are on: build-scripts emits
-DWASM_ENABLE_X=1 and leaves the 0 case to the #ifndef fallback in
core/config.h.  Reading the compile command alone therefore says nothing about
the switches that are off, which is exactly what this wants to check.

So instead of parsing -D flags, this preprocesses core/config.h with the flags
the build actually uses and reads the macro values the compiler ends up seeing.

Note this checks the preset, not the cmake defaults: WAMR_BUILD_BULK_MEMORY and
WAMR_BUILD_SHRUNK_MEMORY ship defaulting to on, on purpose, and the preset turns
them off explicitly.

    cmake --preset all-off
    python3 check_defaults.py build/all-off/compile_commands.json
"""

import json
import re
import shlex
import subprocess
import sys
from pathlib import Path

WAMR_ROOT = Path(__file__).resolve().parents[2]
CONFIG_H = WAMR_ROOT / "core" / "config.h"

# What counts as a feature switch: the three prefixes WAMR uses for them.  This
# deliberately leaves out the sizes and the platform selectors (WASM_TABLE_MAX_SIZE,
# WASM_CPU_SUPPORTS_UNALIGNED_ADDR_ACCESS, BH_PLATFORM_LINUX, ...), which are
# non-zero in every build and say nothing about which features are on.
SWITCH = re.compile(r"^(?:WASM_ENABLE|WASM_DISABLE|BH_ENABLE)_[A-Z0-9_]+$")

# Switches which are legitimately non-zero in an all-off build:
#   - the running mode the sample explicitly asks for
#   - values derived from the compiler or the host platform
ALLOWED_NON_ZERO = {
    "WASM_ENABLE_INTERP",
    "WASM_ENABLE_LABELS_AS_VALUES",
    "WASM_ENABLE_LOG",
}

DEFINE = re.compile(r"^#define\s+((?:WASM|BH)_[A-Z0-9_]+)\s+(\S+)$", re.M)


def build_flags(entries):
    """The -D/-I/-m flags of any entry that compiles a core/ source file."""
    for entry in entries:
        if "/core/" not in entry.get("file", ""):
            continue
        return [
            arg
            for arg in shlex.split(entry["command"])
            if arg.startswith(("-D", "-I", "-m", "-f"))
        ]
    raise SystemExit("no core/ translation unit in compile_commands.json")


def effective_macros(flags):
    """Switch values the compiler sees after core/config.h has had its say."""

    def preprocess(source):
        return subprocess.run(
            ["cc", *flags, "-E", "-x", "c", "-"],
            input=source,
            capture_output=True,
            text=True,
            check=True,
        ).stdout

    # Pass 1: which switches exist at all.  -dM reports the definition verbatim,
    # which is enough to find the names but not to read the values -- some are
    # aliases rather than literals (WASM_ENABLE_HEAP_AUX_STACK_ALLOCATION is
    # defined as WASM_ENABLE_LIB_WASI_THREADS).
    listing = subprocess.run(
        ["cc", *flags, "-E", "-dM", "-x", "c", str(CONFIG_H)],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    names = sorted(n for n, _ in DEFINE.findall(listing) if SWITCH.match(n))

    # Pass 2: let the preprocessor evaluate them.  The name is quoted so that it
    # survives expansion while the value next to it does not.
    probe = f'#include "{CONFIG_H}"\n' + "".join(
        f'__wamr_switch__ "{name}" {name}\n' for name in names
    )
    seen = re.findall(
        r'^__wamr_switch__\s+"([A-Z0-9_]+)"\s+(.*)$', preprocess(probe), re.M
    )
    return {name: value.strip() for name, value in seen}


def main(path: str) -> int:
    with open(path, encoding="utf-8") as f:
        entries = json.load(f)

    values = effective_macros(build_flags(entries))

    unexpected = sorted(
        f"{name}={value}"
        for name, value in values.items()
        if value != "0" and name not in ALLOWED_NON_ZERO
    )

    if unexpected:
        print("The all-off preset should leave every feature off, but found:")
        for item in unexpected:
            print(f"  {item}")
        return 1

    print(f"all-off build: {len(values)} macros, all off except the expected ones")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))

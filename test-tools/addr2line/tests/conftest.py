# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""
pytest fixtures for the addr2line.py test suite.

Default mode: a single wasi-sdk (from $WASI_SDK_PATH or /opt/wasi-sdk).
With --multi-sdk: discover all /opt/wasi-sdk-*-x86_64-linux installations
and parametrize the wasi_sdk fixture so build-based tests run once per SDK.
"""

import os
import shlex
import subprocess
import sys
from pathlib import Path

import pytest


# --- CLI options ----------------------------------------------------------

def pytest_addoption(parser):
    parser.addoption(
        "--multi-sdk",
        action="store_true",
        default=False,
        help="Run build-based tests against every detected "
             "/opt/wasi-sdk-*-x86_64-linux installation (default: only "
             "the SDK from WASI_SDK_PATH or /opt/wasi-sdk).",
    )


# --- Tool-discovery helpers ----------------------------------------------

def _detect_sdks_under_opt():
    """Return a sorted list of (version_str, Path) tuples for every
    wasi-sdk under /opt that has a working clang AND ships
    llvm-symbolizer. addr2line.py requires wasi-sdk 29+; pre-29
    bundles omit llvm-symbolizer and are filtered out here. Newest
    version last."""
    found = []
    for d in sorted(Path("/opt").glob("wasi-sdk-*-x86_64-linux")):
        if not (d / "bin" / "clang").exists():
            continue
        if not (d / "bin" / "llvm-symbolizer").exists():
            continue
        ver = d.name.replace("wasi-sdk-", "").replace("-x86_64-linux", "")
        found.append((ver, d))
    return found


def _default_sdk():
    """Return the single SDK to use when --multi-sdk is not given."""
    env = os.environ.get("WASI_SDK_PATH")
    if env and (Path(env) / "bin" / "clang").exists():
        return Path(env)
    default = Path("/opt/wasi-sdk")
    if (default / "bin" / "clang").exists():
        return default
    return None


# --- Parametrization ------------------------------------------------------

def pytest_generate_tests(metafunc):
    """Parametrize the wasi_sdk fixture based on --multi-sdk."""
    if "wasi_sdk" not in metafunc.fixturenames:
        return

    multi = metafunc.config.getoption("--multi-sdk")
    if multi:
        sdks = _detect_sdks_under_opt()
        if not sdks:
            pytest.skip("--multi-sdk: no /opt/wasi-sdk-*-x86_64-linux found")
        ids = [ver for ver, _ in sdks]
        paths = [path for _, path in sdks]
        metafunc.parametrize("wasi_sdk", paths, ids=ids, indirect=False)
    else:
        sdk = _default_sdk()
        if sdk is None:
            pytest.skip("no wasi-sdk found (set WASI_SDK_PATH or install /opt/wasi-sdk)")
        metafunc.parametrize("wasi_sdk", [sdk], ids=[sdk.name], indirect=False)


# --- Session-scoped path fixtures ----------------------------------------

@pytest.fixture(scope="session")
def wabt():
    p = Path(os.environ.get("WABT_PATH", "/opt/wabt"))
    if not (p / "bin" / "wasm-objdump").exists():
        pytest.skip(f"wabt not found at {p}")
    return p


@pytest.fixture(scope="session")
def binaryen():
    p = Path(os.environ.get("BINARYEN_PATH", "/opt/binaryen"))
    if not (p / "bin" / "wasm-opt").exists():
        pytest.skip(f"binaryen not found at {p}")
    return p


@pytest.fixture(scope="session")
def addr2line_script():
    """Return the absolute path to addr2line.py."""
    here = Path(__file__).resolve().parent
    return here.parent / "addr2line.py"


@pytest.fixture(scope="session")
def apps_dir():
    return Path(__file__).resolve().parent / "apps"


@pytest.fixture(scope="session")
def fixtures_dir():
    return Path(__file__).resolve().parent / "fixtures"


# --- Build / invoke fixtures ---------------------------------------------

@pytest.fixture
def build_wasm(wasi_sdk, apps_dir, tmp_path_factory):
    """Build wasm from sources under apps/ with the given flags.

    Cached per (sources tuple, flags tuple, language) within this test
    session and SDK.
    """
    cache = {}

    def _build(sources, flags, language="c"):
        key = (tuple(sources), tuple(flags), language, str(wasi_sdk))
        if key in cache:
            return cache[key]

        compiler = wasi_sdk / "bin" / ("clang++" if language == "cxx" else "clang")
        outdir = tmp_path_factory.mktemp("build", numbered=True)
        out_wasm = outdir / "out.wasm"

        cmd = [str(compiler)] + list(flags) + [
            "--target=wasm32-wasi",
            "-Wl,--export=app_main",
            "-Wl,--no-entry",
            "-nostartfiles",
            "-o", str(out_wasm),
        ] + [str(apps_dir / s) for s in sources]

        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise AssertionError(
                f"build failed:\n  cmd: {' '.join(cmd)}\n"
                f"  stdout: {p.stdout}\n  stderr: {p.stderr}"
            )
        cache[key] = out_wasm
        return out_wasm

    return _build


@pytest.fixture
def wasm_opt_pass(binaryen, tmp_path):
    """Run wasm-opt on a wasm file and return the new path."""
    def _pass(input_wasm, args):
        out = tmp_path / (input_wasm.stem + ".opt.wasm")
        cmd = [str(binaryen / "bin" / "wasm-opt")] + list(args) + [
            "-o", str(out), str(input_wasm),
        ]
        p = subprocess.run(cmd, capture_output=True, text=True)
        if p.returncode != 0:
            raise AssertionError(
                f"wasm-opt failed:\n  cmd: {' '.join(cmd)}\n"
                f"  stdout: {p.stdout}\n  stderr: {p.stderr}"
            )
        return out
    return _pass


@pytest.fixture
def run_addr2line(addr2line_script, wabt, tmp_path, wasi_sdk, request):
    """Invoke addr2line.py and capture stdout/stderr/exitcode.

    `call_stack` may be either a list of strings (one per frame line) or
    a Path to an existing call-stack file. `extra_args` is appended to
    the command line (e.g. ['--mode', 'aot']).

    Under `pytest -v`/`-vv` (verbosity >= 1) the invocation and resolved
    output are printed; combine with `-s` to see them live, otherwise
    pytest captures them and only surfaces them on failure.
    """
    verbose = request.config.getoption("verbose") >= 1

    def _run(wasm_file, call_stack, sdk_override=None, extra_args=()):
        sdk = sdk_override if sdk_override is not None else wasi_sdk
        if isinstance(call_stack, (list, tuple)):
            cs_file = tmp_path / "callstack.txt"
            cs_file.write_text("\n".join(call_stack) + ("\n" if call_stack else ""))
        else:
            cs_file = Path(call_stack)

        cmd = [
            sys.executable, str(addr2line_script),
            "--wasi-sdk", str(sdk),
            "--wabt", str(wabt),
            "--wasm-file", str(wasm_file),
            *list(extra_args),
            str(cs_file),
        ]
        p = subprocess.run(cmd, capture_output=True, text=True)

        if verbose:
            test_id = request.node.name
            print(f"\n--- [{test_id}] addr2line input ---")
            if isinstance(call_stack, (list, tuple)):
                for line in call_stack:
                    print(f"  {line}")
            else:
                print(f"  (from {cs_file})")
            extra = " ".join(extra_args) if extra_args else "(none)"
            print(f"--- [{test_id}] extra args: {extra} ---")
            print(f"--- [{test_id}] addr2line stdout (rc={p.returncode}) ---")
            print(p.stdout.rstrip())
            if p.stderr.strip():
                print(f"--- [{test_id}] addr2line stderr ---")
                print(p.stderr.rstrip())
            print(f"--- [{test_id}] end ---")

        return p.stdout, p.stderr, p.returncode

    return _run


# --- Helpers -------------------------------------------------------------

def find_dwarf_low_pc(wasi_sdk: Path, wasm: Path, func_name: str) -> int | None:
    """Find the DW_AT_low_pc of the named DW_TAG_subprogram in `wasm`.
    Returns int or None if not found."""
    p = subprocess.run(
        [str(wasi_sdk / "bin" / "llvm-dwarfdump"), str(wasm)],
        capture_output=True, text=True,
    )
    in_subp = False
    low = None
    for line in p.stdout.splitlines():
        s = line.strip()
        if "DW_TAG_subprogram" in s:
            in_subp = True
            low = None
            continue
        if in_subp:
            import re
            m = re.match(r'DW_AT_low_pc\s+\((0x[0-9a-fA-F]+)\)', s)
            if m:
                low = int(m.group(1), 16)
                continue
            m = re.match(r'DW_AT_name\s+\("' + re.escape(func_name) + r'"\)', s)
            if m and low is not None:
                return low
            if s.startswith("DW_TAG_") or s == "":
                in_subp = False
                low = None
    return None


def code_section_start(wabt: Path, wasm: Path) -> int:
    """Read the Code section start offset from wasm-objdump -h."""
    p = subprocess.run(
        [str(wabt / "bin" / "wasm-objdump"), "-h", str(wasm)],
        capture_output=True, text=True, check=True,
    )
    import re
    for line in p.stdout.splitlines():
        s = line.strip()
        if "Code" in s and "start=" in s:
            m = re.search(r"start=(0x[0-9a-fA-F]+)", s)
            if m:
                return int(m.group(1), 16)
    raise AssertionError("Code section not found in " + str(wasm))

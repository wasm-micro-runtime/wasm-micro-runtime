#!/bin/bash
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -euo pipefail

# Symbolicate the captured call stack files (call_stack.txt for .wasm,
# call_stack_aot.txt for .aot) located in this sample's build/ directory.
# Self-locating so invocation works from any cwd.

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"
ADDR2LINE="${SCRIPT_DIR}/../../test-tools/addr2line/addr2line.py"
WASM_FILE="${BUILD_DIR}/wasm-apps/trap.wasm"
CALL_STACK="${BUILD_DIR}/call_stack.txt"
CALL_STACK_AOT="${BUILD_DIR}/call_stack_aot.txt"

WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
WABT_PATH="${WABT_PATH:-/opt/wabt}"

banner() {
    echo
    echo "===== $* ====="
}

# .wasm + classic-interp: addr2line.py defaults to --mode=interp.
banner "wasm / --mode=interp (default; classic interpreter, post-advance offsets)"
python3 "$ADDR2LINE" \
    --wasi-sdk "$WASI_SDK_PATH" \
    --wabt "$WABT_PATH" \
    --wasm-file "$WASM_FILE" \
    "$CALL_STACK"

# Same call stack with --no-addr: function-level resolution only — use
# this for fast-interp call stacks (transformed bytecode has no source
# mapping) and for very old iwasm dumps that don't carry offsets.
banner "wasm / --no-addr (function-name lookup only; fast-interp & WAMR<=1.3.2 fallback)"
python3 "$ADDR2LINE" \
    --wasi-sdk "$WASI_SDK_PATH" \
    --wabt "$WABT_PATH" \
    --wasm-file "$WASM_FILE" \
    --no-addr \
    "$CALL_STACK"

# .aot: --mode=aot — wamrc commits ip at instruction-start (not post-advance).
banner "aot / --mode=aot (instruction-start offsets)"
python3 "$ADDR2LINE" \
    --wasi-sdk "$WASI_SDK_PATH" \
    --wabt "$WABT_PATH" \
    --wasm-file "$WASM_FILE" \
    --mode aot \
    "$CALL_STACK_AOT"

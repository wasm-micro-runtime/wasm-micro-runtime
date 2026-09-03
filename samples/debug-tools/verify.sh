#!/bin/bash
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Verify symbolicated output for samples/debug-tools.
#
# Usage: ./verify.sh
#
# Captures call stacks for both .wasm and .aot, runs symbolicate.sh, and
# asserts that the inline expansion of trap_helper (always_inline) shows
# up under both --mode=interp (the default for the .wasm path) and
# --mode=aot (the third invocation in symbolicate.sh).

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR="${SCRIPT_DIR}/build"

if [ ! -x "${BUILD_DIR}/iwasm" ]; then
    echo "iwasm not found at ${BUILD_DIR}/iwasm; build the sample first" >&2
    exit 1
fi

cd "${BUILD_DIR}"
./iwasm wasm-apps/trap.wasm 2>&1 | grep "^#" > call_stack.txt || true
./iwasm wasm-apps/trap.aot 2>&1 | grep "^#" > call_stack_aot.txt || true

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT
"${SCRIPT_DIR}/symbolicate.sh" 2>&1 | tee "$OUT" > /dev/null

assert() {
    local pattern="$1"
    local where="$2"
    if ! grep -q "$pattern" "$OUT"; then
        echo "FAIL: pattern '$pattern' ($where) not found in symbolicate.sh output" >&2
        cat "$OUT" >&2
        exit 1
    fi
}

# assert_re: like assert but with POSIX extended regex (anchored line match).
assert_re() {
    local pattern="$1"
    local where="$2"
    if ! grep -Eq "$pattern" "$OUT"; then
        echo "FAIL: regex '$pattern' ($where) did not match symbolicate.sh output" >&2
        cat "$OUT" >&2
        exit 1
    fi
}

# trap.c's runtime call chain is _start -> ... -> main -> a -> b -> c
# (with c calling __builtin_trap inside trap_helper, an always_inline
# helper). Symbolicate.sh runs three invocations against the same call
# stack. We assert the precise frames each one produces.
#
# All invocations should symbolicate the user-code frames `c`, `b`, `a`,
# `main` (each on its own line, prefixed by the frame index).
assert_re '^[0-9]+: c$'    "user frame: c"
assert_re '^[0-9]+: b$'    "user frame: b"
assert_re '^[0-9]+: a$'    "user frame: a"
assert_re '^[0-9]+: main$' "user frame: main"
assert "trap.c" "source file"

# Inline expansion of trap_helper (always_inline) only surfaces in the
# address-based runs (default --mode=interp and --mode=aot). It MUST
# appear at least once in the captured output, with the
# "(inlined into c)" annotation that print_frames adds for inlined
# frames.
assert_re '^[[:space:]]*0:[[:space:]]+trap_helper[[:space:]]+\(inlined into c\)$' \
    "inline expansion: trap_helper (inlined into c)"

# The AOT block is the third (last) invocation in symbolicate.sh.
# Confirm --mode=aot also produces inline expansion (the outermost
# frame at index 0 should still be trap_helper inlined into c).
if ! tail -25 "$OUT" | grep -Eq '^[[:space:]]*0:[[:space:]]+trap_helper[[:space:]]+\(inlined into c\)$'; then
    echo "FAIL: AOT block (last 25 lines) missing 'trap_helper (inlined into c)' — --mode=aot may be broken" >&2
    cat "$OUT" >&2
    exit 1
fi

echo "PASS [debug-tools symbolication: precise frames + inline expansion verified for both wasm and aot]"

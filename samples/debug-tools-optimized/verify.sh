#!/bin/bash
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Verify symbolicated output for one (app, mode) combination.
#
# Usage: ./verify.sh <oob|stackoverflow> <wasm|aot>
#
# Runs ./symbolicate.sh and asserts the output contains the expected source
# file references. Auto-detects whether iwasm was built with classic or fast
# interpreter (via the symbolicate.sh script itself) and relaxes the
# fast-interp + oob + wasm assertion since fast-interp can't resolve
# offset=0 (trap-at-entry) to a source line.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
APP="${1:-}"
MODE="${2:-}"

if [ "$APP" != "oob" ] && [ "$APP" != "stackoverflow" ]; then
    echo "Usage: $0 <oob|stackoverflow> <wasm|aot>" >&2
    exit 2
fi
if [ "$MODE" != "wasm" ] && [ "$MODE" != "aot" ]; then
    echo "Usage: $0 <oob|stackoverflow> <wasm|aot>" >&2
    exit 2
fi

# Detect interpreter mode by checking the iwasm binary (same logic as symbolicate.sh)
IWASM="${SCRIPT_DIR}/build/iwasm"
if [ ! -x "$IWASM" ]; then
    echo "iwasm not found at $IWASM; build the sample first" >&2
    exit 1
fi
INTERP="classic"
if strings "$IWASM" 2>/dev/null | grep -q "wasm_interp_fast.c"; then
    INTERP="fast"
fi

OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

"${SCRIPT_DIR}/symbolicate.sh" "$APP" "$MODE" 2>&1 | tee "$OUT" > /dev/null

assert() {
    local pattern="$1"
    if ! grep -q "$pattern" "$OUT"; then
        echo "FAIL [$INTERP/$APP/$MODE]: pattern '$pattern' not found in output" >&2
        cat "$OUT" >&2
        exit 1
    fi
}

# assert_re: assert a POSIX extended regex matches somewhere in the captured output.
# Used for compound expectations like "function name X on its own line".
assert_re() {
    local pattern="$1"
    if ! grep -Eq "$pattern" "$OUT"; then
        echo "FAIL [$INTERP/$APP/$MODE]: regex '$pattern' did not match output" >&2
        cat "$OUT" >&2
        exit 1
    fi
}

case "$APP" in
    oob)
        # Runtime always reports the OOB exception type.
        assert "out of bounds memory access"
        # do_bad_access -> trigger_oob -> app_main are all inlined into a
        # single WASM function under -Oz -flto. On classic-interp + wasm
        # and aot builds, DWARF retains the inline chain, so addr2line.py
        # emits all three names with "(inlined into <next>)" annotations.
        # On fast-interp + wasm the runtime offset doesn't map to source
        # (transformed bytecode), so addr2line.py falls back to
        # function-name lookup and emits only the outermost frame.
        if [ "$INTERP" = "fast" ] && [ "$MODE" = "wasm" ]; then
            assert_re '^0: app_main$'
        else
            assert_re '^0: do_bad_access \(inlined into trigger_oob\)$'
            assert_re '^[[:space:]]+trigger_oob \(inlined into app_main\)$'
            assert_re '^[[:space:]]+app_main$'
            assert "oob_access.c"
            assert "oob_main.c"
        fi
        ;;
    stackoverflow)
        # stackoverflow_recurse.c uses non-tail recursion that reliably
        # exhausts the WASM operand stack (with --stack-size=4096 in
        # symbolicate.sh, depth lands around 20 frames).
        assert "wasm operand stack overflow"
        # The captured call stack is recurse repeated, ending in app_main.
        # addr2line.py must resolve both function names exactly.
        assert_re '^[0-9]+: recurse$'
        assert "stackoverflow_recurse.c"
        if [ "$INTERP" = "fast" ] && [ "$MODE" = "wasm" ]; then
            # fast-interp falls back to function-name lookup; the
            # outermost app_main frame may not surface stackoverflow_main.c
            # but the function name itself does.
            assert_re '^[0-9]+: app_main$'
        else
            assert_re '^[0-9]+: app_main$'
            assert "stackoverflow_main.c"
        fi
        ;;
esac

echo "PASS [$INTERP/$APP/$MODE]"

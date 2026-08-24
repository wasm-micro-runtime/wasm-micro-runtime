#!/bin/bash
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set -euo pipefail

# Run a wasm or aot app, capture the WAMR call stack, and symbolicate it
# using addr2line.py with the debug companion.
#
# Usage: ./symbolicate.sh [oob|stackoverflow] [wasm|aot]

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
APP="${1:-oob}"
MODE="${2:-wasm}"

if [ "$APP" != "oob" ] && [ "$APP" != "stackoverflow" ]; then
    echo "Usage: $0 [oob|stackoverflow] [wasm|aot]" >&2
    exit 1
fi
if [ "$MODE" != "wasm" ] && [ "$MODE" != "aot" ]; then
    echo "Usage: $0 [oob|stackoverflow] [wasm|aot]" >&2
    exit 1
fi

WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
WABT_PATH="${WABT_PATH:-/opt/wabt}"
WAMR_ROOT="${SCRIPT_DIR}/../.."

BUILD_DIR="${SCRIPT_DIR}/build"
if [ "$MODE" = "wasm" ]; then
    PROD_FILE="${BUILD_DIR}/wasm-apps/${APP}.prod.wasm"
else
    PROD_FILE="${BUILD_DIR}/wasm-apps/${APP}.prod.aot"
fi
DEBUG_WASM="${BUILD_DIR}/wasm-apps/${APP}.debug.wasm"
IWASM="${BUILD_DIR}/iwasm"

if [ ! -x "${IWASM}" ]; then
    echo "iwasm not found at ${IWASM}" >&2
    echo "Run: mkdir -p build && cd build && cmake .. && make" >&2
    exit 1
fi
if [ ! -f "${PROD_FILE}" ]; then
    echo "Production binary not found at ${PROD_FILE}" >&2
    exit 1
fi
if [ ! -f "${DEBUG_WASM}" ]; then
    echo "Debug companion not found at ${DEBUG_WASM}" >&2
    exit 1
fi

CALL_STACK_FILE=$(mktemp)
LOG_FILE=$(mktemp)
trap 'rm -f "${CALL_STACK_FILE}" "${LOG_FILE}"' EXIT

echo "=== Running iwasm on ${APP}.prod.${MODE} (expect crash) ==="
# -f app_main calls the exported app_main directly, bypassing wasi _start.
# This preserves the OOB / stack-overflow trap behavior — running _start
# under -Oz -flto would lower the OOB pattern to `unreachable` and produce
# misleading call-stack info.
#
# stackoverflow uses --stack-size=4096 so the WASM operand stack overflows
# at a reasonable depth (~20 frames). With the default stack size the
# recursion would still trap, just much later (~400 frames) and with a
# much larger captured call-stack to symbolicate.
IWASM_ARGS=()
if [ "$APP" = "stackoverflow" ]; then
    IWASM_ARGS+=(--stack-size=4096)
fi
"${IWASM}" "${IWASM_ARGS[@]}" -f app_main "${PROD_FILE}" 2>&1 | tee "${LOG_FILE}" || true

echo ""
echo "=== Captured call stack ==="
grep -E "^#[0-9]+:" "${LOG_FILE}" > "${CALL_STACK_FILE}" || true
cat "${CALL_STACK_FILE}"

if [ ! -s "${CALL_STACK_FILE}" ]; then
    echo "(no call stack captured)"
    exit 1
fi

echo ""
echo "=== Symbolicated call stack (using debug companion) ==="
# Pick the right --mode for addr2line.py:
#   - aot:           offsets are file-absolute, no adjustment (wamrc commits ip
#                    at instruction start). Always use --mode=aot regardless of
#                    how iwasm itself was built.
#   - wasm + classic interp:    offsets are file-absolute, post-advance → --mode=interp
#   - wasm + fast interp:       offsets are function-relative in transformed
#                               bytecode → --mode=fast-interp (function-name lookup only)
#
# Detect fast-interp by inspecting CMakeCache.txt for WAMR_BUILD_FAST_INTERP=1.
if [ "$MODE" = "aot" ]; then
    A2L_MODE=aot
else
    A2L_MODE=interp
    # Detect fast-interp by inspecting iwasm for the wasm_interp_fast.c symbol.
    if strings "${IWASM}" 2>/dev/null | grep -q "wasm_interp_fast.c"; then
        A2L_MODE=fast-interp
    fi
fi

# DWARF only lives in the .debug.wasm — addr2line.py uses it for all modes.
python3 "${WAMR_ROOT}/test-tools/addr2line/addr2line.py" \
    --wasi-sdk "${WASI_SDK_PATH}" \
    --wabt "${WABT_PATH}" \
    --wasm-file "${DEBUG_WASM}" \
    --mode "${A2L_MODE}" \
    "${CALL_STACK_FILE}"

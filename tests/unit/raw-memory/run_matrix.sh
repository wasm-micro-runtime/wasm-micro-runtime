#!/usr/bin/env bash
# Copyright (C) 2026 Pymergetic | Rouven Raudzus. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Build and run Raw memory unit tests for every engine profile.
set -euo pipefail

SRC_DIR=${1:-$(cd "$(dirname "$0")" && pwd)}
OUT_DIR=${2:-/tmp/wamr-raw-memory-matrix}

profiles=(fast_jit fast_interp memory64)
failed=0

for profile in "${profiles[@]}"; do
  build="$OUT_DIR/$profile"
  echo "======== PROFILE $profile ========"
  mkdir -p "$build"
  aot_args=()
  if [[ "$profile" == "fast_interp" ]]; then
    aot_args+=(-DRAW_MEMORY_TEST_BUILD_AOT=OFF)
  fi
  cmake -S "$SRC_DIR" -B "$build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWAMR_BUILD_PLATFORM=linux \
    -DRAW_MEMORY_TEST_PROFILE="$profile" \
    "${aot_args[@]}" \
    ${LLVM_DIR:+-DLLVM_DIR=$LLVM_DIR}
  cmake --build "$build" -j"$(nproc)"
  (cd "$build" && ./raw_memory_test) || failed=1
done

if [[ "$failed" -ne 0 ]]; then
  echo "Raw memory matrix: FAILURE" >&2
  exit 1
fi
echo "Raw memory matrix: all profiles passed"

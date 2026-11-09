#!/usr/bin/env bash
# Copyright (C) 2026 The WAMR Authors. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <WAMR build directory containing libiwasm.a>" >&2
    exit 2
fi

build_dir=$(cd "$1" && pwd)
test_dir=$(cd "$(dirname "$0")" && pwd)
"${CC:-cc}" -std=c99 -O2 -o "$build_dir/posix_signal_handler_test" \
    "$test_dir/main.c" "$build_dir/libiwasm.a" -lm -ldl -pthread
"$build_dir/posix_signal_handler_test"

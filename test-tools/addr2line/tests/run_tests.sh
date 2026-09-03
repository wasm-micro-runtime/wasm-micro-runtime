#!/bin/bash
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Thin wrapper around pytest for the addr2line.py test suite.
#
# Usage:
#   ./run_tests.sh                 # default: single SDK from $WASI_SDK_PATH
#   ./run_tests.sh -m "not slow"   # only fixture-based tests
#   ./run_tests.sh --multi-sdk     # parametrize over /opt/wasi-sdk-*
#
# Env vars consumed:
#   WASI_SDK_PATH (default /opt/wasi-sdk)
#   WABT_PATH     (default /opt/wabt)
#   BINARYEN_PATH (default /opt/binaryen)

set -euo pipefail
cd "$(dirname "$0")"
exec python3 -m pytest "$@"

#!/bin/bash

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

# Define a list of .wasm files
file_names=("main")

WORKDIR="$PWD"
WAMRC_ROOT_DIR="${WORKDIR}/../../../wamr-compiler"
WAMRC="${WAMRC_ROOT_DIR}/build/wamrc"
# CI installs wabt under /opt/wabt (not on PATH); prefer it, fall back to PATH.
WAST2WASM="/opt/wabt/bin/wat2wasm"
if [ ! -x "$WAST2WASM" ]; then WAST2WASM="$(command -v wat2wasm)"; fi

# build wamrc if not exist
if [ ! -s "$WAMRC" ]; then
    cd $WAMRC_ROOT_DIR
    if [ -d "$WAMRC/build" ]; then
        rm -r build 
    fi
    cmake -B build && cmake --build build -j $(nproc)
    cd $WORKDIR
fi

# Iterate over the files array
for file_name in "${file_names[@]}"; do
    # compile wasm to aot
    $WAMRC -o "wasm-apps/${file_name}.aot" "wasm-apps/${file_name}.wasm"
done

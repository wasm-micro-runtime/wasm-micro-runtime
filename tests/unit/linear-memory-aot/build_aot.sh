#!/bin/bash

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

# Define a list of .wasm files
file_names=("mem_grow_out_of_bounds_01" "mem_grow_out_of_bounds_02"
    "mem_page_01" "mem_page_02" "mem_page_03" "mem_page_05"
    "mem_page_07" "mem_page_08" "mem_page_09" "mem_page_10"
    "mem_page_12" "mem_page_14" "mem_page_16" "mem_page_20" "out_of_bounds")

WORKDIR="$PWD"
WAMRC_ROOT_DIR="${WORKDIR}/../../../wamr-compiler"
WAMRC="${WAMRC_ROOT_DIR}/build/wamrc"
WAMRC_OPTIONS="--enable-gc"
WAST2WASM="/opt/wabt/bin/wat2wasm"

# build wamrc if not exist
if [ ! -s "$WAMRC" ]; then
    cd $WAMRC_ROOT_DIR
    if [ -d "$WAMRC/build" ]; then
        rm -r build 
    fi
    cmake -B build && cmake --build build -j $(nproc)
    cd $WORKDIR
fi

# error if not exist
if [ ! -s "$WAST2WASM" ]; then
    echo "please install wabt first" && exit -1
fi

# Detect host architecture
HOST_ARCH=$(uname -m)
echo "Detected host architecture: $HOST_ARCH"

# Iterate over the files array
rm -r build 2>/dev/null
mkdir build
for file_name in "${file_names[@]}"; do
    # wast to wasm
    $WAST2WASM "${file_name}.wast" -o "build/${file_name}.wasm"
    
    # Determine compilation configurations based on host architecture
    case "$HOST_ARCH" in
        x86_64)
            # x86-64 host: compile both x86-64 and x86-32
            $WAMRC $WAMRC_OPTIONS -o "build/${file_name}.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --target=i386 -o "build/${file_name}_32.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --bounds-checks=1 -o "build/${file_name}_no_hw_bounds.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --bounds-checks=1 --target=i386 -o "build/${file_name}_no_hw_bounds_32.aot" "build/${file_name}.wasm"
            ;;
        i386|i686)
            # x86-32 host: compile only x86-32
            $WAMRC $WAMRC_OPTIONS -o "build/${file_name}.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --bounds-checks=1 -o "build/${file_name}_no_hw_bounds.aot" "build/${file_name}.wasm"
            ;;
        aarch64|arm64)
            # ARM64 host: compile only aarch64
            $WAMRC $WAMRC_OPTIONS -o "build/${file_name}.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --bounds-checks=1 -o "build/${file_name}_no_hw_bounds.aot" "build/${file_name}.wasm"
            ;;
        *)
            echo "Warning: Unsupported architecture '$HOST_ARCH'. Using default target."
            $WAMRC $WAMRC_OPTIONS -o "build/${file_name}.aot" "build/${file_name}.wasm"
            $WAMRC $WAMRC_OPTIONS --bounds-checks=1 -o "build/${file_name}_no_hw_bounds.aot" "build/${file_name}.wasm"
            ;;
    esac
done

echo "AOT compilation completed for architecture: $HOST_ARCH"

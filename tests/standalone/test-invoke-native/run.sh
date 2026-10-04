#!/bin/bash
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

if [[ $1 == "--classic-interp" ]]; then
    CMAKE_FLAGS="-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=0"
elif [[ $1 == "--fast-interp" ]]; then
    CMAKE_FLAGS="-DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1"
elif [[ $1 == "--fast-jit" ]]; then
    CMAKE_FLAGS="-DWAMR_BUILD_FAST_JIT=1"
elif [[ $1 == "--jit" ]]; then
    CMAKE_FLAGS="-DWAMR_BUILD_JIT=1"
elif [[ $1 == "--multi-tier-jit" ]]; then
    CMAKE_FLAGS="-DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_JIT=1"
fi

echo "============> test dump-invoke-native"

rm -fr build
mkdir build && cd build
cmake .. ${CMAKE_FLAGS}
make -j ${nproc} > /dev/null 2>&1
cd ..

echo "============> run test-invoke-native"
./build/test_invoke_native

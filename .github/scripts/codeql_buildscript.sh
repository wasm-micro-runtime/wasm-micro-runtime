#!/usr/bin/env bash

#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#

# This script is used to build the WAMR project for CodeQL analysis.

# Pre-requisites
sudo apt -qq update
sudo apt install -y -qq build-essential cmake g++-multilib libgcc-12-dev lib32gcc-12-dev ccache ninja-build

LLVM_VER=18.1.8
pushd /opt
sudo wget --progress=dot:giga -O clang+llvm-x86_64-linux-gnu.tar.xz https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VER}/clang+llvm-${LLVM_VER}-x86_64-linux-gnu-ubuntu-18.04.tar.xz \
  && tar -xf clang+llvm-x86_64-linux-gnu.tar.xz \
  && mv clang+llvm-${LLVM_VER}-x86_64-linux-gnu-ubuntu-18.04 llvm-${LLVM_VER}
popd

# libtinfo.so.5 for /opt/llvm-18.1.8/lib/libomptarget.rtl.amdgpu.so.18.1
# Install the libtinfo5 compat package at the same ncurses version as the
# runner's already-installed libtinfo6; fall back to the newest one in the
# pool. A hard-coded URL 404s once the ncurses point release is bumped, which
# then breaks the wamrc link against libomptarget (NCURSES_TINFO_5 symbols).
sudo apt -qq update
nc_pool="http://security.ubuntu.com/ubuntu/pool/universe/n/ncurses"
nc_ver="$(dpkg-query -W -f='${Version}' libtinfo6)"
libtinfo5_deb="libtinfo5_${nc_ver}_amd64.deb"
if ! wget -q "${nc_pool}/${libtinfo5_deb}"; then
  libtinfo5_deb="$(wget -qO- "${nc_pool}/" \
    | grep -oE 'libtinfo5_[0-9][^"]*_amd64\.deb' | sort -Vu | tail -1)"
  wget -q "${nc_pool}/${libtinfo5_deb}"
fi
sudo apt install -y -qq "./${libtinfo5_deb}"

# Start the build process
WAMR_DIR=${PWD}
LLVM_DIR=/opt/llvm-${LLVM_VER}/lib/cmake/llvm

# Function to build wamrc
build_wamrc() {
    local options="$1"
    echo "Building wamrc with options: $options"

    pushd ${WAMR_DIR}/wamr-compiler
    rm -rf build
    cmake -S . -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DLLVM_DIR=${LLVM_DIR} \
        $options
    cmake --build build --target wamrc --parallel
    if [[ $? != 0 ]]; then
        echo "Failed to build wamrc with options: $options"
        exit 1
    fi
    popd
}

# Function to build iwasm
build_iwasm() {
    local options="$1"
    echo "Building iwasm with options: $options"

    pushd ${WAMR_DIR}/product-mini/platforms/linux
    rm -rf build
    cmake -S . -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug \
        -DLLVM_DIR=${LLVM_DIR} \
        $options
    cmake --build build --target iwasm --parallel
    if [[ $? != 0 ]]; then
        echo "Failed to build iwasm with options: $options"
        exit 1
    fi
    popd
}

# List of compilation options for wamrc
wamrc_options_list=(
    #default
    ""
)

# List of compilation options for iwasm
iwasm_options_list=(
    #default
    ""
    # +classic interp
    "-DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_SIMD=0"
    # fast jit
    "-DWAMR_BUILD_FAST_JIT=1 -DWAMR_BUILD_FAST_JIT_DUMP=1 -DWAMR_BUILD_SIMD=0"
    # +llvm jit
    "-DWAMR_BUILD_JIT=1"
    #
    "-DWAMR_BUILD_TARGET=X86_32"
    #
    # libraries
    "-DWAMR_BUILD_LIBC_BUILTIN=0 -DWAMR_BUILD_LIBC_UVWASI=1 -DWAMR_BUILD_LIBC_EMCC=1"
    "-DWAMR_BUILD_THREAD_MGR=1 -DWAMR_BUILD_LIB_PTHREAD=1      -DWAMR_BUILD_LIB_PTHREAD_SEMAPHORE=1"
    "-DWAMR_BUILD_THREAD_MGR=1 -DWAMR_BUILD_LIB_WASI_THREADS=1 -DWAMR_BUILD_LIB_PTHREAD_SEMAPHORE=1"
    "-DWAMR_BUILD_WASI_NN=1 -DWAMR_BUILD_WASI_NN_LLAMACPP=1"
    #
    # Wasm specs
    "-DWAMR_BUILD_GC=1 -DWAMR_BUILD_STRINGREF=1 -DWAMR_STRINGREF_IMPL_SOURCE=STUB"
    "-DWAMR_BUILD_EXCE_HANDLING=1 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_SIMD=0"
    "-DWAMR_BUILD_MEMORY64=1 -DWAMR_BUILD_MULTI_MEMORY=1 -DWAMR_BUILD_SHARED_MEMORY=1 -DWAMR_BUILD_FAST_INTERP=0 -DWAMR_BUILD_SIMD=0 -DWAMR_BUILD_AOT=0"
    #
    # WARM features
    "-DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_MINI_LOADER=1 -DWAMR_BUILD_SHARED_HEAP=1"
    "-DWAMR_DISABLE_HW_BOUND_CHECK=1"
    "-DWAMR_CONFIGURABLE_BOUNDS_CHECKS=1"
    "-DWAMR_BUILD_EXTENDED_CONST_EXPR=1"
    # - Debug
    "-DWAMR_BUILD_DEBUG_INTERP=1 -DWAMR_BUILD_DEBUG_AOT=1 -DWAMR_BUILD_DYNAMIC_AOT_DEBUG=1"
    # - developer options
    "-DWAMR_BUILD_CUSTOM_NAME_SECTION=1  -DWAMR_BUILD_LOAD_CUSTOM_SECTION=1 -DWAMR_BUILD_DUMP_CALL_STACK=1 -DWAMR_BUILD_LINUX_PERF=1 -DWAMR_BUILD_AOT_VALIDATOR=1 -DWAMR_BUILD_MEMORY_PROFILING=1 -DWAMR_BUILD_PERF_PROFILING=1"
    # - global heap
    "-DWAMR_BUILD_ALLOC_WITH_USER_DATA=1  -DWAMR_BUILD_GLOBAL_HEAP_POOL=1 -DWAMR_BUILD_GLOBAL_HEAP_SIZE=131072"
    "-DWAMR_BUILD_QUICK_AOT_ENTRY=0 -DWAMR_DISABLE_WAKEUP_BLOCKING_OP=1 -DWAMR_BUILD_MODULE_INST_CONTEXT=0"
    # - pgo
    "-DWAMR_BUILD_STATIC_PGO=1"
    # TODO: SGX specifics.
)

# Loop through all iwasm options and build
for options in "${iwasm_options_list[@]}"; do
    build_iwasm "$options"
done

# Loop through all wamrc options and build
for options in "${wamrc_options_list[@]}"; do
    build_wamrc "$options"
done

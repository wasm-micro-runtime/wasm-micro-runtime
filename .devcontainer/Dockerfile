# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# hadolint global ignore=DL3008,DL3009

ARG VARIANT=debian-12

# ===========================================================================
# Stage 1: build the prebuilt LLVM libraries once at image build time.
#
# build-scripts/build_llvm.py clones llvm-project (llvmorg-18.1.8) under
# /opt/core/deps/llvm, compiles it and repackages the result as an install
# tree (bin/include/lib/libexec/share). Here we keep only that tree at
# /opt/llvm and drop every intermediate output (source clone, build tree,
# CPack tarball). The final stage imports /opt/llvm via COPY --from, so
# every devcontainer built from this image has a prebuilt LLVM available
# without recompiling it during daily development.
#
# Cache invalidation: the builder stage is rebuilt only when the base image
# tag, LLVM_ARCH, or build-scripts/build_llvm.py (which pins the LLVM
# version/tag) changes - unrelated final-stage changes never rebuild LLVM.
# ===========================================================================
FROM mcr.microsoft.com/devcontainers/cpp:${VARIANT} AS llvm-builder

ARG DEBIAN_FRONTEND=noninteractive
ARG LLVM_ARCH=X86

# The base image already ships git/cmake/ninja/gcc/python3; only add the
# python "requests" dependency of build_llvm.py plus ccache.
RUN apt-get update \
  && apt-get install -y --no-install-recommends python3-requests ccache \
  && rm -rf /var/lib/apt/lists/*

# build_llvm.py resolves its deps dir as <script_dir>/../core/deps/llvm and
# expects it to already exist, so create /opt/core/deps upfront; the
# llvm-project source is then cloned under /opt/core/deps/llvm.
COPY build-scripts/build_llvm.py build-scripts/requirements.txt /opt/wamr-build/

# Keep only the repackaged install tree; afterwards drop the llvm-project
# source clone (/opt/core) and the copied scripts (/opt/wamr-build).
RUN mkdir -p /opt/core/deps \
  && python3 /opt/wamr-build/build_llvm.py --arch ${LLVM_ARCH} --use-ccache \
  && mv /opt/core/deps/llvm/build /opt/llvm \
  && rm -rf /opt/core /opt/wamr-build

# ===========================================================================
# Stage 2: the final devcontainer image
# ===========================================================================
FROM mcr.microsoft.com/devcontainers/cpp:${VARIANT}

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Shanghai

SHELL ["/bin/bash", "-o", "pipefail", "-c"]

RUN apt-get update \
  && apt-get upgrade -y

RUN apt-get update \
  && apt-get install -y apt-transport-https apt-utils build-essential \
      ca-certificates ccache curl file g++-multilib git gnupg \
      libgcc-12-dev lib32gcc-12-dev libzstd-dev lsb-release \
      ninja-build ocaml ocamlbuild opam \
      python3-venv python3-pip \
      software-properties-common tree tzdata \
      unzip valgrind vim wget zip --no-install-recommends

WORKDIR /opt

ARG WASI_SDK_VER=29
RUN wget -c --progress=dot:giga https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VER}/wasi-sdk-${WASI_SDK_VER}.0-x86_64-linux.tar.gz -P /tmp \
  && tar xf /tmp/wasi-sdk-${WASI_SDK_VER}.0-x86_64-linux.tar.gz -C /opt \
  && ln -sf /opt/wasi-sdk-${WASI_SDK_VER}.0-x86_64-linux /opt/wasi-sdk

ARG WABT_VER=1.0.37
RUN wget -c --progress=dot:giga https://github.com/WebAssembly/wabt/releases/download/${WABT_VER}/wabt-${WABT_VER}-ubuntu-20.04.tar.gz -P /tmp \
  && tar xf /tmp/wabt-${WABT_VER}-ubuntu-20.04.tar.gz -C /opt \
  && ln -sf /opt/wabt-${WABT_VER} /opt/wabt

# set path

# Install wasm-tools
ARG WASM_TOOLS_VER=1.243.0
RUN wget -c --progress=dot:giga https://github.com/bytecodealliance/wasm-tools/releases/download/v${WASM_TOOLS_VER}/wasm-tools-${WASM_TOOLS_VER}-x86_64-linux.tar.gz -P /tmp
RUN tar xzf /tmp/wasm-tools-${WASM_TOOLS_VER}-x86_64-linux.tar.gz -C /opt\
  && ln -sf /opt/wasm-tools-${WASM_TOOLS_VER}-x86_64-linux/wasm-tools /usr/local/bin/wasm-tools

# set proxy variables for sudoer
RUN echo 'Defaults env_keep += "http_proxy https_proxy no_proxy HTTP_PROXY HTTPS_PROXY NO_PROXY"' | tee /etc/sudoers.d/proxy

# Install uv for virtual environment management
RUN wget -qO- https://astral.sh/uv/install.sh | sh

# Install utilities for C/C++ development
RUN apt-get update \
  && apt-get install -y --no-install-recommends bear ccache shellcheck

# Install clang-format-21 from the official LLVM apt repository
# (Debian 12 default repositories do not provide clang-format-21)
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
  && echo "deb http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-21 main" > /etc/apt/sources.list.d/llvm.list \
  && apt-get update \
  && apt-get install -y --no-install-recommends clang-format-21 clang-tidy-21

# set path
RUN ln -sf /usr/bin/clang-format-21 /usr/bin/clang-format \
  && ln -sf /usr/bin/clang-tidy-21 /usr/bin/clang-tidy

# clean up
RUN apt-get autoremove -y \
  && apt-get clean -y \
  && rm -rf /var/lib/apt/lists/* \
  && rm -rf /tmp/*

# ship the prebuilt LLVM libraries built in the llvm-builder stage
COPY --from=llvm-builder /opt/llvm /opt/llvm

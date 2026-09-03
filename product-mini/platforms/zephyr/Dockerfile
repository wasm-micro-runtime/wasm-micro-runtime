# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# A T2 (star topology) Zephyr workspace with WAMR as a Zephyr module. The steps
# below mirror the "Workspace" section of ./README.md; keep the two in sync.
# The resulting layout is:
#
# /root/zephyrproject/          --> topdir (WEST_TOPDIR)
# ├─── .west/config
# ├─── zephyr/                  --> Zephyr source code
# ├─── zephyr-sdk/
# ├─── modules/
# │    └─── wasm-micro-runtime  --> WAMR source code
# └─── application/             --> DUMMY manifest repo, keeps west_lite.yml
#
# If you modify this file, you may need to sync the modifications to the
# .github/workflows/compilation_on_zephyr.yml
FROM ghcr.io/zephyrproject-rtos/ci-base:v0.26-branch

ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asian/Shanghai
ARG ZEPHYR_SDK_VERSION=0.16.9
# In west_lite.yml, the Zephyr version is set to v3.7.0
#ARG ZEPHYR_VERSION=3.7.0

# Install the Zephyr Software Development Kit (SDK) minimal version
WORKDIR /root/zephyrproject/zephyr-sdk
# hadolint ignore=DL4006
RUN wget --progress=dot:giga https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz \
    && wget --progress=dot:giga -O - https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/sha256.sum | shasum --check --ignore-missing \
    && tar --strip-components=1 -xf zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz && rm zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64_minimal.tar.xz
# hadolint ignore=DL4006
# Install arc/x86 tools, host tools and Register Zephyr SDK CMake package
# If you want to use other toolchains, please change the -t options
RUN ./setup.sh -t arc-zephyr-elf -t arc64-zephyr-elf -t x86_64-zephyr-elf -h -c

# Install west
# hadolint ignore=DL3013,DL3059
RUN pip3 install --no-cache-dir west

# The dummy manifest repository only carries the minimal west manifest
WORKDIR /root/zephyrproject/application
COPY ./west_lite.yml ./west_lite.yml

# Init the workspace from the minimal manifest, fetch Zephyr and export the
# Zephyr CMake package, then install the Zephyr python requirements
WORKDIR /root/zephyrproject
# hadolint ignore=DL3059
RUN west init -l --mf west_lite.yml application \
    && west update --stats \
    && west zephyr-export \
    && pip3 install --no-cache-dir -r zephyr/scripts/requirements.txt

# Install the wasi-sdk, used to (re)compile the WASM applications of the
# samples that rely on WASI
ARG WASI_SDK_VERSION=29
ENV WASI_SDK_PATH=/opt/wasi-sdk
# hadolint ignore=DL4006
RUN mkdir -p ${WASI_SDK_PATH} \
    && wget --progress=dot:giga -O - https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VERSION}/wasi-sdk-${WASI_SDK_VERSION}.0-x86_64-linux.tar.gz \
    | tar xz -C ${WASI_SDK_PATH} --strip-components=1

# WAMR is not baked into the image. Bind mount the local checkout at this path
# so that the sources being built are the ones in the working tree:
#   docker run -v <local-wamr>:/root/zephyrproject/modules/wasm-micro-runtime
RUN mkdir -p /root/zephyrproject/modules/wasm-micro-runtime
WORKDIR /root/zephyrproject/modules/wasm-micro-runtime/product-mini/platforms/zephyr

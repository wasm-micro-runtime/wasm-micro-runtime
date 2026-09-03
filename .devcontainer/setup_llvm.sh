#!/usr/bin/env bash
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Wire the prebuilt LLVM shipped in the image (/opt/llvm, built by
# build-scripts/build_llvm.py at image build time) into the workspace layout
# that WAMR's cmake expects (core/deps/llvm/build).
# core/deps/** is git-ignored, so this never pollutes the working tree.
#
# Idempotent: run once per container creation by devcontainer.json's
# postCreateCommand.

set -o errexit
set -o pipefail
set -o nounset

LLVM_LINK_TARGET="core/deps/llvm/build"

# A real directory or an existing symlink (e.g. the user built LLVM in the
# workspace manually) always wins: keep it untouched.
if [ -e "${LLVM_LINK_TARGET}" ] || [ -L "${LLVM_LINK_TARGET}" ]; then
  echo "--- ${LLVM_LINK_TARGET} already exists, keep it as is ---"
  exit 0
fi

if [ ! -d /opt/llvm ]; then
  echo "!!! /opt/llvm (prebuilt LLVM) not found in the image." >&2
  echo "!!! LLVM JIT/AOT builds will need a manual build first:" >&2
  echo "!!!   cd wamr-compiler && ./build_llvm.sh" >&2
  exit 0
fi

mkdir -p core/deps/llvm
ln -s /opt/llvm "${LLVM_LINK_TARGET}"
echo "--- linked core/deps/llvm/build -> /opt/llvm (prebuilt LLVM) ---"

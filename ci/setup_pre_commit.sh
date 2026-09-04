#!/bin/bash

# Copyright (C) 2023 Amazon.com Inc. or its affiliates. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# This script executes some commands to make your onboarding with WAMR easier.
# For example, setting pre-commit hook that will make your code complaint with the
# code style requirements checked in WAMR CI

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
wamr_root=$(git -C "$script_dir/.." rev-parse --show-toplevel)
hook_path=$(git -C "$wamr_root" rev-parse --git-path hooks/pre-commit)

echo "Copy the pre-commit hook to your hooks folder"
install -m 755 "$script_dir/pre_commit_hook_sample" "$hook_path"

# Feel free to propose your commands to this script to make developing WAMR easier

echo "Setup is done"

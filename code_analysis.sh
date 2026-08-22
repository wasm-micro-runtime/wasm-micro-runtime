#!/bin/sh

# Copyright (C) 2026 Airbus Defence and Space Romania SRL. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Prerequisites: clang-tidy and cppcheck (First time install: sudo apt-get update && sudo apt-get install -y clang-tidy cppcheck)
# Prerequisite for clang-tidy: CMAKE_EXPORT_COMPILE_COMMANDS=ON
rm -rf product-mini/platforms/linux/build && mkdir -p product-mini/platforms/linux/build
cd product-mini/platforms/linux/build
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make -j8
cd ../../../../

# Define the target folders
TARGET_DIRS="core/iwasm/interpreter \
             core/iwasm/common/component-model \
             core/iwasm/libraries/libc-wasi-p2 \
             tests/unit/canonical* \
             tests/unit/component* \
             tests/unit/libc-wasi-p2"
             
# Define the log report file
REPORT_FILE="code_analysis_report.log"
echo "--- Running code analysis for: $TARGET_DIRS"
echo "--- The analysis report will be written into $REPORT_FILE"

: > "$REPORT_FILE"
exec > "$REPORT_FILE" 2>&1

echo "--- Running Cppcheck ---"
cppcheck -q --force --enable=warning,performance,style,portability $TARGET_DIRS

echo "--- Running Clang-Tidy ---"
FILES=$(find $TARGET_DIRS -name "*.c")
HEADERS=$(find $TARGET_DIRS -name "*.h")
$(command -v /opt/wasi-sdk/bin/clang-tidy || echo clang-tidy) -p product-mini/platforms/linux/build --quiet $HEADERS $FILES

TOTAL=$(grep -c ': error:' "$REPORT_FILE" 2>/dev/null || echo 0)
printf "Total errors: $TOTAL"

for CHECK in \
    bugprone-narrowing-conversions \
    bugprone-multi-level-implicit-pointer-conversion \
    bugprone-null-dereference \
    bugprone-use-after-move \
    bugprone-sizeof-expression \
    clang-analyzer-core \
    clang-analyzer-security \
    clang-analyzer-deadcode \
    cppcoreguidelines-init-variables \
    cppcoreguidelines-narrowing-conversions \
    performance-no-int-to-ptr \
    readability-math-missing-parentheses \
    concurrency-mt-unsafe \
    cert-msc30-c \
    cert-err34-c \
    readability-redundant-casting; do
    COUNT=$(grep -c "$CHECK" "$REPORT_FILE" 2>/dev/null || echo 0)
    if [ "$COUNT" -gt 0 ]; then
        printf '  %-56s %d\n' "$CHECK:" "$COUNT" | tee -a "$REPORT_FILE"
    fi
done

echo "--- Analysis complete. Check  $REPORT_FILE ---"

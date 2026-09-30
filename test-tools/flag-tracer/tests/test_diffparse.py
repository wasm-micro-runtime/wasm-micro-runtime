# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import diffparse

DIFF = """\
diff --git a/core/iwasm/interpreter/wasm_loader.c b/core/iwasm/interpreter/wasm_loader.c
index 1111111..2222222 100644
--- a/core/iwasm/interpreter/wasm_loader.c
+++ b/core/iwasm/interpreter/wasm_loader.c
@@ -100,2 +100,3 @@ static void f(void)
 keep
-old line
+new line
+another
diff --git a/README.md b/README.md
--- a/README.md
+++ b/README.md
@@ -1 +1 @@
-a
+b
"""


def test_paths_and_kinds():
    files = diffparse.parse(DIFF)
    assert [f.path for f in files] == [
        "core/iwasm/interpreter/wasm_loader.c", "README.md"]
    assert [f.kind for f in files] == ["c", "other"]


def test_line_numbers():
    c = diffparse.parse(DIFF)[0]
    assert c.new_lines == [101, 102]
    assert c.old_lines == [101]


def test_cmake_kind():
    for path in ("build-scripts/config_common.cmake", "CMakeLists.txt",
                 "product-mini/CMakeLists.txt"):
        d = diffparse.FileDiff(path, path)
        assert d.kind == "cmake"


def test_changed_lines_are_captured():
    c = diffparse.parse(DIFF)[0]
    assert c.changed == ["old line", "new line", "another"]

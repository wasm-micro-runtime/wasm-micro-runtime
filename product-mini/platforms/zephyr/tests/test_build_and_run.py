# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[1] / "build_and_run.py"
SPEC = importlib.util.spec_from_file_location("build_and_run", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ResolveTestRootTest(unittest.TestCase):
    def test_help_describes_sample_and_test_roots(self):
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--help"],
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("sample or tests/... test root", result.stdout)
        self.assertIn("sample.yaml or testcase.yaml", result.stdout)

    def test_resolves_existing_sample(self):
        self.assertEqual(MODULE.resolve_test_root("simple"), Path("simple"))

    def test_resolves_nested_test_application(self):
        self.assertEqual(
            MODULE.resolve_test_root("tests/platform-api"),
            Path("tests/platform-api"),
        )

    def test_rejects_parent_traversal(self):
        with self.assertRaises(ValueError):
            MODULE.resolve_test_root("../simple")

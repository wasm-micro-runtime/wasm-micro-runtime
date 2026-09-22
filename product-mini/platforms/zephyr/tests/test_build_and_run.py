# Copyright (C) 2026 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

import importlib.util
import shlex
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).parents[1] / "build_and_run.py"
WORKFLOW = (
    Path(__file__).parents[4] / ".github/workflows/compilation_on_zephyr.yml"
)
SPEC = importlib.util.spec_from_file_location("build_and_run", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CoverageWorkflowTest(unittest.TestCase):
    def test_smoke_job_collects_native_coverage_and_runs_qemu_arc(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")
        smoke_job = workflow.split("  smoke_test:", 1)[1].split(
            "  required:", 1
        )[0]

        self.assertIn(
            "--no-docker --coverage --sim native_sim", smoke_job
        )
        self.assertIn("--no-docker --sim qemu_arc", smoke_job)
        self.assertIn(
            "github.event.repository.fork == true", smoke_job
        )
        self.assertNotIn("  coverage_measurement:", workflow)


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
        self.assertIn("--coverage", result.stdout)

    def test_resolves_existing_sample(self):
        self.assertEqual(MODULE.resolve_test_root("simple"), Path("simple"))

    def test_resolves_nested_test_application(self):
        self.assertEqual(
            MODULE.resolve_test_root("tests/platform-api"),
            Path("tests/platform-api"),
        )

    def test_resolves_no_root_to_all_of_them(self):
        self.assertIsNone(MODULE.resolve_test_root(None))
        self.assertEqual(
            MODULE.artifact_name(None, ["native_sim", "qemu_arc"]),
            "all-native_sim-qemu_arc",
        )

    def test_rejects_parent_traversal(self):
        with self.assertRaises(ValueError):
            MODULE.resolve_test_root("../simple")


class TwisterCommandTest(unittest.TestCase):
    def test_coverage_forwards_zephyr_3_7_gcovr_options(self):
        command = MODULE.twister_command(
            Path("tests/platform-api"), ["native_sim"], False, coverage=True
        )

        self.assertIn("--coverage", command.split())
        self.assertIn(f"--coverage-basedir {MODULE.WAMR_ROOT}", command)
        self.assertIn("--coverage-tool gcovr", command)
        self.assertIn("--coverage-formats html,xml", command)

    def test_no_docker_paths_remain_single_shell_arguments(self):
        checkout = Path("/tmp/WAMR checkout's coverage")
        with mock.patch.object(MODULE, "WAMR_ROOT", checkout):
            command = MODULE.twister_command(
                Path("tests/platform api"),
                ["native_sim"],
                False,
                coverage=True,
            )

        tokens = shlex.split(command)
        module_dir = str(checkout)
        platform_dir = f"{module_dir}/product-mini/platforms/zephyr"
        self.assertEqual(
            tokens[tokens.index("-T") + 1],
            "$topdir/application/tests/platform api",
        )
        self.assertIn(f"EXTRA_ZEPHYR_MODULES={module_dir}", tokens)
        self.assertEqual(
            tokens[tokens.index("--outdir") + 1],
            f"{platform_dir}/build/twister-tests-platform api-native_sim-coverage",
        )
        self.assertEqual(
            tokens[tokens.index("--coverage-basedir") + 1], module_dir
        )

    def test_docker_coverage_uses_container_paths(self):
        command = MODULE.twister_command(
            Path("tests/platform-api"), ["native_sim"], True, coverage=True
        )
        tokens = shlex.split(command)

        self.assertEqual(
            tokens[tokens.index("--coverage-basedir") + 1], MODULE.MODULE_DIR
        )
        self.assertEqual(
            tokens[tokens.index("--outdir") + 1],
            f"{MODULE.MODULE_DIR}/product-mini/platforms/zephyr/build/"
            "twister-tests-platform-api-native_sim-coverage",
        )

    def test_coverage_uses_a_separate_output_tree(self):
        coverage = MODULE.twister_command(
            Path("tests/platform-api"), ["native_sim"], False, coverage=True
        )
        plain = MODULE.twister_command(
            Path("tests/platform-api"), ["native_sim"], False, coverage=False
        )

        coverage_outdir = (
            f"--outdir {MODULE.HERE}/build/"
            "twister-tests-platform-api-native_sim-coverage"
        )
        plain_outdir = (
            f"--outdir {MODULE.HERE}/build/"
            "twister-tests-platform-api-native_sim"
        )
        self.assertIn(coverage_outdir, coverage)
        self.assertIn(plain_outdir, plain)
        self.assertNotIn(f"{plain_outdir}-coverage", plain)
        self.assertNotIn("--coverage", plain.split())

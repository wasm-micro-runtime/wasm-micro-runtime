#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Run the twister test scenarios of a WAMR Zephyr sample on a simulator.

Every sample carries a sample.yaml describing its twister scenarios, so the
pass/fail verdict comes from twister itself. By default twister runs inside the
Docker image described by the Dockerfile next to this script, against the local
checkout; with --no-docker it runs in the current environment instead, which is
what CI does inside the Zephyr container."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WAMR_ROOT = HERE.parents[2]
LOG_DIR = HERE / "build" / "logs"
IMAGE = "wamr-zephyr"
MODULE_DIR = "/root/zephyrproject/modules/wasm-micro-runtime"
ZEPHYR_PLATFORM_DIR = f"{MODULE_DIR}/product-mini/platforms/zephyr"
TIMEOUT_SECONDS = 30

# twister platform identifier per simulator
BOARDS = {
    "native_sim": "native_sim",
    "qemu_arc": "qemu_arc/qemu_arc_hs",
}

EPILOG = """\
output:
  The Docker image build only shows progress on the console; its full output is
  written to build/logs/docker-build.log.

  twister runs with its output on both the console and
  build/logs/<sample>-<sim>.log, and its own exit status decides whether the
  sample passed: each scenario in the sample's sample.yaml declares the console
  output that a successful run must produce.

  twister keeps its build trees and reports under build/twister-<sample>-<sim>/.
  Everything under build/ is created by the container and therefore owned by
  root.
"""


def tail(log_path, lines=15):
    content = log_path.read_text(errors="replace").splitlines()
    return "\n".join(f"  | {line}" for line in content[-lines:])


def report(succeeded, step, log_path, note=""):
    print(("    ok" + note) if succeeded else "    FAILED")
    if not succeeded:
        print(f"    log: {log_path}")
    return succeeded


def run_logged(argv, log_path, step):
    """Run argv, sending its output to log_path only."""
    print(f"--> {step} ...", flush=True)
    with log_path.open("a") as log:
        log.write(f"\n$ {' '.join(argv)}\n")
        log.flush()
        completed = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT)

    return report(completed.returncode == 0, step, log_path)


def run_streamed(argv, log_path, step, note_of=None):
    """Run argv, echoing its output to both the console and log_path. note_of
    is called on success to annotate the result line."""
    print(f"--> {step} ...", flush=True)
    with log_path.open("a") as log:
        log.write(f"\n$ {' '.join(argv)}\n")
        process = subprocess.Popen(
            argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        for line in process.stdout:
            print(f"  | {line}", end="", flush=True)
            log.write(line)
        returncode = process.wait()

    succeeded = returncode == 0
    note = note_of() if succeeded and note_of else ""
    return report(succeeded, step, log_path, note)


def build_image():
    log_path = LOG_DIR / "docker-build.log"
    log_path.unlink(missing_ok=True)
    return run_logged(
        ["docker", "build", "-t", IMAGE, str(HERE)],
        log_path,
        f"building Docker image {IMAGE} (this takes a while)",
    )


def image_exists():
    return (
        subprocess.run(
            ["docker", "image", "inspect", IMAGE],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        == 0
    )


def run_sample(sample, simulator, use_docker):
    """Run the twister scenarios of one sample on one simulator."""
    platform = BOARDS[simulator]
    log_path = LOG_DIR / f"{sample}-{simulator}.log"
    log_path.unlink(missing_ok=True)

    # paths as seen by the shell running twister: inside the container when
    # dockerized, in the checkout itself otherwise
    module_dir = MODULE_DIR if use_docker else str(WAMR_ROOT)
    platform_dir = f"{module_dir}/product-mini/platforms/zephyr"
    outdir = f"{platform_dir}/build/twister-{sample}-{simulator}"

    command = (
        f"west twister -T {platform_dir}/{sample} -p {platform}"
        f" -x EXTRA_ZEPHYR_MODULES={module_dir}"
        f" --outdir {outdir} --inline-logs --clobber-output"
        # twister compiles with -Werror by default; the runtime is not built
        # with that in any other configuration
        f" --disable-warnings-as-errors"
        # build-scripts/version.cmake generates core/version.h inside the
        # source tree, so parallel configurations of the same checkout race
        f" --jobs 1"
    )

    if not use_docker:
        argv = ["bash", "-euo", "pipefail", "-c", command]
    else:
        argv = [
            "docker",
            "run",
            "--rm",
            "-v",
            # native path on the host side, posix path on the container side
            f"{WAMR_ROOT}:{MODULE_DIR}",
            "-w",
            platform_dir,
            IMAGE,
            "bash",
            "-euo",
            "pipefail",
            "-c",
            command,
        ]

    def note():
        """twister reports a build_only scenario as passed without running it,
        so say which of the two happened."""
        report_path = (
            LOG_DIR.parent / f"twister-{sample}-{simulator}" / "twister.json"
        )
        try:
            suites = json.loads(report_path.read_text())["testsuites"]
        except (OSError, KeyError, ValueError):
            return ""

        if suites and not any(suite.get("runnable") for suite in suites):
            return " (build only)"
        return ""

    return run_streamed(
        argv, log_path, f"{sample} on {simulator} ({platform})", note
    )


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "sample",
        nargs="?",
        default="simple",
        help="sample directory name (default: simple)",
    )
    parser.add_argument(
        "--build",
        action="store_true",
        help=f"build the {IMAGE} Docker image and exit",
    )
    parser.add_argument(
        "--no-docker",
        action="store_true",
        help="run west in the current environment instead of in the container",
    )
    parser.add_argument(
        "--sim",
        choices=sorted(BOARDS),
        action="append",
        dest="simulators",
        help="simulator to run on, repeatable (default: native_sim)",
    )
    args = parser.parse_args()

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    if args.build:
        return 0 if build_image() else 1

    # accept "simple", "./simple" and "simple/" alike
    sample = Path(args.sample).name
    if not (HERE / sample / "sample.yaml").is_file():
        parser.error(f"unknown sample: {args.sample}")

    use_docker = not args.no_docker
    if use_docker and not image_exists() and not build_image():
        return 1

    for simulator in args.simulators or ["native_sim"]:
        if not run_sample(sample, simulator, use_docker):
            return 1

    print("all done")
    return 0


if __name__ == "__main__":
    sys.exit(main())

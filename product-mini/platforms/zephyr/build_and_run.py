#!/usr/bin/env python3
#
# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Run the twister test scenarios of the WAMR Zephyr samples on the simulators.

Every sample carries a sample.yaml describing its twister scenarios, so the
pass/fail verdict comes from twister itself. By default twister runs inside the
Docker image described by the Dockerfile next to this script, against the local
checkout; with --no-docker it runs in the current environment instead, which is
what CI does inside the Zephyr container - and which therefore expects to be run
from within a Zephyr workspace."""

import argparse
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WAMR_ROOT = HERE.parents[2]
LOG_DIR = HERE / "build" / "logs"
IMAGE = "wamr-zephyr"
TOPDIR = "/root/zephyrproject"
MODULE_DIR = f"{TOPDIR}/modules/wasm-micro-runtime"
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
  build/logs/<sample>-<sims>.log, and its own exit status decides whether the
  samples passed: each scenario in a sample's sample.yaml declares the console
  output that a successful run must produce.

  Nothing here maps samples to platforms: twister gets every platform asked for
  and filters each scenario against the sample.yaml that declares it. Asking for
  a --sim no sample allows is therefore not an error, just a SKIPPED result
  line; "ok (build only)" means a scenario was built but never run.

  twister keeps its build trees and reports under build/twister-<sample>-<sims>/.
  Everything under build/ is created by the container and therefore owned by
  root.

  The applications are built from <topdir>/application, a mirror of this
  directory that is refreshed on every run, so the workspace looks the way a
  user's does. Only the runtime is attached from the outside, with
  EXTRA_ZEPHYR_MODULES, because it has to be your working tree rather than the
  clone west.yml would fetch.
"""


def tail(log_path, lines=15):
    content = log_path.read_text(errors="replace").splitlines()
    return "\n".join(f"  | {line}" for line in content[-lines:])


def report(succeeded, step, log_path, verdict="ok"):
    print(f"    {verdict}" if succeeded else "    FAILED")
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


def run_streamed(argv, log_path, step, verdict_of=None):
    """Run argv, echoing its output to both the console and log_path. verdict_of
    is called on success to word the result line."""
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
    verdict = verdict_of() if succeeded and verdict_of else "ok"
    return report(succeeded, step, log_path, verdict)


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


def run_sample(sample, simulators, use_docker):
    """Run the twister scenarios of a sample, or of every sample when sample is
    None, on the given simulators.

    Nothing here says which sample may run on which platform: twister is handed
    all of them and filters against each sample.yaml."""
    tag = f"{sample or 'all'}-{'-'.join(simulators)}"
    log_path = LOG_DIR / f"{tag}.log"
    log_path.unlink(missing_ok=True)

    # paths as seen by the shell running twister: inside the container when
    # dockerized, in the checkout itself otherwise
    module_dir = MODULE_DIR if use_docker else str(WAMR_ROOT)
    platform_dir = f"{module_dir}/product-mini/platforms/zephyr"
    outdir = f"{platform_dir}/build/twister-{tag}"
    platforms = " ".join(f"-p {BOARDS[name]}" for name in simulators)

    command = (
        # The workspace already exists - built by the Dockerfile, or by CI -
        # but its application/ is whatever it was then. Mirror this directory
        # into it so the applications under test are the ones in the working
        # tree, exactly as a user's application/ would hold them.
        'topdir=$(west topdir)'
        f" && rsync -a --delete --exclude build --exclude __pycache__"
        f' {platform_dir}/ "$topdir/application/"'
        f' && cd "$topdir"'
        f' && west twister -T "$topdir/application/{sample or ""}" {platforms}'
        # west.yml lists WAMR, but manifest.project-filter marks it inactive so
        # that west update leaves the checkout alone; hand the build the
        # working tree instead
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
            # the workspace, so that `west topdir` resolves
            "-w",
            TOPDIR,
            IMAGE,
            "bash",
            "-euo",
            "pipefail",
            "-c",
            command,
        ]

    def verdict():
        """twister exits 0 in three quite different situations: a scenario ran
        and passed, a build_only scenario was built but never run, and every
        scenario was filtered out because no sample.yaml allows the platform.
        Say which one happened."""
        report_path = LOG_DIR.parent / f"twister-{tag}" / "twister.json"
        try:
            suites = json.loads(report_path.read_text())["testsuites"]
        except (OSError, KeyError, ValueError):
            return "ok"

        ran = [
            suite for suite in suites if suite.get("status") != "filtered"
        ]
        if not ran:
            return "SKIPPED (no scenario declares these platforms)"
        if not any(suite.get("runnable") for suite in ran):
            return "ok (build only)"
        if len(ran) < len(suites):
            return f"ok ({len(ran)} of {len(suites)} scenarios, rest filtered)"
        return "ok"

    return run_streamed(
        argv,
        log_path,
        f"{sample or 'every sample'} on {', '.join(simulators)}",
        verdict,
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
        help="sample directory name; every sample by default",
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
        help="simulator to run on, repeatable; all of them by default,"
        " since each sample.yaml declares the ones its sample allows",
    )
    args = parser.parse_args()

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    if args.build:
        return 0 if build_image() else 1

    sample = None
    if args.sample is not None:
        # accept "simple", "./simple" and "simple/" alike
        sample = Path(args.sample).name
        if not (HERE / sample / "sample.yaml").is_file():
            parser.error(f"unknown sample: {args.sample}")

    use_docker = not args.no_docker
    if use_docker and not image_exists() and not build_image():
        return 1

    if not run_sample(sample, args.simulators or sorted(BOARDS), use_docker):
        return 1

    print("all done")
    return 0


if __name__ == "__main__":
    sys.exit(main())

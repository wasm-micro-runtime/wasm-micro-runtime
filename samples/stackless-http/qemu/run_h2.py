#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Run HTTP/2 frames through the bounded Cortex-M33 server and record SRAM."""
import argparse
import hashlib
import json
from pathlib import Path

from run import ram, run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('--qemu', default='qemu-system-arm')
    args = p.parse_args()
    elf = args.elf.resolve()
    result = run(args.qemu, '-M', 'mps2-an505', '-nographic', '-monitor', 'none',
                 '-serial', 'null', '-net', 'none', '-icount', 'shift=0,sleep=off',
                 '-kernel', elf, '-semihosting-config', 'enable=on,target=native')
    row = next(line for line in (result.stdout + result.stderr).splitlines()
               if line.startswith('H2 M33 PASS '))
    counters = {k: int(v) for k, v in (word.split('=') for word in row.split()[3:])}
    assert counters['completed'] == counters['peak_streams'] == 4
    assert counters['input_peak'] <= 2048 and counters['libc_peak'] <= 65536
    assert counters['pool_peak'] <= 131072
    print(json.dumps({
        'qemu': run(args.qemu, '--version').stdout.splitlines()[0],
        'machine': 'mps2-an505',
        'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
        'memory': ram(elf, 8192, 65536),
        'workload': counters,
    }, indent=2))


if __name__ == '__main__':
    main()

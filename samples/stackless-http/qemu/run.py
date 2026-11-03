#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Run the bounded M33 workload and count executed guest instructions.

QEMU single-instruction translation blocks provide instruction counts, not
hardware cycles. The timed interrupt advances virtual time while WFI sleeps.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def run(*args):
    return subprocess.run(list(map(str, args)), check=True, capture_output=True,
                          text=True, timeout=60)


def ram(elf):
    sections = {}
    for line in run('arm-none-eabi-size', '-A', elf).stdout.splitlines():
        words = line.split()
        if len(words) != 3 or not words[0].startswith('.'):
            continue
        name, size, address = words[0], int(words[1]), int(words[2])
        if size and (0x20000000 <= address < 0x20082000 or 0x38000000 <= address < 0x38082000):
            sections[name] = size
    used = sum(sections.values())
    if (not 131072 < used < 520 * 1024 or sections.get('.heap') != 16384
            or sections.get('.stack', sections.get('.stack_dummy')) != 4096):
        raise RuntimeError(f'bad Cortex-M33 SRAM accounting: {sections}')
    return {'sram_reserved_bytes': used, 'sram_limit_bytes': 520 * 1024, 'sram_sections': sections}


def result(run_result):
    output = run_result.stdout + run_result.stderr
    row = next(line for line in output.splitlines() if line.startswith('M33 PASS '))
    print(row)
    return {key: int(value) for key, value in (item.split('=') for item in row.split()[2:])}


def instructions(elf, trace):
    names = {'native_begin', 'native_end', 'idle_begin', 'idle_end'}
    markers = {}
    for line in run('arm-none-eabi-nm', '-n', elf).stdout.splitlines():
        words = line.split()
        if len(words) == 3 and words[2] in names:
            markers[int(words[0], 16)] = words[2]
    if set(markers.values()) != names:
        raise RuntimeError('missing instruction count markers')
    state = None
    counts = {}
    line_pattern = re.compile(r'^Trace \d+: .*?\[[0-9a-f]+/([0-9a-f]+)/', re.I)
    with trace.open() as file:
        for line in file:
            match = line_pattern.search(line)
            if not match:
                continue
            marker = markers.get(int(match[1], 16))
            if marker and marker.endswith('_begin'):
                if state or marker.split('_')[0] in counts:
                    raise RuntimeError('unexpected repeated trace marker')
                state = marker.split('_')[0]
                counts[state] = 0
            elif marker and marker.endswith('_end'):
                if state != marker.split('_')[0]:
                    raise RuntimeError('unpaired trace marker')
                state = None
            elif state:
                counts[state] += 1
    if state or set(counts) != {'idle', 'native'} or not 0 < counts['idle'] < 100 or not counts['native']:
        raise RuntimeError(f'bad instruction trace: {counts}')
    return counts


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf', type=Path)
    p.add_argument('--pico-elf', type=Path)
    p.add_argument('--qemu', default='qemu-system-arm')
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    command = [a.qemu, '-M', 'mps2-an505', '-nographic', '-monitor', 'none', '-serial', 'null',
               '-net', 'none', '-icount', 'shift=0,sleep=off', '-kernel', a.elf.resolve()]
    test = result(run(*command, '-semihosting-config', 'enable=on,target=native'))
    if (test['completed'], test['cancelled'], test['disconnected'], test['live']) != (34, 1, 1, 0):
        raise RuntimeError(f'incomplete simulated HTTP workload: {test}')
    trace = a.output / 'instructions.log'
    count = result(run(*command, '-semihosting-config', 'enable=on,target=native,arg=count',
                       '-accel', 'tcg,one-insn-per-tb=on', '-d', 'exec,nochain', '-D', trace))
    if count['completed'] or count['idle_wakes'] != 1:
        raise RuntimeError('instruction-only mode did not run')
    ops = instructions(a.elf, trace)
    report = {'qemu': run(a.qemu, '--version').stdout.splitlines()[0], 'machine': 'mps2-an505',
              'elf_sha256': hashlib.sha256(a.elf.read_bytes()).hexdigest(), 'simulated_http': test,
              'm33_link': ram(a.elf), 'instruction_counts': ops,
              'instructions_per_context_get_import': ops['native'] / count['native_imports']}
    if a.pico_elf:
        report['pico_link'] = ram(a.pico_elf)
        report['pico_link']['elf_sha256'] = hashlib.sha256(a.pico_elf.read_bytes()).hexdigest()
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

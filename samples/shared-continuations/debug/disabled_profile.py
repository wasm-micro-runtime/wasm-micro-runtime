# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compare macOS LTO/whole-program fixture builds; collect paired, serial samples."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import statistics
import struct
import subprocess


def sections(path):
    data = path.read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        raise ValueError('this receipt reader requires little-endian Mach-O 64')
    count = struct.unpack_from('<I', data, 16)[0]
    position = 32
    result = {}
    for _ in range(count):
        command, size = struct.unpack_from('<II', data, position)
        if command == 0x19:
            nsects = struct.unpack_from('<I', data, position+64)[0]
            for i in range(nsects):
                offset = position+72+80*i
                name = data[offset:offset+16].split(b'\0')[0].decode()
                segment = data[offset+16:offset+32].split(b'\0')[0].decode()
                length, file_offset = struct.unpack_from('<QI', data, offset+40)
                flags = struct.unpack_from('<I', data, offset+64)[0]
                zero = flags & 0xff in (1, 0xc, 0x12)
                result[segment+'/'+name] = {'size': length, 'sha256': None if zero else hashlib.sha256(data[file_offset:file_offset+length]).hexdigest()}
        position += size
    return result


def hot_functions(path):
    # Compare the complete disassembly blocks, including instruction bytes, for
    # both ordinary native dispatch and continuation resumption. Rust LTO may
    # outline a cold constructor cleanup differently after adding an export.
    tool = os.environ.get('LLVM_OBJDUMP', 'llvm-objdump')
    assembly = subprocess.check_output([tool, '--macho', '--disassemble', str(path)], text=True)
    names = ['_wasm_func_call', '_wasm_ext_call_resume']
    result = {}
    for name in names:
        match = re.search(r'^'+name+r':\n((?:[^\n]*\n)*?)(?=^_[^\n]*:$|\Z)', assembly, re.M)
        if match:
            result[name] = hashlib.sha256(match[1].encode()).hexdigest()
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('builds', type=Path)
    p.add_argument('--fixture', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--samples', type=int, default=9)
    a = p.parse_args()
    report = {}
    env = os.environ.copy()
    env.pop('WASM_EXT_TEST_ONLY', None)
    for runtime in ['wamr', 'tinywasm']:
        for feature in ['on', 'off']:
            key = runtime+'_'+feature
            paths = {phase: a.builds/(phase+'_'+key)/'fixture' for phase in ['base', 'after']}
            sec = {phase: sections(path) for phase, path in paths.items()}
            entry = {'binary_bytes': {phase: path.stat().st_size for phase, path in paths.items()},
                'sha256': {phase: hashlib.sha256(path.read_bytes()).hexdigest() for phase, path in paths.items()},
                'section_sizes_identical': {k:v['size'] for k,v in sec['base'].items()} == {k:v['size'] for k,v in sec['after'].items()},
                'text_identical': sec['base']['__TEXT/__text'] == sec['after']['__TEXT/__text'],
                'sections': sec, 'hot_function_disassembly': {phase: hot_functions(path) for phase,path in paths.items()}, 'samples': {'base': [], 'after': []}}
            if not entry['section_sizes_identical'] or len(set(entry['binary_bytes'].values())) != 1 or entry['hot_function_disassembly']['base'] != entry['hot_function_disassembly']['after']:
                raise RuntimeError('disabled build changed code or memory footprint: '+key)
            for i in range(a.samples):
                for phase in (['base','after'] if i%2==0 else ['after','base']):
                    output = subprocess.check_output([str(paths[phase]), str(a.fixture)], text=True, env=env)
                    sample = {}
                    for line in output.splitlines():
                        fields = line.split()
                        if len(fields)==2:
                            try: sample[fields[0]]=float(fields[1])
                            except ValueError: pass
                    entry['samples'][phase].append(sample)
            entry['medians'] = {phase: {k:statistics.median(x[k] for x in samples) for k in samples[0]} for phase,samples in entry['samples'].items()}
            report[key] = entry
            a.output.write_text(json.dumps(report, indent=2)+'\n')
            print(key, entry['medians'], flush=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')

if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Compile the freestanding HTTP handler, validate its component and link it."""
import argparse
from pathlib import Path
import subprocess
from component import emit, lower, parse

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--clang', default='clang')
p.add_argument('--wasm-tools', default='wasm-tools')
p.add_argument('--validator', required=True)
a = p.parse_args()
here = Path(__file__).resolve().parent
a.output.mkdir(parents=True, exist_ok=True)
core = a.output / 'guest.wasm'
subprocess.run([a.clang, '--target=wasm32', '-O2', '-fno-builtin', '-nostdlib',
                '-Wl,--no-entry,--import-memory,--initial-memory=65536,--max-memory=65536,-z,stack-size=4096,--strip-all',
                here / 'guest.c', '-o', core], check=True)
module = parse(subprocess.check_output([a.wasm_tools, 'print', core], text=True))[0]
fields = [['core', 'module', '$Memory', ['memory', ['export', '"mem"'], '1', '1']],
          ['core', 'instance', '$mem', ['instantiate', '$Memory']],
          ['core', 'module', '$Guest', *module[1:]],
          ['type', '$Stream', ['stream', 'u8']], ['type', '$Future', ['future', 'u8']],
          ['type', '$Result', ['tuple', '$Stream', '$Future']]]
imports = []
for decl in module[1:]:
    if not isinstance(decl, list) or decl[0] != 'import':
        continue
    assert decl[1] == '"env"'
    name = decl[2][1:-1]
    if decl[3][0] == 'memory':
        imports.append(['export', decl[2], ['memory', '$mem', '"mem"']])
        continue
    ident = '$' + name
    canon = ['canon', name]
    if name.startswith(('stream.', 'future.')):
        canon.append('$Stream' if name.startswith('stream.') else '$Future')
    if name.endswith(('.read', '.write', '.cancel-read', '.cancel-write')):
        canon.append('async')
    if name.endswith(('.read', '.write')):
        canon.append(['memory', ['core', 'memory', '$mem', '"mem"']])
    if name == 'task.return':
        canon.append(['result', '$Result'])
    if name.startswith('context.'):
        canon.extend(['i32', '0'])
    canon.append(['core', 'func', ident])
    fields.append(canon)
    imports.append(['export', decl[2], ['func', ident]])
fields.extend([['core', 'instance', '$h', ['instantiate', '$Guest', ['with', '"env"', ['instance', *imports]]]],
               ['func', ['export', '"handle"'], 'async', ['param', '"slot"', 'u32'],
                ['param', '"request"', '$Stream'], ['result', '$Result'],
                ['canon', 'lift', ['core', 'func', '$h', '"handle"'], 'async',
                 ['callback', ['core', 'func', '$h', '"handle-event"']]]]])
component = ['component', *fields]
wat = a.output / 'handler.component.wat'
wat.write_text(emit(component))
subprocess.run([a.validator, 'parse', wat, '-o', a.output / 'handler.component.wasm'], check=True)
core, _ = lower(component)
wat = a.output / 'handler.linked.wat'
wat.write_text(emit(core))
binary = a.output / 'handler.linked.wasm'
subprocess.run([a.validator, 'parse', wat, '-o', binary], check=True)
data = binary.read_bytes()
(a.output / 'handler.h').write_text('static const unsigned char handler_wasm[] = {\n' +
    '\n'.join('    ' + ', '.join(f'0x{v:02x}' for v in data[i:i+16]) + ',' for i in range(0, len(data), 16)) + '\n};\n')

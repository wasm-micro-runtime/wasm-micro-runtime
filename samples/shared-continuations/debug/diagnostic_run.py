# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Verify mixed native/guest/async traces in debuggers and Apple reports."""
import argparse
import hashlib
import shutil
import sys
import uuid
import struct
import json
import os
from pathlib import Path
import platform
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--runtime', choices=['wamr', 'tinywasm'], required=True)
p.add_argument('--include', type=Path, required=True)
p.add_argument('--library', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--debugger', choices=['lldb', 'gdb'], required=True)
p.add_argument('--swift', action='store_true')
p.add_argument('--apple-crash', action='store_true', help='intentionally crash the fixture and verify an Apple .ips report')
p.add_argument('--guest', type=Path)
p.add_argument('--sanitize', action='store_true')
p.add_argument('--skip-debugger', action='store_true')

a = p.parse_args()
source = Path(__file__).resolve().parent
build = a.build.resolve()
build.mkdir(parents=True, exist_ok=True)
guest = (a.guest or source/'source_guest.wasm').resolve()
mac = platform.system() == 'Darwin'
if a.swift and not mac:
    p.error('this fixture uses the macOS Swift toolchain')
if a.apple_crash and (not mac or a.sanitize):
    p.error('--apple-crash requires macOS and the continuation fixture')

def run(cmd, **kwargs):
    print(' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, timeout=300, **kwargs)

cc = os.environ.get('CC', 'cc')
cxx = os.environ.get('CXX', 'c++')
flags = ['-g', '-O2', '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror']
defines = ['-DDIAGNOSTIC_NATIVE'] + (['-DDIAGNOSTIC_SWIFT'] if a.swift else [])
if a.sanitize:
    flags += ['-fsanitize=address']
run([cc, *flags, *defines, '-I'+str(a.include.resolve()), '-c', source/'diagnostic.c', '-o', build/'native.o'])
run([cc, *flags, '-I'+str(a.include.resolve()), '-c', source/'recorder.c', '-o', build/'recorder.o'])
run([cxx, *flags, '-DDIAGNOSTIC_ERRORS', '-c', source/'native.cpp', '-o', build/'cpp.o'])
run([os.environ.get('RUSTC', 'rustc'), '--cfg=diagnostic_errors', '--crate-type=lib', '--emit=obj', '--edition=2024',
     '-Copt-level=2', '-Cdebuginfo=2', '-Cpanic=abort', '-Cforce-frame-pointers=yes',
     source/'native.rs', '-o', build/'rust.o'])
if a.swift:
    linker = ['xcrun', 'swiftc', '-module-cache-path', build/'swiftcache']
    run([*linker, '-D', 'DIAGNOSTIC_ERRORS', '-g', '-O', '-parse-as-library', '-emit-object', source/'native.swift', '-o', build/'bridge.o'])
else:
    linker = [cxx]
    run([cc, *flags, '-c', source/'bridge.c', '-o', build/'bridge.o'])
exe = build/(a.runtime+'_diagnostic')
libs = ['-framework', 'Security', '-framework', 'CoreFoundation', '-liconv'] if mac else ['-lm', '-lpthread', '-ldl']
run([*linker, '-g', build/'native.o', build/'recorder.o', build/'cpp.o', build/'rust.o', build/'bridge.o',
     a.library.resolve(), '-o', exe, *libs, *(['-lc++'] if a.swift else []), *(['-fsanitize=address'] if a.sanitize else []),
     *(['-Xlinker', '-object_path_lto', '-Xlinker', build/'linked_lto.o'] if mac else [])])
if mac:
    run(['xcrun', 'dsymutil', exe])
wasm_tools = shutil.which('wasm-tools')
if not wasm_tools:
    raise RuntimeError('wasm-tools is required for original DWARF source mappings')
manifest = {'session': str(uuid.uuid4()), 'executable_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'modules': {'0': {'path': str(guest), 'sha256': hashlib.sha256(guest.read_bytes()).hexdigest(),
    'sources': [{'path': str(source/'source_guest.c'), 'sha256': hashlib.sha256((source/'source_guest.c').read_bytes()).hexdigest()}]}},
    'wasm_tools': wasm_tools}
manifest_path = build/'manifest.json'
manifest_path.write_text(json.dumps(manifest, indent=2)+'\n')
os.environ['WASM_DEBUG_MANIFEST'] = str(manifest_path)
os.environ['WASM_DEBUG_MANIFEST_SHA256'] = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
source_lines = (source/'source_guest.c').read_text().splitlines()
expected_locations = ['source_guest.c:'+str(next(i for i,line in enumerate(source_lines, 1) if text in line))+':'
    for text in ['int result = native_call(value)', 'return guest_leaf(value) + 1', 'return guest_middle(value) + 1']]
normal = run([exe, guest], capture_output=True, text=True)
(build/'tests.txt').write_text(normal.stdout)
expected = ['native_checkpoint', 'nativeSwift' if a.swift else 'native_bridge', 'native_rust', 'native_cpp',
            'diagnostic_import', 'wasm_ext_call_resume']
receipt = {'runtime': a.runtime, 'debugger': a.debugger, 'platform': platform.platform(),
           'swift': a.swift, 'guest_sha256': manifest['modules']['0']['sha256'], 'tests': normal.stdout}
if not a.skip_debugger:
    if a.debugger == 'lldb':
        commands = ['command script import '+str(source/'debugger.py'),
            'breakpoint set -n native_checkpoint', 'breakpoint set -n parked_checkpoint',
            'breakpoint set -n failure_checkpoint', 'run', 'bt 32', 'wasm-bt',
            'breakpoint disable 1', 'continue', 'bt 32', 'wasm-bt', 'continue', 'bt 32', 'wasm-bt', 'continue']
        cmd = ['lldb', '--batch', *[x for c in commands for x in ['-o', c]], '--', exe, guest]
    else:
        commands = ['set pagination off', 'source '+str(source/'debugger.py'),
            'break native_checkpoint', 'break parked_checkpoint', 'break failure_checkpoint',
            'run', 'bt 32', 'wasm-bt', 'disable 1', 'continue', 'bt 32', 'wasm-bt', 'continue', 'bt 32', 'wasm-bt', 'continue']
        cmd = ['gdb', '--batch', *[x for c in commands for x in ['-ex', c]], '--args', exe, guest]
    result = run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (build/'debugger.txt').write_text(result.stdout)
    frames = '\n'.join(line for line in result.stdout.splitlines() if ('frame #' in line or line.startswith('#')))
    position = 0
    for symbol in expected:
        position = frames.find(symbol, position)
        if position < 0:
            raise RuntimeError('missing or misordered native frame: '+symbol)
        position += len(symbol)
    for text in ['guest_leaf', 'guest_middle', 'guest_root', *expected_locations,
                 'state=host', 'state=pending', 'state=async-error', 'async parent task=1', 'exception: native async failure']:
        if text not in result.stdout:
            raise RuntimeError('missing mixed trace evidence: '+text+'; see debugger.txt')
    receipt['mixed_debugger_trace'] = True

if a.apple_crash:
    import symbolize
    reports = Path.home()/'Library/Logs/DiagnosticReports'
    receipt['apple_crashes'] = []
    for mode in ['crash', 'async_crash']:
        sidecar = build/(mode+'.snapshot')
        sidecar.unlink(missing_ok=True)
        before = set(reports.glob(exe.name+'*.ips'))
        env = os.environ | {'WASM_DEBUG_SIDECAR': str(sidecar)}
        crashed = subprocess.run([str(exe), str(guest), mode], env=env, capture_output=True, timeout=30)
        if crashed.returncode != -5:
            raise RuntimeError('expected SIGTRAP from the deliberate native crash: '+repr(crashed.stderr))
        deadline = time.monotonic()+30
        while time.monotonic() < deadline:
            found = set(reports.glob(exe.name+'*.ips'))-before
            if found:
                break
            time.sleep(0.25)
        else:
            raise RuntimeError('Apple did not produce an .ips report within 30 seconds')
        report = json.loads(max(found, key=lambda x:x.stat().st_mtime).read_text().split('\n', 1)[1])
        frames = next(t for t in report['threads'] if t.get('triggered'))['frames']
        symbols = [f.get('symbol', '') for f in frames]
        position = 0
        for symbol in expected if mode == 'crash' else ['failure_checkpoint', 'main']:
            matches = [i for i in range(position, len(symbols)) if symbol in symbols[i]]
            if not matches:
                raise RuntimeError('Apple report is missing native frame: '+symbol)
            position = matches[0]+1
        image = report['usedImages'][frames[0]['imageIndex']]
        dsym = Path(str(exe)+'.dSYM')/'Contents/Resources/DWARF'/exe.name
        uuid = subprocess.check_output(['xcrun', 'dwarfdump', '--uuid', str(dsym)], text=True).split()[1]
        if uuid.lower() != image['uuid'].lower():
            raise RuntimeError('crash report and dSYM UUID mismatch')
        native_frames = [f for f in frames if f['imageIndex'] == frames[0]['imageIndex']]
        native = subprocess.check_output(['xcrun', 'atos', '-o', str(dsym), '-l', hex(image['base']),
            *[hex(image['base']+f['imageOffset']) for f in native_frames]], text=True)
        if struct.unpack_from('<I', sidecar.read_bytes(), 16)[0] != report['pid']:
            raise RuntimeError('Apple report and sidecar process mismatch')
        logical = symbolize.render(sidecar.read_bytes(), manifest_path)
        for text in ['guest_leaf', 'guest_middle', 'guest_root', *expected_locations]:
            if text not in logical:
                raise RuntimeError('Apple sidecar source mapping missing: '+text)
        if mode == 'async_crash' and ('async parent task=1' not in logical or 'exception: native async failure' not in logical):
            raise RuntimeError('Apple sidecar lost async ancestry/failure')
        (build/(mode+'_combined.txt')).write_text('Apple native trace (dSYM '+uuid+')\n'+native+'\nLogical guest/async trace\n'+logical)
        receipt['apple_crashes'].append({'mode': mode, 'exception': report['exception'], 'binary_uuid': uuid,
            'native_symbols': symbols, 'dsym_uuid_matches': True, 'guest_async_trace': logical})
(build/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print('PASS bounded logical guest/source/async records with native unwind evidence')

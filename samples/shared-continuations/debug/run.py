# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Exercise native unwinding; guest/async trace limitations are recorded separately."""
import argparse
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
p.add_argument('--ordinary', action='store_true', help='ordinary calls, including WAMR AOT')
a = p.parse_args()
source = Path(__file__).resolve().parent
build = a.build.resolve()
build.mkdir(parents=True, exist_ok=True)
guest = (a.guest or source/'guest.wasm').resolve()
mac = platform.system() == 'Darwin'
if a.swift and not mac:
    p.error('this fixture uses the macOS Swift toolchain')
if a.apple_crash and (not mac or a.ordinary):
    p.error('--apple-crash requires macOS and the continuation fixture')

def run(cmd, **kwargs):
    print(' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, timeout=300, **kwargs)

cc = os.environ.get('CC', 'cc')
cxx = os.environ.get('CXX', 'c++')
flags = ['-g', '-O2', '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror']
defines = ['-DSTACK_AUDIT_NATIVE']
if a.runtime == 'wamr':
    defines += ['-DEXPECT_GUEST_TRACE']
run([cc, *flags, *defines, '-I'+str(a.include.resolve()), '-c', source/'native.c', '-o', build/'native.o'])
run([cxx, *flags, '-c', source/'native.cpp', '-o', build/'cpp.o'])
run([os.environ.get('RUSTC', 'rustc'), '--crate-type=lib', '--emit=obj', '--edition=2024',
     '-Copt-level=2', '-Cdebuginfo=2', '-Cpanic=abort', '-Cforce-frame-pointers=yes',
     source/'native.rs', '-o', build/'rust.o'])
if a.swift:
    linker = ['xcrun', 'swiftc', '-module-cache-path', build/'swiftcache']
    run([*linker, '-g', '-O', '-parse-as-library', '-emit-object', source/'native.swift', '-o', build/'bridge.o'])
else:
    linker = [cxx]
    run([cc, *flags, '-c', source/'bridge.c', '-o', build/'bridge.o'])
exe = build/(a.runtime+'_stack_audit')
libs = ['-framework', 'Security', '-framework', 'CoreFoundation', '-liconv'] if mac else ['-lm', '-lpthread', '-ldl']
run([*linker, '-g', build/'native.o', build/'cpp.o', build/'rust.o', build/'bridge.o',
     a.library.resolve(), '-o', exe, *libs,
     *(['-Xlinker', '-object_path_lto', '-Xlinker', build/'linked_lto.o'] if mac else [])])
if mac:
    run(['xcrun', 'dsymutil', exe])
mode = ['ordinary'] if a.ordinary else []
normal = run([exe, guest, *mode], capture_output=True, text=True)
(build/'guest_trace.txt').write_text(normal.stdout)
if not a.ordinary:
    run([exe, guest, 'early'], capture_output=True, text=True)

if a.debugger == 'lldb':
    commands = ['settings set plugin.jit-loader.gdb.enable on', 'breakpoint set -n native_checkpoint']
    if not a.ordinary:
        commands += ['breakpoint set -n parked_checkpoint']
    commands += ['run', 'bt 32', 'continue']
    if not a.ordinary:
        commands += ['bt 32', 'continue']
    cmd = ['lldb', '--batch', *[x for c in commands for x in ['-o', c]], '--', exe, guest, *mode]
else:
    commands = ['set pagination off', 'break native_checkpoint']
    if not a.ordinary:
        commands += ['break parked_checkpoint']
    commands += ['run', 'bt 32', 'continue']
    if not a.ordinary:
        commands += ['bt 32', 'continue']
    cmd = ['gdb', '--batch', *[x for c in commands for x in ['-ex', c]], '--args', exe, guest, *mode]
result = run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
(build/'debugger.txt').write_text(result.stdout)
expected = ['native_checkpoint', 'nativeSwift' if a.swift else 'native_bridge', 'native_rust', 'native_cpp',
            'sync_import' if a.ordinary else 'native_import', 'wasm_func_call' if a.ordinary else 'wasm_ext_call_resume']
frames = [line for line in result.stdout.splitlines()
          if ('frame #' in line or line.startswith('#'))]
text = '\n'.join(frames)
position = 0
for symbol in expected:
    position = text.find(symbol, position)
    if position < 0:
        raise RuntimeError('missing or misordered native frame: '+symbol+'; see debugger.txt')
    position += len(symbol)
if not a.ordinary and 'parked_checkpoint' not in text:
    raise RuntimeError('missing suspended checkpoint')
receipt = {'runtime': a.runtime, 'debugger': a.debugger, 'platform': platform.platform(),
           'swift': a.swift, 'ordinary': a.ordinary, 'native_frame_order': expected,
           'guest_trace': normal.stdout, 'full_mixed_guest_stack_qualified': False}
if a.apple_crash:
    reports = Path.home()/'Library/Logs/DiagnosticReports'
    before = set(reports.glob(exe.name+'*.ips'))
    crashed = subprocess.run([str(exe), str(guest), 'crash'], capture_output=True, timeout=30)
    if crashed.returncode != -5:
        raise RuntimeError('expected SIGTRAP from the deliberate native crash')
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
    for symbol in expected:
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
    symbolicated = subprocess.check_output(['xcrun', 'atos', '-o', str(dsym), '-l', hex(image['base']),
        *[hex(image['base']+f['imageOffset']) for f in native_frames]], text=True)
    (build/'apple_symbolicated.txt').write_text(symbolicated)
    receipt['apple_crash'] = {'exception': report['exception'], 'binary_uuid': uuid,
                            'native_symbols': symbols, 'dsym_uuid_matches': True}
(build/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
print('PASS native unwind order; full guest/async stack remains a separate requirement')

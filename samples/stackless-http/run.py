#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Build the external adapter against feature-off runtimes and run its tests."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
from component import compile_script
from test_server import run as test_server

HERE = Path(__file__).resolve().parent
PIN = 'fed10b0b823b82553dd8363f16a640816f5eac65'
CASES = {'future-completion-order': (2, 0), 'same-component-stream-future': (12, 0),
         'futures-must-write': (5, 2)}


def run(*cmd, **kw):
    print(' '.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, timeout=900, **kw)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime', choices=('tinywasm', 'wamr'), required=True)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--component-tests', type=Path, required=True)
    p.add_argument('--library', type=Path, help='Reuse an already built static runtime archive')
    p.add_argument('--validator', type=Path, help='Reuse the pinned component tool')
    p.add_argument('--sanitize', action='store_true', help='WAMR runtime/client or supported-host Rust/std/C ASan')
    p.add_argument('--client-asan', action='store_true', help='TinyWasm Windows C clients only')
    a = p.parse_args()
    root, build = a.root.resolve(), a.build.resolve()
    win = os.name == 'nt'
    suffix = '.exe' if win else ''
    san = a.sanitize or a.client_asan
    wamr = a.runtime == 'wamr'
    build.mkdir(parents=True, exist_ok=True)
    library = a.library
    if not library:
        fixture = root / ('samples/shared-continuations' if wamr else 'crates/c-api/tests/continuations')
        flags = ['--sanitize'] if a.sanitize else ['--client-asan'] if a.client_asan else []
        with (build / 'ordinary-native.log').open('w') as f:
            run('python3' if not win else 'python', fixture / 'run.py', '--runtime', a.runtime,
                '--root', root, '--build', build / 'runtime', '--feature', 'off', *flags,
                stdout=f, stderr=subprocess.STDOUT)
        names = ('libiwasm.lib' if win else 'libiwasm.a') if wamr else ('tinywasm.lib' if win else 'libtinywasm.a')
        matches = [path for path in (build / 'runtime' / 'runtime').rglob(names)
                   if path.parent.name != 'deps']
        if len(matches) != 1:
            raise RuntimeError(f'expected exactly one runtime archive: {matches}')
        library = matches[0]
    include = root / ('core/iwasm/include' if wamr else 'crates/c-api/include')
    validator = a.validator
    if not validator:
        run('cargo', 'build', '--locked', '--release', '--manifest-path', HERE / 'tools/Cargo.toml',
            '--target-dir', build / 'tools')
        validator = build / 'tools' / 'release' / ('stackless-component-tool' + suffix)
    component_tree = a.component_tests.resolve()
    revision = subprocess.check_output(['git', '-C', component_tree, 'rev-parse', 'HEAD'], text=True).strip()
    if revision != PIN:
        raise RuntimeError(f'expected Component Model tests at {PIN}; got {revision}')
    artifact = HERE / 'handler.linked.wasm'
    data = artifact.read_bytes()
    (build / 'handler.h').write_text('static const unsigned char handler_wasm[] = {\n' +
        '\n'.join('    ' + ', '.join(f'0x{v:02x}' for v in data[i:i+16]) + ',' for i in range(0, len(data), 16)) + '\n};\n')
    env = os.environ.copy()
    if san:
        env['ASAN_OPTIONS'] = 'halt_on_error=1' + ('' if win else ':detect_stack_use_after_return=1')
    if win and env.get('VSINSTALLDIR'):
        tools = Path(env['VSINSTALLDIR']) / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
        env['PATH'] = os.pathsep.join([str(tools / 'CMake/bin'), str(tools / 'Ninja'), env['PATH']])
    output = build / 'adapter'
    run('cmake', '-G', 'Ninja', '-S', HERE, '-B', output, '-DCMAKE_BUILD_TYPE=Release',
        '-DP3_WAMR=' + str(int(wamr)), '-DP3_ASAN=' + str(int(san)),
        '-DP3_RUNTIME_LIBRARY=' + str(library.resolve()), '-DP3_RUNTIME_INCLUDE=' + str(include),
        '-DP3_HANDLER_INCLUDE=' + str(build), env=env)
    run('cmake', '--build', output, '-j', '4', env=env)
    receipts = []
    for name, (passed, unsupported) in CASES.items():
        rel = f'test/async/{name}.wast'
        pinned = subprocess.check_output(['git', '-C', component_tree, 'show', f'{PIN}:{rel}'])
        work = build / 'cases' / name
        work.mkdir(parents=True, exist_ok=True)
        # Read the pinned blob, independent of checkout filters such as CRLF.
        source = work / (name + '.wast')
        source.write_bytes(pinned)
        manifest = compile_script(source, work, str(validator))
        actual = [c for c in manifest['cases'] if 'module' in c]
        skips = [c for c in manifest['cases'] if 'module' not in c]
        if (len(actual), len(skips)) != (passed, unsupported):
            raise RuntimeError(f'{name}: unexpected profile coverage')
        for case in actual:
            receipt = run(output / ('component_check' + suffix), work / case['module'], case['export'],
                          case['expected'], *case['args'], env=env, capture_output=True, text=True)
            print(receipt.stdout, end='')
            case.update(status='PASS', receipt=receipt.stdout.strip())
        receipts.append(manifest)
    executable = output / ('stackless_http' + suffix)
    report = {'runtime': a.runtime, 'host': platform.platform(), 'component_model_revision': PIN,
              'component_sha256': hashlib.sha256((HERE / 'handler.component.wasm').read_bytes()).hexdigest(),
              'linked_sha256': hashlib.sha256(data).hexdigest(), 'component_tests': receipts,
              'sanitizer': 'client-only' if a.client_asan else 'runtime-and-client' if a.sanitize else 'none',
              'http': test_server(str(executable), 3, env)}
    if not san:
        samples = []
        for _ in range(7):
            out = run(executable, '--native', capture_output=True, text=True).stdout
            line = next(line for line in out.splitlines() if line.startswith('NATIVE '))
            samples.append(float(line.split('cpu_ns_per_import=')[1]))
        report['native_context_get'] = {'imports': 10000000, 'ns_per_import': samples,
                                        'median_ns_per_import': statistics.median(samples)}
        print(report['native_context_get'], flush=True)
    (build / 'results.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()

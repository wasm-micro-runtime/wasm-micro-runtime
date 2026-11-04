#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Run pinned upstream WASI 0.3 HTTP guests and their HTTP service assertions."""
import argparse
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time

from component import Unsupported
from wasi_http_link import link

SUITE = '9d84631e427729802085c3d172d0a63cd360a1fa'


def service(exe, wasm, config, cancel_probe=False):
    process = subprocess.Popen([exe, '0', '6' if os.name == 'nt' else '30', wasm], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        for op in config['operations']:
            match op['type']:
                case 'run':
                    line = process.stdout.readline()
                    if not line.startswith('LISTEN '):
                        raise RuntimeError(line)
                    port = int(line.split()[1])
                case 'request':
                    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
                    try:
                        conn.request(op['method'], op['path'], op.get('body'), op.get('headers', {}))
                        res = conn.getresponse()
                        expected = op['response']
                        assert res.status == expected['status'], (op, res.status)
                        for name, value in expected.get('headers', {}).items():
                            assert res.getheader(name) == value, (op, name, res.getheader(name))
                        body = res.read()
                        if 'body' in expected:
                            assert body == expected['body'].encode(), (op, body)
                    finally:
                        conn.close()
                case 'kill' if op['signal'] == 'SIGINT':
                    if cancel_probe:
                        with socket.create_connection(('127.0.0.1', port), timeout=3) as conn:
                            conn.sendall(b'POST /echo HTTP/1.1\r\nHost: localhost\r\n'
                                         b'Content-Length: 32\r\n\r\nabc')
                            time.sleep(.05)
                        time.sleep(.05)
                    if os.name != 'nt':
                        process.send_signal(signal.SIGINT)
                case 'wait':
                    out, err = process.communicate(timeout=8)
                    if (process.returncode or err or 'resources_live=0 ' not in out
                            or ' live=0 ' not in out
                            or (cancel_probe and ' cancelled=1 ' not in out)):
                        raise RuntimeError((out, err, process.returncode))
                case _:
                    raise ValueError('Unsupported test operation ' + str(op))
    except Exception as error:
        if process.poll() is None:
            process.kill()
        out, err = process.communicate()
        raise RuntimeError(f'{error}; server stdout={out!r}, stderr={err!r}') from error
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('suite', type=Path)
    parser.add_argument('build', type=Path)
    parser.add_argument('--wasm-tools', default='wasm-tools')
    parser.add_argument('--aot-compiler', type=Path,
                        help='run the linked guests as WAMR AOT with software bounds checks')
    parser.add_argument('--aot-gc', action='store_true', help='match a GC-enabled WAMR runtime')
    args = parser.parse_args()
    if args.aot_gc and not args.aot_compiler:
        parser.error('--aot-gc requires --aot-compiler')
    commit = subprocess.check_output(['git', '-C', args.suite, 'rev-parse', 'HEAD'], text=True).strip()
    if commit != SUITE:
        parser.error('Expected artifact revision ' + SUITE)
    directory = args.suite / 'tests/rust/testsuite/wasm32-wasip3'
    exe = '.exe' if os.name == 'nt' else ''
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for wasm in sorted(directory.glob('http-*.wasm')):
            config = json.loads(wasm.with_suffix('.json').read_text())
            linked = Path(tmp) / wasm.name
            try:
                link(wasm, linked, args.wasm_tools)
                if args.aot_compiler:
                    compiled = linked.with_suffix('.aot')
                    subprocess.run([args.aot_compiler.resolve(), '--bounds-checks=1',
                                    '--stack-bounds-checks=1',
                                    *(['--enable-gc'] if args.aot_gc else []),
                                    '-o', compiled, linked], check=True, capture_output=True, text=True)
                    linked = compiled
                if config.get('world') == 'wasi:http/service':
                    service(args.build / ('wasi_http_server' + exe), linked, config,
                            cancel_probe=wasm.stem == 'http-service-echo')
                else:
                    subprocess.run([args.build / ('wasi_http_check' + exe), linked], check=True,
                                   capture_output=True, text=True)
                print('PASS', wasm.stem)
            except Unsupported as error:
                if str(error) != 'wasi:http/client.send requires outbound HTTP transport':
                    print('FAIL', wasm.stem, error)
                    failures += 1
                else:
                    print('UNSUPPORTED', wasm.stem, error)
            except (OSError, ValueError, AssertionError, RuntimeError, subprocess.CalledProcessError) as error:
                print('FAIL', wasm.stem, error, getattr(error, 'stderr', ''))
                failures += 1
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())

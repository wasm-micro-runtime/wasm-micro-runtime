#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import time


def stats(output):
    row = next(line for line in output.splitlines() if line.startswith('RESULT '))
    return {k: float(v) if '.' in v else int(v)
            for k, v in (pair.split('=') for pair in row.split()[1:])}


def start(executable, seconds, env):
    p = subprocess.Popen([executable, '0', str(seconds)], text=True, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, env=env)
    for line in p.stdout:
        if line.startswith('LISTEN '):
            return p, int(line.split()[1])
        print(line, end='')
    raise RuntimeError(f'server failed to start: {p.wait()}')


def collect(p):
    output, _ = p.communicate(timeout=20)
    print(output, end='')
    if p.returncode:
        raise RuntimeError(f'server exit {p.returncode}')
    return stats(output)


def connect(port):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2048)
    s.settimeout(3)
    s.connect(('127.0.0.1', port))
    return s


def response(s):
    chunks = []
    while chunk := s.recv(4096):
        chunks.append(chunk)
    header, body = b''.join(chunks).split(b'\r\n\r\n', 1)
    assert len(body) == int(next(line.split(b': ')[1] for line in header.splitlines()
                                if line.startswith(b'Content-Length: ')))
    return header.split()[1], body


def request(port, data):
    with connect(port) as s:
        s.sendall(data)
        return response(s)


def run(executable, idle_seconds, env=None):
    p, _ = start(executable, idle_seconds, env)
    idle = collect(p)
    assert idle['wakes'] == 0 and idle['waits'] <= 2 and idle['callbacks'] == 0
    assert idle['elapsed_s'] >= idle_seconds * .95
    p, port = start(executable, 6, env)
    try:
        # A task with an incomplete request must not block another in the same store.
        with connect(port) as slow:
            slow.sendall(b'GET / HTTP/1.1\r\nHost:')
            assert request(port, b'GET / HTTP/1.1\r\n\r\n') == (b'200', b'stackless wasm\n')
            slow.sendall(b' localhost\r\n\r\n')
            assert response(slow) == (b'200', b'stackless wasm\n')
        # Leave three more tasks waiting while a fourth streams a bounded response.
        waiting = [connect(port) for _ in range(3)]
        try:
            for s in waiting:
                s.sendall(b'GET /')
            with connect(port) as slow:
                slow.sendall(b'GET /large HTTP/1.1\r\n\r\n')
                time.sleep(.15)
                status, data = response(slow)
                assert status == b'200' and data == bytes(97 + i % 26 for i in range(65536))
        finally:
            for s in waiting:
                s.close()
        # A disconnect after task.return closes the stream, not the resolved task.
        with connect(port) as s:
            s.sendall(b'GET /large HTTP/1.1\r\n\r\n')
            assert s.recv(128).startswith(b'HTTP/1.1 200')
            linger = struct.pack('hh' if os.name == 'nt' else 'ii', 1, 0)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
        assert request(port, b'GET /' + b'x' * 2043) == (b'431', b'')
        assert request(port, b'NO / HTTP/1.1\r\n\r\n') == (b'400', b'')
        # Repeated opens prove resource pools are reused after cancellation and EOF.
        for _ in range(20):
            assert request(port, b'GET / HTTP/1.1\r\n\r\n') == (b'200', b'stackless wasm\n')
        load = collect(p)
        assert load['completed'] == 25 and load['cancelled'] == 3
        assert load['disconnected'] == 1 and load['live'] == 0
        assert load['peak_handles'] <= 4 * 7
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate()
    return {'idle': idle, 'load': load}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Exercise the real TCP/component server and measure hosted idle CPU')
    parser.add_argument('executable', type=Path)
    parser.add_argument('--idle-seconds', type=float, default=3)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    report = run(str(args.executable.resolve()), args.idle_seconds)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + '\n')

#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Check concurrent standard-component HTTP/1.1 bodies, overload and recovery."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import os
import signal
import socket
import struct
import subprocess
import threading
import time

from build_wasi_http_guest import json_body, png

ASSETS = {
    '/data.json': ('GET', 'application/json', json_body()),
    '/image.png': ('GET', 'image/png', png(4096)),
    '/upload': ('POST', 'image/png', png(16384)),
}


def request(sock, path, barrier=None):
    method, content_type, expected = ASSETS[path]
    body = expected if method == 'POST' else b''
    head = (f'{method} {path} HTTP/1.1\r\nHost: localhost\r\n'
            f'Content-Length: {len(body)}\r\nContent-Type: {content_type}\r\n')
    sock.sendall(head.encode())
    if barrier:
        barrier.wait(timeout=3)
    sock.sendall(b'\r\n')
    for offset in range(0, len(body), 509):
        sock.sendall(body[offset:offset + 509])
    response = http.client.HTTPResponse(sock)
    response.begin()
    assert response.status == 200, (path, response.status)
    assert response.getheader('content-type') == content_type
    assert response.read() == expected, path


def run(exe):
    process = subprocess.Popen([exe, '0', '6' if os.name == 'nt' else '60'], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
    try:
        line = process.stdout.readline()
        assert line.startswith('LISTEN '), line
        port = int(line.split()[1])
        def connect():
            return socket.create_connection(('127.0.0.1', port), timeout=3)
        time.sleep(.25)
        def batch(paths):
            barrier = threading.Barrier(len(paths))
            def get(path):
                with connect() as sock:
                    request(sock, path, barrier)
            with ThreadPoolExecutor(max_workers=len(paths)) as pool:
                list(pool.map(get, paths))
        for count in range(1, 5):
            batch([('/data.json', '/image.png')[i % 2] for i in range(count)])
            batch(['/upload'] * count)
        batch(['/data.json', '/image.png', '/upload', '/upload'])
        failures = {}
        for path in ('/data.json', '/image.png', '/upload'):
            holders = []
            try:
                for _ in range(4):
                    sock = connect()
                    holders.append(sock)
                    if path == '/upload':
                        sock.sendall(b'POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 16384\r\n\r\n')
                        sock.sendall(ASSETS[path][2][:509])
                    else:
                        sock.sendall(f'GET {path} HTTP/1.1\r\nHost: localhost\r\n'.encode())
                # Wait for all four sockets to be admitted before offering the fifth.
                time.sleep(.05)
                def probe(_):
                    phase = 'connect'
                    try:
                        with connect() as extra:
                            phase = 'established'
                            method = ASSETS[path][0]
                            length = 16384 if method == 'POST' else 0
                            extra.sendall((f'{method} {path} HTTP/1.1\r\nHost: localhost\r\n'
                                           f'Content-Length: {length}\r\n\r\n').encode())
                            assert extra.recv(256) == b'', 'over-capacity connection served'
                            return 'established:eof'
                    except ConnectionResetError:
                        return phase + ':reset'
                    except BrokenPipeError:
                        return phase + ':broken-pipe'
                    except ConnectionRefusedError:
                        return phase + ':refused'
                with ThreadPoolExecutor(max_workers=4) as pool:
                    failures[path] = list(pool.map(probe, range(4)))
            finally:
                for sock in holders:
                    sock.close()
            time.sleep(.05)
            batch([path])
        # Reset after seeing a response while a POST producer is still waiting for bytes.
        with connect() as sock:
            sock.sendall(b'POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 16384\r\n\r\n')
            sock.sendall(ASSETS['/upload'][2][:509])
            assert sock.recv(256).startswith(b'HTTP/1.1 200')
            linger = 'HH' if os.name == 'nt' else 'ii'
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack(linger, 1, 0))
        batch(['/data.json', '/image.png', '/upload'])
        for headers, status in (
            (b'Content-Length: 1\r\nContent-Length: 1\r\n', 400),
            (b'Content-Length: 1\r\nTransfer-Encoding: chunked\r\n', 400),
            (b'Content-Length: 65537\r\n', 413),
            (b'X-Too-Large: ' + b'x' * 2200 + b'\r\n', 431),
        ):
            with connect() as sock:
                sock.sendall(b'POST /upload HTTP/1.1\r\nHost: localhost\r\n' + headers + b'\r\n')
                response = http.client.HTTPResponse(sock)
                response.begin()
                assert response.status == status and not response.read()
        with connect() as sock:
            sock.sendall(b'GET /\xff HTTP/1.1\r\nHost: localhost\r\n\r\n')
            response = http.client.HTTPResponse(sock)
            response.begin()
            assert response.status == 400 and not response.read()
        batch(['/data.json'])
        if os.name != 'nt':
            process.send_signal(signal.SIGINT)
        out, err = process.communicate(timeout=8)
        assert process.returncode == 0 and not err, (out, err, process.returncode)
        assert 'resources_live=0 ' in out and ' live=0 ' in out, out
        return {'overload': failures, 'server': out.strip().splitlines()}
    except Exception:
        if process.poll() is None:
            process.kill()
        out, err = process.communicate()
        print(out, err)
        raise


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('server')
    a = p.parse_args()
    print(json.dumps(run(a.server), indent=2))

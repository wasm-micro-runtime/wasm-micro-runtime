#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Exercise cleartext HTTP/2 against the shared WASI HTTP service."""
import argparse
import json
import os
import select
import signal
import socket
import subprocess
import time

import h2.config
import h2.connection
import h2.events
import h2.exceptions
from h2.settings import SettingCodes

from build_wasi_http_guest import json_body, png

ASSETS = {
    '/data.json': ('GET', 'application/json', json_body()),
    '/image.png': ('GET', 'image/png', png(4096)),
    '/upload': ('POST', 'image/png', png(16384)),
}


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=3)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.h2 = h2.connection.H2Connection(h2.config.H2Configuration(header_encoding='ascii'))
        self.jobs = {}
        self.settings = 0
        self.h2.initiate_connection()
        try:
            self.flush()
        except OSError:
            self.sock.close()
            raise

    def flush(self):
        data = self.h2.data_to_send()
        if data:
            self.sock.sendall(data)

    def open(self, path):
        method, mime, expected = ASSETS[path]
        stream = self.h2.get_next_available_stream_id()
        body = expected if method == 'POST' else b''
        self.h2.send_headers(stream, [
            (':method', method), (':scheme', 'http'), (':authority', 'localhost'),
            (':path', path), ('content-type', mime), ('content-length', str(len(body))),
        ], end_stream=not body)
        self.jobs[stream] = {'path': path, 'send': body, 'at': 0, 'received': bytearray(),
                             'headers': None, 'trailers': {}, 'done': False, 'reset': None}
        return stream

    def read(self):
        data = self.sock.recv(65536)
        if not data:
            raise EOFError('HTTP/2 connection ended')
        for ev in self.h2.receive_data(data):
            if isinstance(ev, h2.events.SettingsAcknowledged):
                self.settings += 1
            elif isinstance(ev, h2.events.ConnectionTerminated):
                raise RuntimeError(f'GOAWAY {ev.error_code}')
            elif isinstance(ev, h2.events.ResponseReceived):
                self.jobs[ev.stream_id]['headers'] = dict(ev.headers)
            elif isinstance(ev, h2.events.TrailersReceived):
                self.jobs[ev.stream_id]['trailers'] = dict(ev.headers)
            elif isinstance(ev, h2.events.DataReceived):
                self.jobs[ev.stream_id]['received'].extend(ev.data)
                self.h2.acknowledge_received_data(ev.flow_controlled_length, ev.stream_id)
            elif isinstance(ev, h2.events.StreamEnded):
                self.jobs[ev.stream_id]['done'] = True
            elif isinstance(ev, h2.events.StreamReset):
                self.jobs[ev.stream_id]['reset'] = int(ev.error_code)

    def upload(self):
        for sid, j in self.jobs.items():
            if j['reset'] is not None or j['at'] == len(j['send']):
                continue
            n = min(self.h2.local_flow_control_window(sid), len(j['send']) - j['at'])
            while n > 0:
                take = min(n, 509)
                end = j['at'] + take
                self.h2.send_data(sid, j['send'][j['at']:end], end_stream=end == len(j['send']))
                j['at'] = end
                n -= take
        self.flush()

    def check(self, sid):
        j = self.jobs.pop(sid)
        method, mime, expected = ASSETS[j['path']]
        assert j['headers'][':status'] == '200' and j['headers']['content-type'] == mime, j
        assert j['received'] == expected, (j['path'], len(j['received']))
        if method == 'POST':
            assert j['trailers']['x-upload-check'] == 'complete', j['trailers']
        return len(expected)


def drive(clients, complete):
    deadline = time.monotonic() + 5
    while not complete():
        for c in clients:
            c.upload()
        left = deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError([(j['path'], j['at'], len(j['received']), j['done'], j['reset'])
                                for c in clients for j in c.jobs.values()])
        ready, _, _ = select.select([c.sock for c in clients], [], [], left)
        for c in clients:
            if c.sock in ready:
                c.read()
                c.flush()


def run(exe, rounds):
    p = subprocess.Popen([exe, '0', '20' if os.name == 'nt' else '90'],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    clients = []
    try:
        line = p.stdout.readline().split()
        assert line[0] == 'LISTEN', line
        port = int(line[1])
        caps = {k: int(v) for k, v in (x.split('=') for x in line[2:])}

        retries = 0

        def connect(n):
            nonlocal retries
            for attempt in range(20):
                new = []
                try:
                    for _ in range(n):
                        c = Client(port)
                        new.append(c)
                        clients.append(c)
                    drive(new, lambda: all(c.settings for c in new))
                    return new
                except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError, EOFError):
                    close(new)
                    if attempt == 19:
                        raise
                    # TCP accept may race the previous connection's reaping.
                    retries += 1
                    time.sleep(.02)

        def close(cs):
            for c in cs:
                c.sock.close()
                clients.remove(c)

        def batch(cs, paths):
            jobs = [(c, c.open(path)) for c, group in zip(cs, paths) for path in group]
            drive(cs, lambda: all(c.jobs[i]['done'] or c.jobs[i]['reset'] is not None for c, i in jobs))
            assert all(c.jobs[i]['done'] for c, i in jobs), [c.jobs[i]['reset'] for c, i in jobs]
            return sum(c.check(i) for c, i in jobs)

        rows = []
        workloads = [('mixed', ('/data.json', '/image.png', '/upload')),
                     ('GET', ('/data.json', '/image.png')), ('POST', ('/upload',))]
        for connections in (1, 2, 4):
            cs = connect(connections)
            maximum = min(caps['per_connection'], caps['streams'] // connections)
            for concurrency in sorted({1, maximum // 2, maximum}):
                for name, paths in workloads:
                    start = time.perf_counter()
                    transferred = 0
                    for wave in range(rounds):
                        groups = [[paths[(wave + i + j) % len(paths)] for j in range(concurrency)]
                                  for i in range(connections)]
                        transferred += batch(cs, groups)
                    elapsed = time.perf_counter() - start
                    rows.append({'workload': name, 'connections': connections,
                                 'streams_per_connection': concurrency, 'responses': rounds * connections * concurrency,
                                 'response_bytes': transferred, 'seconds': elapsed})
            close(cs)

        held = connect(4)
        for c in held:
            c.h2.update_settings({SettingCodes.INITIAL_WINDOW_SIZE: 0})
            c.flush()
        drive(held, lambda: all(c.settings >= 2 for c in held))
        first = [held[0].open('/image.png') for _ in range(caps['per_connection'])]
        drive(held, lambda: all(held[0].jobs[i]['headers'] for i in first))
        try:
            held[0].open('/data.json')
        except h2.exceptions.TooManyStreamsError:
            pass
        else:
            raise AssertionError('peer SETTINGS did not cap per-connection streams')
        second = [held[1].open('/upload') for _ in range(caps['streams'] - len(first))]
        drive(held, lambda: all(held[1].jobs[i]['headers'] for i in second))
        overflow = held[2].open('/data.json')
        drive(held, lambda: held[2].jobs[overflow]['reset'] is not None)
        assert held[2].jobs.pop(overflow)['reset'] == 7  # REFUSED_STREAM
        connection_failure = None
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=3) as extra:
                extra.sendall(b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n')
                assert extra.recv(1) == b''
                connection_failure = 'EOF'
        except ConnectionResetError:
            connection_failure = 'reset'
        except ConnectionAbortedError:
            connection_failure = 'abort'
        # One POST drains while siblings remain blocked by their own windows.
        target = second.pop()
        held[1].h2.increment_flow_control_window(20000, target)
        drive(held, lambda: held[1].jobs[target]['done'])
        held[1].check(target)
        assert all(not held[0].jobs[i]['received'] for i in first)
        assert all(not held[1].jobs[i]['received'] for i in second)
        for c, ids in zip(held, (first, second)):
            for i in ids:
                c.h2.reset_stream(i)
                del c.jobs[i]
            c.flush()
        close(held)
        recovered = connect(1)
        batch(recovered, [['/data.json', '/image.png', '/upload']])
        close(recovered)

        if os.name != 'nt':
            p.send_signal(signal.SIGINT)
        out, err = p.communicate(timeout=25)
        assert p.returncode == 0 and not err, (out, err, p.returncode)
        row = next(line for line in out.splitlines() if line.startswith('H2 '))
        server = {k: float(v) if '.' in v else int(v)
                  for k, v in (word.split('=') for word in row.split()[1:])}
        assert all(server[k] == 0 for k in ('live', 'handles', 'resources', 'heap'))
        assert server['refused'] >= 1 and server['connections_rejected'] >= 1
        assert server['peak_streams'] == caps['streams'] and server['queue_peak'] <= 2048
        return {'limits': caps, 'overload': {'per_connection': 'peer SETTINGS limit',
                'global': 'REFUSED_STREAM', 'tcp_after_establishment': connection_failure},
                'reconnect_retries': retries, 'workloads': rows, 'server': server}
    except Exception:
        for c in clients:
            c.sock.close()
        p.kill()
        print(p.communicate())
        raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('server')
    parser.add_argument('--rounds', type=int, default=24)
    args = parser.parse_args()
    print(json.dumps(run(args.server, args.rounds), indent=2))

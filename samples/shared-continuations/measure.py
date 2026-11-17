# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Alternate named fixture executables and preserve every timing/memory sample as CSV."""
import argparse
import csv
import ctypes
import os
from pathlib import Path
import re
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--rounds', type=int, default=7)
p.add_argument('--cpu', type=int, help='logical CPU index; Windows/Linux only')
p.add_argument('executables', nargs='+', help='name=path, e.g. baseline=build-base/fixture')
a = p.parse_args()
if a.rounds < 1 or (a.cpu is not None and a.cpu < 0):
    p.error('rounds must be positive and cpu nonnegative')
if a.cpu is not None and os.name != 'nt' and not hasattr(os, 'sched_setaffinity'):
    p.error('CPU affinity is unavailable on this platform')
variants = [(name, str(Path(path).resolve())) for name, path in (x.split('=', 1) for x in a.executables)]
if os.name == 'nt' and a.cpu is not None:
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
    k32.OpenProcess.restype = ctypes.c_void_p
    k32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    k32.CloseHandle.argtypes = [ctypes.c_void_p]
writer = csv.writer(sys.stdout, lineterminator="\n")
writer.writerow(['variant', 'round', 'compute_ns', 'native_ns', 'peak_rss_bytes', 'peak_commit_bytes', 'compute_cycles', 'native_cycles', 'cpu'])
for i in range(a.rounds):
    for name, path in (variants if i % 2 == 0 else reversed(variants)):
        child = subprocess.Popen([path, str(a.fixture.resolve())], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            if a.cpu is not None:
                if os.name == 'nt':
                    handle = k32.OpenProcess(0x200, False, child.pid)  # PROCESS_SET_INFORMATION
                    if not handle:
                        raise ctypes.WinError(ctypes.get_last_error())
                    try:
                        if not k32.SetProcessAffinityMask(handle, 1 << a.cpu):
                            raise ctypes.WinError(ctypes.get_last_error())
                    finally:
                        k32.CloseHandle(handle)
                else:
                    os.sched_setaffinity(child.pid, {a.cpu})
            out, err = child.communicate(timeout=120)
            if child.returncode:
                raise RuntimeError(f'{name} failed: {out}\n{err}')
            values = []
            for key in ['compute_ns_per_iteration', 'native_ns_per_iteration', 'peak_rss_bytes', 'peak_commit_bytes', 'compute_cycles_per_iteration', 'native_cycles_per_iteration']:
                m = re.search(r'(?m)^' + key + r' ([0-9.]+)', out) if key != 'peak_commit_bytes' else re.search(key + r' ([0-9.]+)', out)
                if m is None and key not in ('peak_commit_bytes', 'compute_cycles_per_iteration', 'native_cycles_per_iteration'):
                    raise RuntimeError(f'{name} omitted {key}: {out}')
                values.append(m[1] if m else '')
            writer.writerow([name, i, *values, a.cpu if a.cpu is not None else ''])
            sys.stdout.flush()
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()

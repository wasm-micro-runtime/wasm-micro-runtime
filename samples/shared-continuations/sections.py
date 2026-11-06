# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Print linked Mach-O64 or PE image section sizes and hashes as CSV."""
import argparse
import csv
import hashlib
from pathlib import Path
import struct
import sys
p = argparse.ArgumentParser()
p.add_argument('executables', nargs='+', help='variant=path')
a = p.parse_args()
w = csv.writer(sys.stdout, lineterminator="\n")
w.writerow(['variant', 'section', 'virtual_bytes', 'file_bytes', 'sha256'])
for item in a.executables:
    name, path = item.split('=', 1)
    b = Path(path).read_bytes()
    w.writerow([name, '<file>', '', len(b), hashlib.sha256(b).hexdigest()])
    if b[:4] == b'\xcf\xfa\xed\xfe':
        commands = struct.unpack_from('<I', b, 16)[0]
        pos = 32
        for _ in range(commands):
            command, length = struct.unpack_from('<II', b, pos)
            if command == 0x19:
                count = struct.unpack_from('<I', b, pos + 64)[0]
                for i in range(count):
                    section = pos + 72 + 80 * i
                    field = lambda start: b[start:start+16].split(b'\0')[0].decode()
                    label = field(section+16) + ',' + field(section)
                    size = struct.unpack_from('<Q', b, section+40)[0]
                    offset = struct.unpack_from('<I', b, section+48)[0]
                    w.writerow([name, label, size, size if offset else 0,
                                hashlib.sha256(b[offset:offset+size]).hexdigest() if offset else ''])
            pos += length
    elif b[:2] == b'MZ':
        pe = struct.unpack_from('<I', b, 60)[0]
        if b[pe:pe+4] != b'PE\0\0':
            p.error(f'{path}: invalid PE header')
        count = struct.unpack_from('<H', b, pe+6)[0]
        optional = struct.unpack_from('<H', b, pe+20)[0]
        for i in range(count):
            section = pe + 24 + optional + 40 * i
            label = b[section:section+8].split(b'\0')[0].decode()
            vsize, _, size, offset = struct.unpack_from('<IIII', b, section+8)
            w.writerow([name, label, vsize, size, hashlib.sha256(b[offset:offset+size]).hexdigest()])
    else:
        p.error(f'{path}: only Mach-O64 and PE are supported; use llvm-size/readelf for ELF')

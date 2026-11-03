#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Build a one-page, freestanding guest from the upstream HTTP service world."""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

from wasi_http_link import link


def png(size):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    header = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 1, 1, 8, 0, 0, 0, 0))
    image = chunk(b'IDAT', zlib.compress(b'\0\x7f')) + chunk(b'IEND', b'')
    padding = b'Comment\0'
    return header + chunk(b'tEXt', padding + b'x' * (size - len(header) - len(image) - 12 - len(padding))) + image


def json_body():
    return b'{"data":"' + b'x' * (4096 - len(b'{"data":""}\n')) + b'"}\n'


def c_array(name, data):
    return f'static const unsigned char {name}[] = {{\n' + '\n'.join(
        '    ' + ', '.join(f'0x{v:02x}' for v in data[i:i + 16]) + ','
        for i in range(0, len(data), 16)) + '\n};\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('suite', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--tasks', type=int, choices=(4, 16), default=4)
    p.add_argument('--clang', default='clang')
    p.add_argument('--wasm-tools', default='wasm-tools')
    a = p.parse_args()
    here = Path(__file__).resolve().parent
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / 'upload.h').write_text(c_array('upload_body', png(16384)))
    (a.output / 'payloads.h').write_text(c_array('json_body', json_body()) + c_array('png_body', png(4096)))
    wit = a.suite / 'tests/rust/wasm32-wasip3/wit'
    core, typed, component, linked = (a.output / f'standard.{ext}.wasm' for ext in ('core', 'typed', 'component', 'linked'))
    subprocess.run([a.clang, '--target=wasm32', '-Os', '-fno-builtin', '-nostdlib',
                    '-Wl,--no-entry,--initial-memory=65536,--max-memory=65536,-z,stack-size=4096,--strip-all',
                    f'-DCONNECTIONS={a.tasks}', '-I', a.output, here / 'wasi_http_guest.c', '-o', core], check=True)
    subprocess.run([a.wasm_tools, 'component', 'embed', wit, core, '--world', 'wasi:http/service@0.3.2', '-o', typed], check=True)
    subprocess.run([a.wasm_tools, 'component', 'new', typed, '-o', component], check=True)
    link(component, linked, a.wasm_tools)
    (a.output / 'standard_handler.h').write_text(c_array('handler_wasm', linked.read_bytes()))


if __name__ == '__main__':
    main()

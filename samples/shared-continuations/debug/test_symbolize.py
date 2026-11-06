# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import subprocess
import symbolize


class DecodeTest(unittest.TestCase):
    def record(self):
        data = bytearray(symbolize.RECORD)
        struct.pack_into('<4I', data, 0, 0x57444247, 1, len(data), 0)
        struct.pack_into('<4I', data, 64, 7, 0, 3, 0)
        struct.pack_into('<4I', data, 84, 1, 528, 0, 0)
        return data

    def test_bounded_corrupt_and_interrupted_publication(self):
        data = self.record()
        # Inactive bank can be half-written; active committed bank stays readable.
        data[64+symbolize.TASK:] = b'\xff'*symbolize.TASK
        self.assertEqual(symbolize.decode(data)['current']['task'], 7)
        for offset, value in [(12,2), (92,33), (64+symbolize.SEGMENT,5), (64+symbolize.SEGMENT+8,5)]:
            broken = data.copy()
            struct.pack_into('<I', broken, offset, value)
            with self.assertRaises(ValueError):
                symbolize.decode(broken)
        with self.assertRaises(ValueError):
            symbolize.decode(data[:-1])

    def test_missing_dwarf_keeps_the_logical_frame(self):
        data = self.record()
        struct.pack_into('<I', data, 92, 1)
        struct.pack_into('<4I', data, 100, 0, 1, 1, 1)
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory)/'guest.wasm'
            module.write_bytes(b'original')
            manifest = Path(directory)/'manifest.json'
            manifest.write_text(json.dumps({'wasm_tools':'wasm-tools', 'modules': {'0': {'path':str(module), 'sha256':hashlib.sha256(module.read_bytes()).hexdigest()}}}))
            data[20:52] = hashlib.sha256(manifest.read_bytes()).digest()
            with patch('subprocess.check_output', side_effect=subprocess.CalledProcessError(1, 'addr2line')):
                self.assertIn('function=1 0x1 <source unavailable>', symbolize.render(data, manifest))

    def test_build_identity_and_original_module_hash(self):
        data = self.record()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = root/'guest.wasm'
            module.write_bytes(b'original')
            manifest = root/'manifest.json'
            manifest.write_text(json.dumps({'modules': {'0': {'path':str(module), 'sha256':hashlib.sha256(module.read_bytes()).hexdigest()}}}))
            data[20:52] = hashlib.sha256(manifest.read_bytes()).digest()
            self.assertIn('state=pending', symbolize.render(data,manifest))
            module.write_bytes(b'different build')
            with self.assertRaisesRegex(ValueError,'content hash'):
                symbolize.render(data,manifest)
            manifest.write_text('{}')
            with self.assertRaisesRegex(ValueError,'build/session'):
                symbolize.render(data,manifest)

if __name__ == '__main__':
    unittest.main()

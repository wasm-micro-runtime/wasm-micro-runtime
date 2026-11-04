# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Consumer-side logical stack decoding; never executes code in a crashed target."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

SEGMENT = 676
TASK = SEGMENT * 9 + 16
RECORD = 64 + TASK * 2
EVENTS = {1: 'start', 2: 'host', 3: 'pending', 4: 'resume', 5: 'trap', 6: 'cancel', 7: 'done', 8: 'async-error'}

def decode(data):
    if len(data) != RECORD:
        raise ValueError('snapshot size mismatch')
    magic, version, size, active = struct.unpack_from('<4I', data)
    if (magic, version, size) != (0x57444247, 1, RECORD) or active > 1:
        raise ValueError('snapshot header mismatch')
    start = 64 + active * TASK
    def segment(offset):
        task, parent, event, cause, error_flags = struct.unpack_from('<5I', data, offset)
        v, size, count, flags = struct.unpack_from('<4I', data, offset+20)
        if (v, size) != (1, 528) or count > 32 or flags & ~1:
            raise ValueError('invalid guest snapshot')
        frames = [struct.unpack_from('<4I', data, offset+36+i*16) for i in range(count)]
        error = data[offset+548:offset+676].split(b'\0', 1)[0].decode('utf-8', errors='replace')
        return dict(task=task, parent=parent, event=EVENTS.get(event, 'unknown'), cause=cause,
                    frames=frames, truncated=bool(flags), error=error, error_truncated=bool(error_flags))
    ancestors, truncated, causes, causes_truncated = struct.unpack_from('<4I', data, start+SEGMENT)
    if ancestors > 4 or causes > 4:
        raise ValueError('invalid bounded ancestry count')
    return dict(current=segment(start),
                ancestry=[segment(start+SEGMENT+16+i*SEGMENT) for i in range(ancestors)],
                failures=[segment(start+SEGMENT+16+(4+i)*SEGMENT) for i in range(causes)],
                truncated=bool(truncated), causes_truncated=bool(causes_truncated))

def render(data, manifest_path=None):
    record = decode(data)
    manifest_bytes = Path(manifest_path or os.environ['WASM_DEBUG_MANIFEST']).read_bytes()
    if hashlib.sha256(manifest_bytes).digest() != data[20:52]:
        raise ValueError('snapshot belongs to a different build/session manifest')
    manifest = json.loads(manifest_bytes)
    modules = manifest['modules']
    # Refuse stale modules instead of resolving a plausible but incorrect source line.
    for module in modules.values():
        if hashlib.sha256(Path(module['path']).read_bytes()).hexdigest() != module['sha256']:
            raise ValueError('original Wasm content hash mismatch')
        for source in module.get('sources', []):
            if hashlib.sha256(Path(source['path']).read_bytes()).hexdigest() != source['sha256']:
                raise ValueError('guest source content hash mismatch')
    lines = []
    def show(segment, label):
        lines.append(f"{label} task={segment['task']} parent={segment['parent']} state={segment['event']} cause={segment['cause']}")
        if segment['error']:
            lines.append('  exception: '+segment['error']+(' [message truncated]' if segment['error_truncated'] else ''))
        for instance, function, offset, flags in segment['frames']:
            module = modules.get(str(instance))
            prefix = f'  Wasm instance={instance} function={function} '
            if flags != 1 or not module:
                lines.append(prefix+'<unmapped>')
                continue
            if offset >= Path(module['path']).stat().st_size:
                raise ValueError('source offset outside original module')
            resolved = subprocess.check_output([manifest['wasm_tools'], 'addr2line', module['path'], hex(offset)], text=True, timeout=15).strip()
            for source in module.get('sources', []):
                resolved = resolved.replace('/guest/'+Path(source['path']).name, source['path'])
            lines.append(prefix+resolved)
        if segment['truncated']:
            lines.append('  [guest stack truncated at 32 frames]')
    show(record['current'], 'current')
    for segment in record['ancestry']:
        show(segment, 'async parent')
    for segment in record['failures']:
        show(segment, 'caused by')
    if record['truncated']:
        lines.append('[async ancestry truncated at 4 parents]')
    if record['causes_truncated']:
        lines.append('[exception causes truncated at 4]')
    return '\n'.join(lines)+'\n'

if __name__ == '__main__':
    import sys
    print(render(Path(sys.argv[1]).read_bytes(), sys.argv[2]), end='')

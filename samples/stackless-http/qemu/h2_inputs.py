#!/usr/bin/env python3
# Copyright (C) 2026 Matt Hargett. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Generate the HTTP/2 request frames used by the Cortex-M33 fixture."""
from pathlib import Path
import sys

from hpack import Encoder
from hyperframe.frame import HeadersFrame, SettingsFrame

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_wasi_http_guest import c_array


def requests(path):
    encoder = Encoder()
    encoder.header_table_size = 0
    settings = SettingsFrame(0)
    settings.settings = {SettingsFrame.INITIAL_WINDOW_SIZE: 0}
    data = b'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n' + settings.serialize()
    for stream, method, route, size in ((1, 'GET', path, 0), (3, 'POST', '/upload', 16384)):
        frame = HeadersFrame(stream)
        frame.data = encoder.encode([(':method', method), (':scheme', 'http'),
                                     (':authority', 'localhost'), (':path', route),
                                     ('content-length', str(size))])
        frame.flags.add('END_HEADERS')
        if not size:
            frame.flags.add('END_STREAM')
        data += frame.serialize()
    return data


output = Path(sys.argv[1])
output.write_text(c_array('h2_json_requests', requests('/data.json'))
                  + c_array('h2_png_requests', requests('/image.png')))

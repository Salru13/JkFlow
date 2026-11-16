#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Rewrite a stored (uncompressed) 8-bit RGB PNG with zlib level 9, pixels unchanged.
    png_recompress.py FILE..."""
import struct
import sys
import zlib


def chunks(data):
    p = 8
    while p < len(data):
        n, = struct.unpack('>I', data[p:p + 4])
        yield data[p + 4:p + 8], data[p + 8:p + 8 + n]
        p += 12 + n


def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)


for path in sys.argv[1:]:
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    parts = list(chunks(data))
    ihdr = [d for t, d in parts if t == b'IHDR'][0]
    raw = zlib.decompress(b''.join(d for t, d in parts if t == b'IDAT'))
    out = data[:8] + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    assert zlib.decompress(b''.join(d for t, d in chunks(out) if t == b'IDAT')) == raw
    open(path, 'wb').write(out)
    w, h, depth, ctype = struct.unpack('>IIBB', ihdr[:10])
    print(path, w, h, depth, ctype, len(out))

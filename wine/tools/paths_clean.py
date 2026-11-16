#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Post-link path cleans of the shipped Wine modules (paths6 and paths6b).

    paths_clean.py paths6  elf|pe IN OUT     build-host prefixes (POSTLINK_PATH_MAP + CI prefix)
    paths_clean.py paths6b elf IN OUT        developer env-file names -> unopenable /dev/null paths
    paths_clean.py selftest

This is a re-implementation of the two post-link steps that produced the
JkFlow 1.00 Wine modules from their link outputs. The original one-off tool
was not kept; its receipts describe the method, and this script reproduces
the receipts' output hashes (see wine/BUILD.md, "Post-link steps").

paths6 (no build-host path in the package):
  every occurrence of a build-host directory prefix is replaced in place by a
  neutral prefix of the same length. The prefixes come from the file named by
  POSTLINK_PATH_MAP ("FROM TO" per line, same length, see
  title/postlink-path-map.example); the LLVM release CI prefix
  /home/runner/ -> /build/ci-rt/ is always applied.
    elf  the link ELF of a prx module; the prx is then made from the cleaned
         ELF with the build's own create-fself conversion
         (orbis-win scripts/create-fself-repro.sh ... -ptype fake), which
         changes nothing but the 32-byte SELF digest of the new .oelf.
    pe   a Wine PE file: the same replacement (all hits are in .rdata), then
         the PE CheckSum is recomputed when it was non-zero and valid and left
         0 when it was 0.
  The registry text of the seed (system.reg/user.reg) holds the same prefix
  as \\\\home\\\\<user>\\\\; the seed builder rewrites it to \\\\build\\\\jkflw\\\\.

paths6b (developer env files unopenable), elf only:
  the four env-file names, two developer config files and the Mesa log
  fallback file below are replaced
  by equal-length /dev/null/... names, so fopen fails as for an absent file.

paths7 (developer switch paths) is title/postlink_devpaths.py.
"""
import hashlib
import os
import struct
import sys

CI = [(b'/home/runner/', b'/build/ci-rt/')]
ENV6B = [
    (b'/data/orbis-env.txt', b'/dev/null/s-env.txt'),
    (b'/data/tempest-env.txt', b'/dev/null/est-env.txt'),
    (b'/data/retroarch-env.txt', b'/dev/null/oarch-env.txt'),
    (b'/data/retroarch-glcore-env.txt', b'/dev/null/oarch-glcore-env.txt'),
    (b'/data/blurdev/radv.env', b'/dev/null/dev/radv.env'),
    (b'/data/blurdev/dev.conf', b'/dev/null/dev/dev.conf'),
    (b'/data/blurdev/radv.log', b'/dev/null/dev/radv.log'),   # Mesa log fallback (libvulkan only)
]
for _a, _b in CI + ENV6B:
    assert len(_a) == len(_b), (_a, _b)


def load_map(path):
    pairs = []
    with open(path, 'rb') as f:
        for line in f:
            line = line.split(b'#', 1)[0].strip()
            if not line:
                continue
            a, b = line.split()
            assert len(a) == len(b) and a.startswith(b'/') and b.startswith(b'/'), ('bad map line', line)
            pairs.append((a, b))
    return pairs


def replace(data, pairs):
    n = 0
    for a, b in pairs:
        n += data.count(a)
        data = data.replace(a, b)
    return data, n


def clean_pe(old, pairs):
    """Replace, then recompute the PE CheckSum (field at e_lfanew + 0x58) when it was set."""
    assert old[:2] == b'MZ', 'not a PE file'
    off = struct.unpack_from('<I', old, 0x3c)[0] + 0x58
    stored = struct.unpack_from('<I', old, off)[0]
    raw_len = len(old)

    def csum(d):
        dd = d[:off] + b'\0\0\0\0' + d[off + 4:]
        if len(dd) % 2:
            dd += b'\0'
        s = 0
        for (w,) in struct.iter_unpack('<H', dd):
            s += w
            s = (s & 0xffff) + (s >> 16)
        return (s + raw_len) & 0xffffffff
    valid = stored != 0 and csum(old) == stored
    new, n = replace(old, pairs)
    if stored and valid:
        new = new[:off] + struct.pack('<I', csum(new)) + new[off + 4:]
    elif stored:
        sys.exit('PE CheckSum is set but not valid: refusing to guess')
    return new, n


def main():
    a = sys.argv[1:]
    if a == ['selftest']:
        pairs = [(b'/home/tester/', b'/build/jkflw/')] + CI
        d, n = replace(b'x\0/home/tester/w/a.c\0/home/runner/l/b.c\0', pairs)
        assert d == b'x\0/build/jkflw/w/a.c\0/build/ci-rt/l/b.c\0' and n == 2
        d, n = replace(b'\0/data/orbis-env.txt\0', ENV6B)
        assert d == b'\0/dev/null/s-env.txt\0' and n == 1
        print('paths_clean selftest: ok')
        return
    if len(a) != 4 or a[0] not in ('paths6', 'paths6b') or a[1] not in ('elf', 'pe'):
        sys.exit(__doc__)
    step, kind, src, dst = a
    old = open(src, 'rb').read()
    if step == 'paths6':
        m = os.environ.get('POSTLINK_PATH_MAP')
        if not m:
            sys.exit('set POSTLINK_PATH_MAP to the build-host path map file')
        pairs = load_map(m) + CI
    else:
        assert kind == 'elf', 'paths6b applies to prx link ELFs only'
        pairs = ENV6B
    if kind == 'pe':
        new, n = clean_pe(old, pairs)
    else:
        assert old[:4] == b'\x7fELF', 'not an ELF file'
        new, n = replace(old, pairs)
    if os.path.exists(dst):
        sys.exit('exists: ' + dst)
    open(dst, 'wb').write(new)
    print('%s %d occurrences %s -> %s' % (os.path.basename(src), n, hashlib.sha256(old).hexdigest()[:8],
                                           hashlib.sha256(new).hexdigest()[:8]))


if __name__ == '__main__':
    main()

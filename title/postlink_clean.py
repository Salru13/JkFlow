#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Post-link clean of the BLUR 1.00 title (audit findings 6 and 10), run by
the prepared tree's build-launcher.sh after every build:

    postlink_clean.py run TREE      clean TREE/reloc-build/prx/relocated_wine_app.prx in place
    postlink_clean.py scan FILE...  exit 1 when a file still holds a pattern below
    postlink_clean.py selftest      the patch and proof logic on synthetic bytes

run and scan read the build-host path map from the file named by
POSTLINK_PATH_MAP: one "FROM TO" pair per line, both absolute directory
prefixes of the same length ("#" starts a comment); see
postlink-path-map.example. The LLVM release CI prefix /home/runner/ ->
/build/ci-rt/ is always applied.

Build-host paths in the link ELF (app.shared.elf) are replaced by
equal-length ones, and the orbis-compat developer env-file names (whole
NUL-delimited strings) by same-length paths under /dev/null/ (not a
directory, so fopen fails; orbis_env.o's load_file returns at once on a NULL
fopen, the same as for an absent file). The patched ELF is converted with
the build's own create-fself-repro.sh -ptype fake, after a control run of
the UNCHANGED ELF has reproduced the build's prx byte for byte. Proof: the
new prx differs from the build's only inside the replaced strings and the
fself header (the 32-byte digest, which must equal SHA-256 of the new
.oelf). The eboot (stub.elf, eboot.bin) must have no hit at all. Anything
else fails the build. The build's prx is kept as relocated_wine_app.uncleaned.prx
and the result recorded in postlink-receipt.json.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

CI_PATHS = [
    (b'/home/runner/', b'/build/ci-rt/'),          # LLVM release CI (compiler-rt, libunwind)
]
ENV_FILES = [b'/data/orbis-env.txt', b'/data/tempest-env.txt', b'/data/retroarch-env.txt',
             b'/data/retroarch-glcore-env.txt']
ENV = [(b'\0' + o + b'\0', b'\0' + b'/dev/null/' + o[10:] + b'\0') for o in ENV_FILES]


def load_path_map(path):
    """Build-host prefixes from the POSTLINK_PATH_MAP file: "FROM TO" per line, same length."""
    pairs = []
    with open(path, 'rb') as f:
        for line in f:
            line = line.split(b'#', 1)[0].strip()
            if not line:
                continue
            a, b = line.split()
            assert len(a) == len(b) and a.startswith(b'/') and b.startswith(b'/'), ('bad path map line', line)
            pairs.append((a, b))
    return pairs


def set_paths(paths):
    """The replacement table: the build-host prefixes, the CI prefix and the env files."""
    global PATHS, MAP, SCAN
    PATHS = list(paths) + CI_PATHS
    MAP = PATHS + ENV
    for _a, _b in MAP:
        assert len(_a) == len(_b), (_a, _b)
    SCAN = [a for a, _ in PATHS] + ENV_FILES


def paths_from_env():
    m = os.environ.get('POSTLINK_PATH_MAP')
    if not m:
        sys.exit('set POSTLINK_PATH_MAP to the build-host path map file (see postlink-path-map.example)')
    set_paths(load_path_map(m))


set_paths([])


def sha(b):
    return hashlib.sha256(b).hexdigest()


def occurrences(d):
    occ = []
    for a, b in MAP:
        i = d.find(a)
        while i >= 0:
            occ.append((i, a, b))
            i = d.find(a, i + 1)
    return sorted(occ)


def patch_bytes(d):
    out = bytearray(d)
    occ = occurrences(d)
    for i, a, b in occ:
        out[i:i + len(a)] = b
    out = bytes(out)
    assert not occurrences(out), 'patterns left after the patch'
    return out, occ


def fself_header_end(d):
    """The smallest file offset of a SELF segment: everything before it is header."""
    assert d[:4] == bytes([0x4f, 0x15, 0x3d, 0x1d]), 'not a SELF'
    nseg = struct.unpack_from('<H', d, 0x18)[0]
    offs = [struct.unpack_from('<Q', d, 0x20 + 32 * k + 8)[0] for k in range(nseg)]
    offs = [x for x in offs if x]
    assert offs, 'SELF without segments'
    return min(offs)


def prove(old, new, fself, digest=None):
    """Every differing byte lies in a replaced occurrence (found in old) or,
    for a SELF, in its header; with digest, the header bytes that differ are
    exactly the 32 bytes of that digest. Returns the accounting; raises on
    anything unexplained."""
    assert len(old) == len(new), ('size', len(old), len(new))
    allowed = bytearray(len(old))
    occ = occurrences(old)
    for i, a, b in occ:
        assert new[i:i + len(a)] == b, ('occurrence not replaced', i, a)
        for k in range(i, i + len(a)):
            allowed[k] = 1
    hdr = fself_header_end(old) if fself else 0
    for k in range(hdr):
        if not allowed[k]:
            allowed[k] = 2
    diff = [k for k in range(len(old)) if old[k] != new[k]]
    bad = [k for k in diff if not allowed[k]]
    in_hdr = [k for k in diff if allowed[k] == 2]
    res = dict(bytes=len(old), occurrences=len(occ), differing_bytes=len(diff),
               in_strings=len(diff) - len(in_hdr) - len(bad), in_fself_header=len(in_hdr),
               unexplained=len(bad), first_unexplained=bad[:8])
    assert not bad, res
    if digest is not None:
        at = new.find(digest, 0, hdr)
        assert at >= 0, 'the header does not carry SHA-256 of the new .oelf'
        assert all(at <= k < at + 32 for k in in_hdr), ('header bytes differ outside the digest', res)
        res['digest_offset'] = at
    return res


def env_xref(elf_path):
    """No code or relocation points INSIDE the first 10 bytes of an env-file
    string (the bytes that change); pointers to its start or its unchanged tail
    are fine. Needs llvm-readelf-18 and llvm-objdump-18."""
    d = open(elf_path, 'rb').read()
    secs = []
    for line in subprocess.check_output(['llvm-readelf-18', '-S', '-W', elf_path], text=True).splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', line)
        if m:
            secs.append((m[1], int(m[2], 16), int(m[3], 16), int(m[4], 16)))
    dis = subprocess.run(['llvm-objdump-18', '-d', '--no-show-raw-insn', elf_path], capture_output=True,
                         text=True).stdout
    refs = [int(m, 16) for m in re.findall(r'# 0x([0-9a-f]+)', dis)]
    rel = subprocess.run(['llvm-readelf-18', '-r', '-W', elf_path], capture_output=True, text=True).stdout
    refs += [int(m, 16) for m in re.findall(r'R_X86_64_\w+\s+(?:[0-9a-f]+\s+\S+\s+\+\s+)?([0-9a-f]+)\s*$', rel, re.M)]
    found = {}
    for s in ENV_FILES:
        i = d.find(b'\0' + s + b'\0')
        if i < 0:
            continue
        off = i + 1
        sec = [x for x in secs if x[1] and x[2] <= off < x[2] + x[3]]
        assert sec, ('not in an allocated section', s)
        va = sec[0][1] + off - sec[0][2]
        inside = sorted(hex(r) for r in refs if va < r < va + 10)
        assert not inside, ('a reference into the changed bytes of', s, inside)
        found[s.decode()] = dict(section=sec[0][0], vaddr=hex(va), refs_to_start=sum(1 for r in refs if r == va))
    return found


def convert(orbis, elf, outdir):
    """The build's own conversion (build-relocatable-wine-ps4.sh): the ELF is
    copied as app.shared.elf (create-fself embeds that name)."""
    if os.path.exists(outdir):
        shutil.rmtree(outdir)
    os.makedirs(outdir)
    shutil.copyfile(elf, os.path.join(outdir, 'app.shared.elf'))
    oo = os.path.join(orbis, 'sdk/orbis-sdk-v1/sdk')
    prx_dir = os.path.dirname(os.path.abspath(elf))
    lib_path = ':'.join([prx_dir, os.path.join(orbis, 'build/ps4-wine-sdk'), os.path.join(oo, 'lib')])
    cmd = ['sh', os.path.join(orbis, 'scripts/create-fself-repro.sh'), os.path.join(oo, 'bin/linux/create-fself'),
           '-in', os.path.join(outdir, 'app.shared.elf'), '-out', os.path.join(outdir, 'app.oelf'),
           '-lib', os.path.join(outdir, 'relocated_wine_app.prx'), '-libname', 'relocated_wine_app.prx',
           '-ptype', 'fake', '-library-path', lib_path]
    env = dict(os.environ, OO_PS4_TOOLCHAIN=oo)
    with open(os.path.join(outdir, 'create-fself.log'), 'w') as log:
        subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT, env=env)
    return (open(os.path.join(outdir, 'relocated_wine_app.prx'), 'rb').read(),
            open(os.path.join(outdir, 'app.oelf'), 'rb').read())


def hits(path):
    d = open(path, 'rb').read()
    return {s.decode(): d.count(s) for s in SCAN if d.count(s)}


def run(tree):
    build = os.path.join(tree, 'reloc-build')
    prx_dir = os.path.join(build, 'prx')
    orbis = os.path.join(tree, 'orbis')
    elf = os.path.join(prx_dir, 'app.shared.elf')
    shipped_path = os.path.join(prx_dir, 'relocated_wine_app.prx')
    work = os.path.join(prx_dir, 'postlink')
    receipt = dict(schema=1, kind='blur100-postlink-clean', findings=['6', '10'])
    for f in ('eboot.bin', 'stub.elf'):
        h = hits(os.path.join(build, f))
        assert not h, ('the eboot has hits, not handled here', f, h)
    receipt['eboot_sha256'] = sha(open(os.path.join(build, 'eboot.bin'), 'rb').read())
    receipt['eboot_hits'] = {}
    shipped = open(shipped_path, 'rb').read()
    elf_bytes = open(elf, 'rb').read()
    os.makedirs(work, exist_ok=True)
    control, _ = convert(orbis, elf, os.path.join(work, 'control'))
    assert control == shipped, ('control conversion does not reproduce the build', sha(control), sha(shipped))
    receipt['control'] = dict(reproduces=True, prx_sha256=sha(shipped))
    receipt['env_strings'] = env_xref(elf)
    patched, occ = patch_bytes(elf_bytes)
    receipt['elf'] = dict(old_sha256=sha(elf_bytes), new_sha256=sha(patched), hits_before=hits(elf),
                          proof=prove(elf_bytes, patched, False))
    clean_elf = os.path.join(work, 'app.shared.elf')
    open(clean_elf, 'wb').write(patched)
    new, oelf = convert(orbis, clean_elf, os.path.join(work, 'clean'))
    receipt['prx'] = dict(old_sha256=sha(shipped), new_sha256=sha(new), oelf_sha256=sha(oelf),
                          proof=prove(shipped, new, True, hashlib.sha256(oelf).digest()))
    assert not occurrences(new) and not hits_bytes(new), 'patterns left in the new prx'
    shutil.copyfile(shipped_path, os.path.join(prx_dir, 'relocated_wine_app.uncleaned.prx'))
    tmp = shipped_path + '.new'
    open(tmp, 'wb').write(new)
    os.replace(tmp, shipped_path)
    receipt['installed'] = shipped_path
    with open(os.path.join(prx_dir, 'postlink-receipt.json'), 'w') as f:
        json.dump(receipt, f, indent=1)
    print('postlink clean: prx %s -> %s (%d strings, %d header bytes), eboot %s clean'
          % (sha(shipped)[:8], sha(new)[:8], receipt['prx']['proof']['occurrences'],
             receipt['prx']['proof']['in_fself_header'], receipt['eboot_sha256'][:8]))


def hits_bytes(d):
    return [s for s in SCAN if s in d]


def selftest():
    n = 0
    set_paths([(b'/home/tester/', b'/build/jkflw/')])   # a synthetic build host
    body = (b'\x7fELF' + b'\0' * 60 + b'x/home/runner/work/llvm/a.c\0' + b'\0/data/orbis-env.txt\0'
            + b'tail\0/data/tempest-env.txt\0/data/retroarch-env.txt\0/data/retroarch-glcore-env.txt\0'
            + b'keep /data/orbis-env.txt.bak\0' + b'/home/tester/x\0' + b'z' * 40)
    new, occ = patch_bytes(body)
    n += 1; assert len(new) == len(body) and len(occ) == 6, occ
    n += 1; assert b'/build/ci-rt/work/llvm/a.c' in new and b'/build/jkflw/x' in new
    n += 1; assert b'\0/dev/null/s-env.txt\0' in new and b'\0/dev/null/oarch-glcore-env.txt\0' in new
    n += 1; assert b'/data/orbis-env.txt.bak' in new, 'only whole strings are env-file names'
    n += 1; assert not hits_bytes(new.replace(b'/data/orbis-env.txt.bak', b'')), hits_bytes(new)
    res = prove(body, new, False)
    n += 1; assert res['unexplained'] == 0 and res['in_strings'] == res['differing_bytes']
    bad = bytearray(new); bad[2] ^= 1
    try:
        prove(body, bytes(bad), False); ok = False
    except AssertionError:
        ok = True
    n += 1; assert ok, 'a byte outside the strings must fail the proof'
    # A SELF: header (digest inside), then one segment carrying the body.
    def self_file(payload, digest):
        hdr = bytearray(0x200)
        hdr[0:4] = bytes([0x4f, 0x15, 0x3d, 0x1d])
        struct.pack_into('<H', hdr, 0x18, 1)
        struct.pack_into('<Q', hdr, 0x20 + 8, len(hdr))
        hdr[0x100:0x120] = digest
        return bytes(hdr) + payload
    old_self = self_file(body, hashlib.sha256(b'old').digest())
    dig = hashlib.sha256(b'new').digest()
    new_self = self_file(new, dig)
    res = prove(old_self, new_self, True, dig)
    n += 1; assert res['in_fself_header'] <= 32 and res['digest_offset'] == 0x100
    stray = bytearray(new_self); stray[0x40] ^= 1
    try:
        prove(old_self, bytes(stray), True, dig); ok = False
    except AssertionError:
        ok = True
    n += 1; assert ok, 'a header byte outside the digest must fail'
    try:
        prove(old_self, new_self, True, hashlib.sha256(b'other').digest()); ok = False
    except AssertionError:
        ok = True
    n += 1; assert ok, 'the digest must be the new .oelf\'s'
    n += 1; assert len(ENV[0][1]) == len(ENV[0][0]) and ENV[0][1].startswith(b'\0/dev/null/')
    print('postlink_clean selftest: %d checks, 0 failed' % n)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'run' and len(sys.argv) == 3:
        paths_from_env()
        run(sys.argv[2])
    elif cmd == 'scan' and len(sys.argv) > 2:
        paths_from_env()
        left = {p: hits(p) for p in sys.argv[2:]}
        left = {p: h for p, h in left.items() if h}
        print(json.dumps(left) if left else 'postlink scan: clean')
        sys.exit(1 if left else 0)
    elif cmd == 'selftest':
        selftest()
    else:
        sys.exit(__doc__)

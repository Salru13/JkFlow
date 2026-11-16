#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Post-link clean of developer switch paths in the shipped Wine modules
(order 290; same method as the paths6/paths6b and postlink_clean.py cleans):

    postlink_devpaths.py run --orbis ORBIS --out OUT [--stub LIB.prx=ELF]... NAME=ELF=PRX...
    postlink_devpaths.py scan FILE...     exit 1 when a file still holds a pattern below
    postlink_devpaths.py selftest         the patch and proof logic on synthetic bytes

ORBIS is the orbis tree holding scripts/create-fself-repro.sh and
sdk/orbis-sdk-v1/sdk; OUT must not exist. For each NAME, ELF is the link ELF
that produced PRX (the pinned module), under the basename it was converted
from (create-fself embeds the -in basename, so it is kept). Each --stub ELF
is put into the -library-path stubs folder as LIB.prx and LIB.so (as
prx-ps4.sh does for a module's own dependencies: create-fself opens each
DT_NEEDED module to attribute its imports); the same stubs serve the control
and the cleaned conversion, so the control proves they reproduce the build.

In every NUL-delimited string of the ELF that starts with, or holds as a path
(after a space, quote, '=' or '('), the prefix /data/blurdev/ (14 bytes), the
prefix is replaced by /dev/null/bld/ (14 bytes), and /data/orbis-watchdog.txt
(24) by /dev/null/orbis-wdog.txt (24). /dev/null is not a directory, so every
open, stat or mkdir under it fails as if the folder were absent. Nothing else
changes. Per module:
  control  the UNCHANGED ELF converted with create-fself-repro.sh -ptype fake
           reproduces PRX byte for byte (else the module stops: no guess);
  xref     no code reference or relocation points inside a changed range
           (a pointer to the start of a changed string is fine);
  elf      the patched ELF differs only inside the replaced bytes;
  prx      the new prx differs from PRX only inside the replaced bytes and the
           fself header bytes of the 32-byte digest, which equals SHA-256 of
           the new .oelf; 0 unexplained bytes;
  scan     the new prx holds no /data/blurdev and no /data/orbis-watchdog.
OUT/prx/NAME/{control,elf,new} and OUT/paths7-receipt.json are written.
llvm-objcopy (for create-fself-repro.sh), llvm-readelf-18 and llvm-objdump-18
must be on PATH.
"""
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

PREFIX = (b'/data/blurdev/', b'/dev/null/bld/')
WHOLE = (b'/data/orbis-watchdog.txt', b'/dev/null/orbis-wdog.txt')
MAP = [PREFIX, WHOLE]
for _a, _b in MAP:
    assert len(_a) == len(_b), (_a, _b)
SCAN = [b'/data/blurdev', b'/data/orbis-watchdog']
PATH_LEAD = b' "\'=(\t:'   # a byte before the prefix that makes it a path inside a text


def sha(b):
    return hashlib.sha256(b).hexdigest()


def string_at(d, i):
    """The NUL-delimited string around offset i: (start, end)."""
    s = d.rfind(b'\0', 0, i) + 1
    e = d.find(b'\0', i)
    return s, (len(d) if e < 0 else e)


def occurrences(d):
    """(offset, old, new, string_start, string_end) of every replaced pattern."""
    occ = []
    for a, b in MAP:
        i = d.find(a)
        while i >= 0:
            s, e = string_at(d, i)
            if i == s or d[i - 1:i] in [bytes([c]) for c in PATH_LEAD]:
                occ.append((i, a, b, s, e))
            i = d.find(a, i + 1)
    return sorted(occ)


def patch_bytes(d):
    out = bytearray(d)
    occ = occurrences(d)
    for i, a, b, _s, _e in occ:
        out[i:i + len(a)] = b
    out = bytes(out)
    assert not occurrences(out), 'patterns left after the patch'
    return out, occ


def scan_bytes(d):
    """Every pattern, NUL-delimited or not, also in UTF-16LE."""
    res = {}
    for s in SCAN:
        for enc, pat in (('ascii', s), ('utf16le', s.decode().encode('utf-16-le'))):
            n = d.count(pat)
            if n:
                res['%s/%s' % (s.decode(), enc)] = n
    return res


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
    for a SELF, in its header; with digest, the header bytes that differ lie
    in that 32-byte digest. Returns the accounting; raises on anything
    unexplained."""
    assert len(old) == len(new), ('size', len(old), len(new))
    allowed = bytearray(len(old))
    occ = occurrences(old)
    for i, a, b, _s, _e in occ:
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
               fself_header_end=hdr, unexplained=len(bad), first_unexplained=bad[:8])
    assert not bad, res
    if digest is not None:
        at = new.find(digest, 0, hdr)
        assert at >= 0, 'the header does not carry SHA-256 of the new .oelf'
        assert all(at <= k < at + 32 for k in in_hdr), ('header bytes differ outside the digest', res)
        res['digest_offset'] = at
    return res


def xref(elf_path):
    """No code or relocation points inside a changed byte range, except at the
    first changed byte when that is the start of its string."""
    d = open(elf_path, 'rb').read()
    secs = []
    for line in subprocess.check_output(['llvm-readelf-18', '-S', '-W', elf_path], text=True).splitlines():
        m = re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', line)
        if m:
            secs.append((m[1], int(m[2], 16), int(m[3], 16), int(m[4], 16)))
    dis = subprocess.run(['llvm-objdump-18', '-d', '--no-show-raw-insn', elf_path], capture_output=True,
                         text=True).stdout
    code = [int(m, 16) for m in re.findall(r'# 0x([0-9a-f]+)', dis)]
    rel = subprocess.run(['llvm-readelf-18', '-r', '-W', elf_path], capture_output=True, text=True).stdout
    relocs = [int(m, 16) for m in
              re.findall(r'R_X86_64_\w+\s+(?:[0-9a-f]+\s+\S+\s+\+\s+)?([0-9a-f]+)\s*$', rel, re.M)]
    out = []
    for i, a, _b, s, _e in occurrences(d):
        sec = [x for x in secs if x[1] and x[2] <= i < x[2] + x[3]]
        rec = dict(offset=i, string=d[s:_e].decode('latin-1'))
        if not sec:
            rec['section'] = [x[0] for x in secs if x[2] <= i < x[2] + x[3]] or None
            rec['allocated'] = False
            out.append(rec)
            continue
        va = sec[0][1] + i - sec[0][2]
        lo = va + 1 if i == s else va   # a pointer to the string's own start is fine
        inside_code = sorted(hex(r) for r in code if lo <= r < va + len(a))
        inside_rel = sorted(hex(r) for r in relocs if lo <= r < va + len(a))
        rec.update(section=sec[0][0], allocated=True, vaddr=hex(va), string_vaddr=hex(sec[0][1] + s - sec[0][2]),
                   code_refs_string_start=sum(1 for r in code if r == sec[0][1] + s - sec[0][2]),
                   reloc_string_start=sum(1 for r in relocs if r == sec[0][1] + s - sec[0][2]),
                   code_refs_interior=inside_code, reloc_interior=inside_rel)
        assert not inside_code and not inside_rel, ('a reference into the changed bytes', rec)
        out.append(rec)
    return out


def convert(orbis, elf, outdir, name, stubs=()):
    """create-fself-repro.sh -ptype fake, the ELF under its own basename."""
    assert not os.path.exists(outdir), ('exists', outdir)
    os.makedirs(os.path.join(outdir, 'stubs'))
    for lib, src in stubs:
        for ext in ('.prx', '.so'):
            shutil.copyfile(src, os.path.join(outdir, 'stubs', lib[:-4] + ext))
    base = os.path.basename(elf)
    shutil.copyfile(elf, os.path.join(outdir, base))
    oo = os.path.join(orbis, 'sdk/orbis-sdk-v1/sdk')
    cmd = ['sh', os.path.join(orbis, 'scripts/create-fself-repro.sh'), os.path.join(oo, 'bin/linux/create-fself'),
           '-in', base, '-out', name + '.oelf', '-lib', name + '.prx', '-libname', name + '.prx',
           '-ptype', 'fake', '-library-path', os.path.join(os.path.abspath(outdir), 'stubs') + ':' + os.path.join(oo, 'lib')]
    env = dict(os.environ, OO_PS4_TOOLCHAIN=oo)
    with open(os.path.join(outdir, 'convert.log'), 'w') as log:
        subprocess.run(cmd, check=True, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=outdir)
    os.remove(os.path.join(outdir, base))
    shutil.rmtree(os.path.join(outdir, 'stubs'))   # copies of --stub inputs (recorded in the receipt)
    return (open(os.path.join(outdir, name + '.prx'), 'rb').read(),
            open(os.path.join(outdir, name + '.oelf'), 'rb').read())


def clean_module(orbis, out, name, elf, prx, stubs):
    work = os.path.join(out, 'prx', name)
    os.makedirs(work)
    elf_bytes = open(elf, 'rb').read()
    shipped = open(prx, 'rb').read()
    rec = dict(source_elf=elf, source_elf_sha256=sha(elf_bytes), source_prx=prx, old_sha256=sha(shipped),
               scan_before=scan_bytes(shipped))
    rec['stubs'] = {lib: dict(source=src, sha256=sha(open(src, 'rb').read())) for lib, src in stubs}
    control, _ = convert(orbis, elf, os.path.join(work, 'control'), name, stubs)
    if control != shipped:
        rec.update(status='SKIPPED', reason='control conversion does not reproduce the pinned prx',
                   control_sha256=sha(control))
        return rec
    rec['control'] = dict(reproduces=True, prx_sha256=sha(control))
    rec['xref'] = xref(elf)
    patched, occ = patch_bytes(elf_bytes)
    rec['edits_elf'] = [dict(offset=i, old=d_old.decode(), new=d_new.decode(),
                             string_before=elf_bytes[s:e].decode('latin-1'),
                             string_after=patched[s:e].decode('latin-1'))
                        for i, d_old, d_new, s, e in occ]
    rec['elf'] = dict(new_sha256=sha(patched), proof=prove(elf_bytes, patched, False))
    os.makedirs(os.path.join(work, 'elf'))
    clean_elf = os.path.join(work, 'elf', os.path.basename(elf))
    open(clean_elf, 'wb').write(patched)
    new, oelf = convert(orbis, clean_elf, os.path.join(work, 'new'), name, stubs)
    rec['edits_prx'] = [dict(offset=i, old=d_old.decode(), new=d_new.decode(),
                             string_after=new[s:e].decode('latin-1'))
                        for i, d_old, d_new, s, e in occurrences(shipped)]
    rec['prx'] = dict(new_sha256=sha(new), oelf_sha256=sha(oelf),
                      proof=prove(shipped, new, True, hashlib.sha256(oelf).digest()))
    rec['scan_after'] = scan_bytes(new)
    assert not rec['scan_after'], ('patterns left in the new prx', rec['scan_after'])
    rec['status'] = 'CLEANED'
    rec['output'] = os.path.join(work, 'new', name + '.prx')
    return rec


def run(argv):
    args = dict(orbis=None, out=None)
    mods = []
    stubs = []
    it = iter(argv)
    for a in it:
        if a in ('--orbis', '--out'):
            args[a[2:]] = next(it)
        elif a == '--stub':
            lib, src = next(it).split('=', 1)
            assert lib.endswith('.prx'), lib
            stubs.append((lib, os.path.abspath(src)))
        else:
            name, elf, prx = a.split('=', 2)
            mods.append((name, os.path.abspath(elf), os.path.abspath(prx)))
    assert args['orbis'] and args['out'] and mods, __doc__
    args['orbis'] = os.path.abspath(args['orbis'])
    out = os.path.abspath(args['out'])
    assert not os.path.exists(out), ('exists', out)
    os.makedirs(out)
    receipt = dict(schema=1, kind='blur100-paths7', order='290', status='BUILT_NOT_PS4_TESTED',
                   finding='developer switch paths a player could create over FTP',
                   mapping={a.decode(): b.decode() for a, b in MAP},
                   rule='prefix /data/blurdev/ wherever a NUL-delimited string starts with it or holds it as a '
                        'path; /data/orbis-watchdog.txt whole; equal length',
                   method='equal-length replacement in the link ELF, then create-fself-repro.sh -ptype fake under '
                          'the same ELF basename; control: the unchanged ELF reproduces the pinned prx byte for byte; '
                          'only non-string difference: the 32-byte SELF digest = SHA-256 of the new .oelf',
                   modules={})
    rc = 0
    for name, elf, prx in mods:
        try:
            rec = clean_module(args['orbis'], out, name, elf, prx, stubs)
        except Exception as e:   # recorded and reported, never shipped
            rec = dict(status='FAILED', error=repr(e))
        receipt['modules'][name] = rec
        if rec['status'] != 'CLEANED':
            rc = 1
        print('%-12s %s %s -> %s  edits=%s diff=%s hdr=%s unexplained=%s' % (
            name, rec['status'], rec.get('old_sha256', '?')[:8], rec.get('prx', {}).get('new_sha256', '?')[:8],
            rec.get('prx', {}).get('proof', {}).get('occurrences'),
            rec.get('prx', {}).get('proof', {}).get('differing_bytes'),
            rec.get('prx', {}).get('proof', {}).get('in_fself_header'),
            rec.get('prx', {}).get('proof', {}).get('unexplained')))
    with open(os.path.join(out, 'paths7-receipt.json'), 'w') as f:
        json.dump(receipt, f, indent=1)
    sys.exit(rc)


def selftest():
    n = 0
    body = (b'\x7fELF' + b'\0' * 60 + b'/data/blurdev/override\0' + b'/data/blurdev/pw_sched\0'
            + b'/data/orbis-watchdog.txt\0' + b'set X=1 in /data/blurdev/dev.conf to see it\0'
            + b'keep/data/blurdev/x\0' + b'/data/orbis-watchdog.txt.old\0' + b'/data/orbis-compat/lib\0'
            + b'z' * 40)
    new, occ = patch_bytes(body)
    n += 1; assert len(new) == len(body) and len(occ) == 5, occ
    n += 1; assert b'\0/dev/null/bld/override\0/dev/null/bld/pw_sched\0/dev/null/orbis-wdog.txt\0' in new
    n += 1; assert b'in /dev/null/bld/dev.conf to see' in new, 'a path inside a text is replaced'
    n += 1; assert b'keep/data/blurdev/x' in new, 'not a path: left alone'
    n += 1; assert b'/data/orbis-compat/lib' in new, 'orbis-compat is out of scope'
    n += 1; assert b'/dev/null/orbis-wdog.txt.old' in new
    res = prove(body, new, False)
    n += 1; assert res['unexplained'] == 0 and 0 < res['in_strings'] == res['differing_bytes'], res
    bad = bytearray(new); bad[2] ^= 1
    try:
        prove(body, bytes(bad), False); ok = False
    except AssertionError:
        ok = True
    n += 1; assert ok, 'a byte outside the strings must fail the proof'
    n += 1; assert scan_bytes(b'x' + '/data/blurdev/a'.encode('utf-16-le')) == {'/data/blurdev/utf16le': 1}

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
    print('postlink_devpaths selftest: %d checks, 0 failed' % n)


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'run':
        run(sys.argv[2:])
    elif cmd == 'scan' and len(sys.argv) > 2:
        left = {p: scan_bytes(open(p, 'rb').read()) for p in sys.argv[2:]}
        left = {p: h for p, h in left.items() if h}
        print(json.dumps(left) if left else 'postlink_devpaths scan: clean')
        sys.exit(1 if left else 0)
    elif cmd == 'selftest':
        selftest()
    else:
        sys.exit(__doc__)

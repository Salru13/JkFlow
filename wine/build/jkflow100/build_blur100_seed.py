#!/usr/bin/env python3
# Copyright (c) 2026 XxXJoker13XxX. All rights reserved.
"""BLUR 1.00 shipping seed (plan Task E5): users\\BLUR, wowblur.dll, BLUR profile paths.

Starts from the shipped native287 seed (order 284: END b49216c4, index f8f52c50) and its host
manifest, exactly as native287-seed-prepare.py built it, and changes only:
  - the Wine user folder drive_c/users/prospero -> drive_c/users/BLUR (36 folders);
  - user.reg, userdef.reg, system.reg: the user name and paths, and the default WoW64 CPU
    [Software\\Microsoft\\Wow64\\x86] @="wowblur.dll";
  - drive_c/windows/system32/wowprospero.dll -> wowblur.dll (blur100-wow build) and
    system32/wow64.dll -> the blur100-wow build (it names wowblur.dll);
  - the seed profile text: game at Z:\\data\\BLUR\\game (design section 7), no old-name comment.
syswow64/advapi32.dll and every other file are kept byte for byte.

Controls: the helper rebuilds the shipped seed from the unchanged manifest (END b49216c4,
index f8f52c50); the shipped seed is rejected against the BLUR runtime and the BLUR seed is
accepted by `native245_stage check`.
Output: blur100-seed/ (refuses to reuse it). Usage (WSL): python3 build_blur100_seed.py
"""
import hashlib, json, re, subprocess, sys
import os  # JKFLOW_BUILD_ROOT
from pathlib import Path

B = Path(os.environ['JKFLOW_BUILD_ROOT'])
OUT = B / 'blur100-seed'
SRC = B / 'native287-seed'                       # shipped seed recipe output (order 284 seed)
SHIPPED_SEED = SRC / 'seed'
# native287-seed/runtime links into native266-package/pkg-stage, which no longer exists. The
# runtime files are taken from the unpacked order 284 package instead (PkgTool pkg_extract of
# BLUR00001 7a3afbe4); the helper checks every size and hash against the manifest.
RUNTIME = B / 'blur100-inventory/extracted/uroot/win/wine/lib/wine'
HELPER = B / 'native266-package/native248_stage'
HELPER_SHA = '4ca0404ed52b9f99a6d85e95ed6b2d28eee9c4329aa858330c6e3863c49fc5a9'
SHIPPED_END = 'b49216c4c40116844d1c078c0435878521f336279ab7b192d20c56731418baac'
SHIPPED_INDEX = 'f8f52c507b15e7d87c78cd5fe42a12527e6852be01d1b65afbd6f2f8ffdb02ef'
SHIPPED_MANIFEST_END = 'd456fc2a8a4930197754e9cbefdb392cd5c0e77405a5130484de9439c07ea69d'
WOW = B / 'blur100-wow/blur100'
WOW_PINS = {'wow64.dll': '8540553a075b1b083b2a0e4e4502b4b865550a05ac069a21be52128e4c61e478',
            'wowblur.dll': '4f4cb082673e9e5c44c7f48f5cc212c84781bcf5dc227153f25bff42545f497b'}
ADVAPI32 = '1fa1b884'
OLD = 'pros' + 'pero'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
commands = []


def sealed(p, header):
    d = Path(p).read_bytes()
    m = re.fullmatch(rb'(.*\n)END ([0-9a-f]{64})\n', d, re.S)
    assert m and d.startswith((header + '\n').encode()) and hashlib.sha256(m[1]).hexdigest().encode() == m[2], p
    return m[1].decode().splitlines(), m[2].decode()


def link(src, dst):
    assert src.exists(), src
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.symlink_to(src.resolve())


def run(label, args, want=0):
    p = subprocess.run(list(map(str, args)), text=True, capture_output=True)
    commands.append(dict(label=label, argv=list(map(str, args)), exit=p.returncode, output=(p.stdout + p.stderr)[-4000:]))
    assert (p.returncode == 0) == (want == 0), (label, p.stdout[-2000:], p.stderr[-2000:])
    print(label, p.returncode, flush=True)


def reg_edit(text, name):
    """The user name/paths and the default WoW64 CPU only; every replacement counted."""
    n_user = text.count('\\\\users\\\\' + OLD)
    text = text.replace('\\\\users\\\\' + OLD, '\\\\users\\\\BLUR')
    n_name = text.count('"USERNAME"="' + OLD + '"')
    text = text.replace('"USERNAME"="' + OLD + '"', '"USERNAME"="BLUR"')
    n_cpu = text.count('@="wow' + OLD + '.dll"')
    text = text.replace('@="wow' + OLD + '.dll"', '@="wowblur.dll"')
    assert OLD not in text.lower(), (name, [l for l in text.splitlines() if OLD in l.lower()][:5])
    return text, dict(user_paths=n_user, username=n_name, wow64_cpu=n_cpu)


PROFILE_EDITS = [
    ('; Original game stays at /data/Pc/Blur. The prefix needs dosdevices/z: -> /.\n',
     '; The game is the player\'s copy in /data/BLUR/game. The prefix needs dosdevices/z: -> /.\n'),
    ('executable = Z:\\data\\Pc\\Blur\\Blur.exe\n', 'executable = Z:\\data\\BLUR\\game\\Blur.exe\n'),
    ('working_directory = Z:\\data\\Pc\\Blur\n', 'working_directory = Z:\\data\\BLUR\\game\n'),
]


def main():
    if OUT.exists():
        sys.exit('refusing to reuse ' + str(OUT))
    assert sha(HELPER) == HELPER_SHA
    for n, h in WOW_PINS.items():
        assert sha(WOW / n) == h, n
    rows, end = sealed(SHIPPED_SEED / 'seed245/blur-native248/seed.idx', 'N245-SEED 1')
    assert end == SHIPPED_END and sha(SHIPPED_SEED / 'seed245/blur-native248/seed.idx') == SHIPPED_INDEX
    lines, mend = sealed(SRC / 'blur-native248.manifest', 'PW239-MANIFEST 1')
    assert mend == SHIPPED_MANIFEST_END
    OUT.mkdir()
    objects_old = SHIPPED_SEED / 'seed245/blur-native248/objects'

    # ---- control: the shipped seed from the unchanged manifest
    ctl = OUT / 'control-seed'
    cobj = ctl / 'seed245/blur-native248/objects'
    cobj.mkdir(parents=True)
    for src in objects_old.iterdir():
        link(src, cobj / src.name)
    prof_old = [l for l in lines if l.startswith('P\t')][0].split('\t')
    # The shipped package's i386 winevulkan.dll (7ba22002) is not the one the shipped seed pins
    # (2507a7d8, still in native287-seed/runtime): the control union takes that one file from there.
    crt = OUT / 'control-runtime'
    stale = []
    for arch in ('i386-windows', 'x86_64-windows'):
        for src in sorted((RUNTIME / arch).iterdir()):
            alt = SRC / 'runtime' / arch / src.name
            if alt.exists() and sha(alt) != sha(src):
                link(alt, crt / arch / src.name); stale.append(dict(file=arch + '/' + src.name, seed_pin=sha(alt), package=sha(src)))
            else:
                link(src, crt / arch / src.name)
    lines = [l.replace(str(SRC / 'runtime') + '/', str(crt) + '/') for l in lines]
    cbody = ('\n'.join(lines) + '\n').encode()
    cmend = hashlib.sha256(cbody).hexdigest()
    (OUT / 'control.manifest').write_bytes(cbody + b'END ' + cmend.encode() + b'\n')
    lines = [l.replace(str(crt) + '/', str(RUNTIME) + '/') for l in lines]
    run('control seed', [HELPER, 'seed', '--manifest', OUT / 'control.manifest', '--pkg-lib', crt,
                         '--out', ctl, '--expect-manifest-end', cmend, '--expect-profile', prof_old[2]])
    crow, cend = sealed(ctl / 'seed245/blur-native248/seed.idx', 'N245-SEED 1')
    # The index carries manifest_end, the hash of the host manifest, whose source paths had to move
    # (see RUNTIME); every other line must equal the shipped index.
    strip_me = lambda rs: [r for r in rs if not r.startswith('manifest_end ')]
    assert strip_me(crow) == strip_me(rows), 'control rows differ from the shipped seed'
    print('PASS control reproduces every shipped seed row (manifest_end differs: host paths)', cend, flush=True)

    # ---- runtime union with the BLUR WoW64 modules
    union = OUT / 'runtime'
    swapped = []
    for arch in ('i386-windows', 'x86_64-windows'):
        for src in sorted((RUNTIME / arch).iterdir()):
            if arch == 'x86_64-windows' and src.name == 'wow' + OLD + '.dll':
                link(WOW / 'wowblur.dll', union / arch / 'wowblur.dll'); swapped.append(arch + '/wowblur.dll (was ' + src.name + ')')
            elif arch == 'x86_64-windows' and src.name == 'wow64.dll':
                link(WOW / 'wow64.dll', union / arch / 'wow64.dll'); swapped.append(arch + '/wow64.dll')
            else:
                link(src, union / arch / src.name)

    # ---- new objects: registry files and profile
    newobj = OUT / 'new-objects'
    newobj.mkdir()
    reg_report, out_lines, profile, refreshed = {}, [], None, []
    for line in lines:
        f = line.split('\t')
        if f[0] == 'D' and f[1].split('/')[:3] == ['drive_c', 'users', OLD]:
            f[1] = '/'.join(['drive_c', 'users', 'BLUR'] + f[1].split('/')[3:])
        elif f[0] == 'F':
            if f[3] in ('user.reg', 'userdef.reg', 'system.reg'):
                text, rep = reg_edit(Path(f[4]).read_text(encoding='utf-8', errors='surrogateescape'), f[3])
                p = newobj / f[3]
                p.write_text(text, encoding='utf-8', errors='surrogateescape')
                reg_report[f[3]] = dict(old_sha256=f[2], new_sha256=sha(p), bytes=p.stat().st_size, **rep)
                f[1:3] = [str(p.stat().st_size), sha(p)]
                f[4] = str(p)
            elif f[3] == 'drive_c/windows/system32/wow' + OLD + '.dll':
                f[3] = 'drive_c/windows/system32/wowblur.dll'
                f[4] = str(union / 'x86_64-windows/wowblur.dll')
                f[1:3] = [str((WOW / 'wowblur.dll').stat().st_size), WOW_PINS['wowblur.dll']]
            elif f[3] == 'drive_c/windows/system32/wow64.dll':
                f[4] = str(union / 'x86_64-windows/wow64.dll')
                f[1:3] = [str((WOW / 'wow64.dll').stat().st_size), WOW_PINS['wow64.dll']]
            elif f[4].startswith(str(RUNTIME)):
                f[4] = str(union) + f[4][len(str(RUNTIME)):]
                if sha(f[4]) != f[2]:   # pin what the package ships (see stale_in_shipped_seed)
                    refreshed.append(dict(path=f[3], old_sha256=f[2], new_sha256=sha(f[4])))
                    f[1:3] = [str(Path(f[4]).stat().st_size), sha(f[4])]
            assert OLD not in f[3].lower(), f
        elif f[0] == 'P':
            t = Path(f[3]).read_text()
            for a, b in PROFILE_EDITS:
                assert t.count(a) == 1, a
                t = t.replace(a, b)
            assert OLD not in t.lower() and '/data/pc/blur' not in t.lower() and 'data\\pc\\blur' not in t.lower(), t
            profile = newobj / 'profile'
            profile.write_text(t)
            f[1:3] = [str(profile.stat().st_size), sha(profile)]
            f[3] = str(profile)
        out_lines.append('\t'.join(f))
    assert profile is not None
    # D rows must stay sorted the way the helper expects (it re-sorts; keep the manifest canonical)
    body = ('\n'.join(out_lines) + '\n').encode()
    new_mend = hashlib.sha256(body).hexdigest()
    manifest = OUT / 'blur-native248.manifest'
    manifest.write_bytes(body + b'END ' + new_mend.encode() + b'\n')

    seed = OUT / 'seed'
    sobj = seed / 'seed245/blur-native248/objects'
    sobj.mkdir(parents=True)
    for src in objects_old.iterdir():
        link(src, sobj / src.name)
    run('BLUR seed', [HELPER, 'seed', '--manifest', manifest, '--pkg-lib', union, '--out', seed,
                      '--expect-manifest-end', new_mend, '--expect-profile', sha(profile)])
    nrows, new_end = sealed(seed / 'seed245/blur-native248/seed.idx', 'N245-SEED 1')
    # drop object links the new index does not reference (old registry files and profile)
    used = {l.split('\t')[2] for l in nrows if l.startswith(('F\t', 'P\t'))}
    dropped = []
    for o in sorted(sobj.iterdir()):
        if o.name not in used:
            o.unlink(); dropped.append(o.name)

    pkg = OUT / 'pkg-inputs'
    link(union, pkg / 'win/wine/lib/wine')
    run('shipped seed rejects the BLUR runtime', [HELPER, 'check', '--seed', SHIPPED_SEED, '--pkg-stage', pkg, '--end', SHIPPED_END], 1)
    run('BLUR seed accepts the BLUR runtime', [HELPER, 'check', '--seed', seed, '--pkg-stage', pkg, '--end', new_end])

    # ---- row-level comparison with the shipped index
    def rowmap(rs):
        return {('D', r.split('\t')[1]) if r.startswith('D\t') else ('F', r.split('\t')[3]): r for r in rs if r[:2] in ('D\t', 'F\t')}
    a, b = rowmap(rows), rowmap(nrows)
    ren = lambda k: (k[0], k[1].replace('drive_c/users/' + OLD, 'drive_c/users/BLUR').replace('system32/wow' + OLD + '.dll', 'system32/wowblur.dll'))
    changed = sorted(k[1] for k in a if ren(k) not in b or a[k].replace(k[1], ren(k)[1]) != b[ren(k)])
    assert len(a) == len(b)
    advapi = [r for r in nrows if r.startswith('F\t') and r.split('\t')[3] == 'drive_c/windows/syswow64/advapi32.dll'][0].split('\t')
    assert advapi[2].startswith(ADVAPI32), advapi
    users = [r for r in nrows if r.startswith('D\t') and r.split('\t')[1].startswith('drive_c/users/BLUR')]
    result = dict(
        schema=1, kind='blur100-seed', status='PASS_BUILD_NOT_PS4_TESTED',
        helper=dict(path=str(HELPER), sha256=HELPER_SHA),
        control=dict(seed_end=cend, index_sha256=sha(ctl / "seed245/blur-native248/seed.idx"), rows_equal_shipped=True, note="manifest_end differs only because the host manifest source paths moved"),
        shipped=dict(seed_end=SHIPPED_END, index_sha256=SHIPPED_INDEX, manifest_end=SHIPPED_MANIFEST_END),
        blur100=dict(seed_end=new_end, index_sha256=sha(seed / 'seed245/blur-native248/seed.idx'), manifest_end=new_mend,
                     host_manifest=str(manifest), runtime_union=str(union), pkg_inputs=str(pkg)),
        build_env=dict(N245_SEED_DIR=str(seed), N245_SEED_END=new_end),
        runtime_swapped=swapped, stale_in_shipped_seed=stale, runtime_rows_refreshed=refreshed, registry=reg_report, profile=dict(old_sha256=prof_old[2], new_sha256=sha(profile)),
        users_BLUR_folders=len(users), syswow64_advapi32=advapi[2], rows_changed=changed, rows_total=len(b),
        objects_dropped=dropped, commands=commands, PS4_tested=False)
    (OUT / 'plan.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: v for k, v in result.items() if k not in ('commands', 'rows_changed')}, indent=2))
    print('rows changed:', len(changed))


if __name__ == '__main__':
    main()

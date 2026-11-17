#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""BLUR 1.00 winevulkan.prx (plan Task E4): one dev capture path, on the shipped parwork260 module.

Shipped winevulkan 30e2764a (0.8.32/0.8.33 and order 284) was built by
native288-recipes/parwork260build.py at commit 0d9737e (its manifest at that commit records
30e2764a; HEAD's script builds the later 4074aba7). This script exports that commit's
native288-recipes folder and the two native280 inputs it reads, runs it unchanged into its own
folder (control), then runs a copy whose only edit is the async capture file
/data/Pc/Blur/async-capture.bin -> /data/blurdev/async-capture.bin (bench dev root, D14).
Controls (asserted): the exported build reproduces the shipped winevulkan.prx byte for byte, with
the recipe's own control-ELF and control-PRX checks.
Output: blur100-winevulkan/ (refuses to reuse it). Usage (WSL): python3 build_blur100_winevulkan.py
"""
import hashlib, json, subprocess, sys
import os  # JKFLOW_BUILD_ROOT
from pathlib import Path

HERE = Path(__file__).resolve().parent
R = HERE.parents[2]
B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-winevulkan'
COMMIT = '0d9737e'
SHIPPED = '30e2764a4220d1c45ad1cdaa7e8bdd01e7d4e61d8995e2f643e5df3438bf5b44'
EXPORT = ['research/native-wine/native280-decoder-config-candidate.c', 'research/native-wine/native280-decoder-config-build.json']
OLD_PATH = '/data/Pc/Blur/async-capture.bin'
NEW_PATH = '/data/blurdev/async-capture.bin'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
RUNNER = '''
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
sys.dont_write_bytecode = True
import parwork260build as pw
out = Path(sys.argv[2])
pw.OUT = out
pw.WORK = out / 'inputs'
pw.main()
'''


def export(dest):
    files = subprocess.check_output(['git', '-C', str(R), 'ls-tree', '-r', '--name-only', COMMIT,
                                     'research/native-wine/native288-recipes'], text=True).split()
    for f in files:
        p = dest / f.split('research/native-wine/', 1)[1]
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(subprocess.check_output(['git', '-C', str(R), 'show', '%s:%s' % (COMMIT, f)]))
    for f in EXPORT:   # untracked recipe inputs: the working-tree copies (the control proves them)
        p = dest / f.split('research/native-wine/', 1)[1]
        p.write_bytes((R / f).read_bytes())
    return dest / 'native288-recipes'


def build(src, out, tag):
    (O / 'runner.py').write_text(RUNNER)
    p = subprocess.run([sys.executable, str(O / 'runner.py'), str(src), str(out)], text=True, capture_output=True)
    (O / (tag + '.log')).write_text(p.stdout + p.stderr)
    assert not p.returncode, (tag, p.stdout[-2000:], p.stderr[-3000:])
    m = json.loads((src / 'parwork260-manifest.json').read_text())
    return m, Path(m['prx'])


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    O.mkdir()
    ctl_src = export(O / 'export-control')
    m = json.loads((ctl_src / 'parwork260-manifest.json').read_text())
    assert m['prx_sha256'] == SHIPPED, ('commit manifest', m['prx_sha256'])
    cm, cprx = build(ctl_src, O / 'control', 'control')
    assert cm['control_elf_exact'] and cm['control_prx_exact'], cm
    assert sha(cprx) == SHIPPED, ('control prx', sha(cprx))
    print('PASS control: commit %s recipe reproduces the shipped winevulkan.prx %s' % (COMMIT, SHIPPED), flush=True)

    blur_src = export(O / 'export-blur100')
    p = blur_src / 'parwork260build.py'
    t = p.read_text()
    n = t.count(OLD_PATH)
    assert n >= 1, n
    p.write_text(t.replace(OLD_PATH, NEW_PATH))
    diff = subprocess.run(['diff', '-u', '--label', 'a/native288-recipes/parwork260build.py@' + COMMIT, '--label',
                           'b/native288-recipes/parwork260build.py', str(ctl_src / 'parwork260build.py'), str(p)],
                          text=True, capture_output=True).stdout
    (O / 'blur100-winevulkan.patch').write_text(diff)
    bm, bprx = build(blur_src, O / 'blur100', 'blur100')
    assert bm['control_elf_exact'] and bm['control_prx_exact'], bm
    strings = subprocess.check_output(['strings', '-n4', str(bprx)], text=True, errors='replace').splitlines()
    hits = [s for s in strings if ('pros' + 'pero') in s.lower() or '/data/pc/blur' in s.lower()]
    assert not hits, hits
    nm = lambda e: sorted((l.split()[-1], l.split()[-2]) for l in subprocess.check_output(['llvm-nm-18', '-D', str(e)], text=True).splitlines())
    res = dict(task='E4 winevulkan.prx dev capture path', status='PASS_BUILD_NOT_PS4_TESTED', recipe_commit=COMMIT,
               shipped=SHIPPED, control=dict(prx_sha256=sha(cprx), equals_shipped=True, recipe_controls=dict(elf=cm['control_elf_exact'], prx=cm['control_prx_exact'])),
               blur100=dict(prx_sha256=sha(bprx), bytes=bprx.stat().st_size, elf_sha256=bm['elf_sha256']),
               path_occurrences_changed=n,
               dynamic_symbol_names_and_types_identical=nm(cm['elf']) == nm(bm['elf']),
               old_name_strings=0, patch_sha256=sha(O / 'blur100-winevulkan.patch'), PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(res, indent=2) + '\n')
    print(json.dumps(res, indent=2))


if __name__ == '__main__':
    main()

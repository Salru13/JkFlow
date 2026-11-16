#!/usr/bin/env python3
# Copyright (c) 2026 XxXJoker13XxX. All rights reserved.
"""BLUR 1.00 ntdll.prx (plan Task E4): names and paths only, on the shipped override260 ntdll.

Recipe = native288-recipes/override260build.py (the shipped ntdll 3f7e34bb: spin260 + clock fix +
override loader), run through spin260build.main with this script's own output folder, and two
loader-side objects rebuilt:
  pw_wine_dl.o     the override folder /data/prospero-win/override -> /data/blurdev/override
                   (override260's PW_WINE_DL_OVERRIDE_DIR define; bench dev root, design 7)
  pw_wine_compat.o fallback user name BLUR, fallback HOME /download0/BLUR, uname version BLUR
                   (design 7: "uname, ws2_32 and fallback user names become BLUR")
Controls (asserted): the unpatched pw_wine_compat.c compiles to the retained native248 object;
override260's control loader compile; spin260's control relink; and the whole chain rebuilt
unchanged reproduces the shipped ntdll.prx 3f7e34bb byte for byte.
Output: blur100-ntdll/ (refuses to reuse it). Usage (WSL): python3 build_blur100_ntdll.py
"""
import hashlib, json, subprocess, sys
import os  # JKFLOW_BUILD_ROOT
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'native288-recipes'))
sys.dont_write_bytecode = True
import spin260build as spin  # noqa: E402
import override260build as ov  # noqa: E402

B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-ntdll'
SHIPPED = '3f7e34bb30df3f01e8e9eb13ed174439219074e0e6dc3cf62a0e2e94828a1f46'
ROOT = B / 'native248-build-bridge'
GL = B / 'orbis-win-9749fcd'
COMPAT_SRC = ROOT / 'wine/ps5/pw_wine_compat.c'
COMPAT_OBJ = B / 'native248-build-wine/prx/obj/pw_wine_compat.o'
COMPAT_FLAGS = ['-DPW_WINE_PRX_BUILD', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
                '-I' + str(GL / 'src/freebsd-compat'), '-c']          # prx-ps4.sh step 1
OLD = 'pros' + 'pero'
DEV_OVERRIDE = '/data/blurdev/override'
COMPAT_EDITS = [
    ('user && *user?user:"' + OLD + '"', 'user && *user?user:"BLUR"'),
    ('home && *home==\'/\'?home:"/download0/' + OLD + '-win"', 'home && *home==\'/\'?home:"/download0/BLUR"'),
    ('{"FreeBSD","ps5","12.02","' + OLD + '-win","amd64"}', '{"FreeBSD","ps5","12.02","BLUR","amd64"}'),
]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
swap = {}
_orig_run = spin.run


def run_swapped(command, cwd=spin.WORK):
    """spin260's commands with (1) the retained compat object replaced (BLUR pass only) and
    (2) compiles of spin's own OUT/src copies given -ffile-prefix-map OUT=<shipped OUT>, so the
    source path the ELF records (assert __FILE__ strings) is the shipped build's."""
    if str(spin.OUT / 'candidate.elf') in map(str, command):   # the candidate link only, not spin's control relink
        command = [swap.get(str(a), a) for a in command]
    if any(str(a).startswith(str(spin.OUT / 'src') + '/') and str(a).endswith('.c') for a in command):
        command = command[:1] + ['-ffile-prefix-map=%s=%s' % (spin.OUT, SHIPPED_OUT)] + command[1:]
    return _orig_run(command, cwd)


spin.run = run_swapped


def cc(args, cwd=None):
    p = subprocess.run([str(a) for a in args], cwd=cwd, env=spin.ENV, capture_output=True, text=True)
    assert not p.returncode, (args[-3:], p.stdout[-1500:], p.stderr[-2500:])


def chain(tag, dl_o, out=None):
    spin.OUT = out or O / tag
    spin.HERE = O               # spin writes its manifest next to the outputs, never into the repo
    spin.MANIFEST = tag + '-spin-manifest.json'
    spin.DL_O = dl_o
    spin.main()
    return spin.OUT / 'ntdll.prx'


# spin260 writes server.c/sync.c into OUT/src and the ELF records those source paths; the shipped
# ntdll was built with OUT = native288-override260/ntdll (mapped back, see run_swapped).
SHIPPED_OUT = B / 'native288-override260/ntdll'


def exec_cmp(a, b):
    def secs(o):
        out = subprocess.check_output(['llvm-objdump-18', '-h', str(o)], text=True)
        return [l.split()[1] for l in out.splitlines() if len(l.split()) >= 5 and l.split()[0].isdigit() and l.split()[-1] == 'TEXT']
    def data(o, s):
        t = O / 'section.tmp'
        subprocess.check_call(['llvm-objcopy-18', '-O', 'binary', '--only-section=' + s, str(o), str(t)])
        d = t.read_bytes(); t.unlink(); return d
    sa, sb = secs(a), secs(b)
    return dict(executable_sections=len(sa), differing=[s for s in sorted(set(sa) | set(sb))
                                                         if s not in sa or s not in sb or data(a, s) != data(b, s)])


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    O.mkdir()
    (O / 'src/dl').mkdir(parents=True)
    inputs = [COMPAT_SRC, COMPAT_OBJ, ov.SRC, spin.DL_ADMITTED]
    pins = {str(p): sha(p) for p in inputs}

    # control: the shipped ntdll from the unchanged chain
    (O / 'src/dl/pw_wine_dl.c').write_text(ov.patched_source())
    ctl_dl = O / 'control-pw_wine_dl.o'
    cc([ov.CC, *ov.FLAGS, '-iquote', str(ov.SRC.parent), O / 'src/dl/pw_wine_dl.c', '-o', ctl_dl])
    assert sha(ctl_dl) == sha(B / 'native288-override260/pw_wine_dl.o'), 'override260 loader object differs'
    ctl_prx = chain('control', ctl_dl)
    assert sha(ctl_prx) == SHIPPED, ('control ntdll', sha(ctl_prx))
    print('PASS control chain reproduces the shipped ntdll.prx', SHIPPED, flush=True)
    ctl_compat = O / 'control-pw_wine_compat.o'
    cc([ov.CC, *COMPAT_FLAGS, COMPAT_SRC, '-o', ctl_compat])
    assert sha(ctl_compat) == sha(COMPAT_OBJ), 'control pw_wine_compat.o differs from the retained object'
    print('PASS control pw_wine_compat.o = retained native248 object', flush=True)

    # BLUR objects
    blur_dl = O / 'pw_wine_dl.o'
    cc([ov.CC, *ov.FLAGS, '-DPW_WINE_DL_OVERRIDE_DIR="' + DEV_OVERRIDE + '"', '-iquote', str(ov.SRC.parent),
        O / 'src/dl/pw_wine_dl.c', '-o', blur_dl])
    t = COMPAT_SRC.read_text()
    for a, b in COMPAT_EDITS:
        assert t.count(a) == 1, a
        t = t.replace(a, b)
    (O / 'src/pw_wine_compat.c').write_text(t)
    blur_compat = O / 'pw_wine_compat.o'
    cc([ov.CC, *COMPAT_FLAGS, '-iquote', str(COMPAT_SRC.parent), O / 'src/pw_wine_compat.c', '-o', blur_compat])
    swap[str(COMPAT_OBJ)] = str(blur_compat)
    blur_prx = chain('blur100', blur_dl)
    swap.clear()

    diff = subprocess.run(['diff', '-u', '--label', 'a/shared-prospero-108194e/wine/ps5/pw_wine_compat.c', '--label',
                           'b/shared-prospero-108194e/wine/ps5/pw_wine_compat.c', str(COMPAT_SRC), str(O / 'src/pw_wine_compat.c')],
                          text=True, capture_output=True).stdout
    diff += ('# pw_wine_dl.o: override260 source (native288-recipes/override260build.py) compiled with\n'
             '#   -DPW_WINE_DL_OVERRIDE_DIR="%s"\n' % DEV_OVERRIDE)
    (O / 'blur100-ntdll.patch').write_text(diff)
    strings = subprocess.check_output(['strings', '-n4', str(blur_prx)], text=True, errors='replace').splitlines()
    hits = [s for s in strings if OLD in s.lower() and '__wine_' + OLD + '_native_wow64_caps' not in s]
    assert not hits, hits
    nm = lambda p: sorted((l.split()[-1], l.split()[-2]) for l in subprocess.check_output(['llvm-nm-18', '-D', str(p)], text=True).splitlines())
    for p, h in pins.items():
        assert sha(p) == h, ('input changed', p)
    m = dict(task='E4 ntdll.prx names and paths', status='PASS_BUILD_NOT_PS4_TESTED', shipped=SHIPPED,
             control=dict(prx_sha256=sha(ctl_prx), equals_shipped=True, compat_object_equals_retained=True),
             blur100=dict(prx_sha256=sha(blur_prx), bytes=blur_prx.stat().st_size, elf_sha256=sha(O / 'blur100/candidate.elf')),
             object_exec_sections=dict(pw_wine_dl=exec_cmp(ctl_dl, blur_dl), pw_wine_compat=exec_cmp(ctl_compat, blur_compat)),
             pw_wine_dl_note='pw_wine_dl_open differs only where the override folder string is copied inline (its length changed: 27 -> 22 bytes)',
             dynamic_symbol_names_and_types_identical=nm(O / 'control/candidate.elf') == nm(O / 'blur100/candidate.elf'),
             old_name_strings_except_abi_symbol=0, patch_sha256=sha(O / 'blur100-ntdll.patch'), input_pins=pins, PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
    print(json.dumps({k: v for k, v in m.items() if k != 'input_pins'}, indent=2))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# Copyright (c) 2026 XxXJoker13XxX. All rights reserved.
"""BLUR 1.00 win32u.prx (plan Task E4): one dev switch path, on the shipped native274 win32u.

Shipped win32u cfc6f06c = native274-win32u-build.py's candidate link (native274-win32u-build.json)
of the native248 win32u objects, native274's vulkan.o and the native250 profiler objects. The
profiler loader (research/native-wine/native250-performance-loader.c) reads the switch file
/data/prospero-win/native250-performance.enable; BLUR moves it to the bench dev root
/data/blurdev/native250-performance.enable (design 7). Nothing else changes.
Controls (asserted): the loader recompiles to the retained native250 object; the recorded link
reproduces native274's ELF 847eddf2; the conversion gives the shipped win32u.prx cfc6f06c.
Output: blur100-win32u/ (refuses to reuse it). Usage (WSL): python3 build_blur100_win32u.py
"""
import hashlib, json, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NW = HERE.parent
B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-win32u'
ORB = B / 'orbis-win-9749fcd'
SDK = ORB / 'sdk/orbis-sdk-v1/sdk'
CC = ORB / 'build/ps4-wine-sdk/bin/prospero-clang'
OLD_PRX = B / 'native248-build-wine/prx'
W = B / 'native248-build-wine'
N274 = B / 'native274-win32u-link-r2'
LOADER_SRC = NW / 'native250-performance-loader.c'
LOADER_OBJ = B / 'native250-performance/prx/obj/native250-performance-loader.o'
SHIPPED = 'cfc6f06c9a6883b2e7357555a7766bc9931104307a030de8e1517105a90ba150'
N274_ELF = '847eddf2e7a459ef9c6d4ed6d6f31b75fdb4f790045e2641269d0d83b8f28d6f'
FLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
         '-I' + str(ORB / 'sdk/orbis-sdk-v1/mesa/include'), '-I' + str(NW / 'shared-prospero-108194e/wine/ps5')]   # native250-performance-build.py
OLD = 'pros' + 'pero'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
env = dict(os.environ, PW_SHARED_LIBC='1', PW_PRX_LINK='1', PW_PS4_ROOT=str(ORB), MESA_BUILD=str(B / 'native248-platform'),
           OO_PS4_TOOLCHAIN=str(SDK))    # native274-win32u-build.py env
env['PATH'] = ':'.join(str(p) for p in (W / 'tools', B / 'private-bin', B / 'deps/rootfs/usr/bin')) + ':' + env['PATH']
env['LLVM_OBJCOPY'] = str(ORB / 'build/ps4-wine-sdk/bin/llvm-objcopy')
stubs = OLD_PRX / 'stubs'
env['PW_PRX_STUBS'] = ' '.join(['-L' + str(stubs), '-Wl,--as-needed'] + [str(p) for p in sorted(stubs.glob('*.so'))])


def run(argv, cwd=None):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, env=env, text=True, capture_output=True)
    assert not p.returncode, (str(argv[0])[-30:], p.stdout[-1500:], p.stderr[-2500:])


def recorded(name):
    d = json.loads((NW / 'native274-win32u-build.json').read_text())
    return list(next(c['argv'] for c in d['commands'] if c['name'] == name))


def link_convert(loader_o, work):
    link = recorded('candidate-link')
    i = link.index('-o')
    assert link[i + 1] == str(N274 / 'candidate.elf')
    link[i + 1] = str(work / 'candidate.elf')
    j = link.index(str(LOADER_OBJ))
    link[j] = str(loader_o)
    run(link, cwd=W / 'build')
    conv = recorded('convert')
    for flag, name in (('-in', 'candidate.elf'), ('-out', 'candidate.oelf'), ('-lib', 'win32u.prx')):
        conv[conv.index(flag) + 1] = str(work / name)
    run(conv, cwd=W / 'build')
    return work / 'candidate.elf', work / 'win32u.prx'


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    O.mkdir()
    pins = {str(p): sha(p) for p in (LOADER_SRC, LOADER_OBJ, N274 / 'candidate.elf', N274 / 'win32u.prx', NW / 'native274-win32u-build.json')}
    assert pins[str(N274 / 'win32u.prx')] == SHIPPED and pins[str(N274 / 'candidate.elf')] == N274_ELF

    ctl = O / 'control'
    ctl.mkdir()
    run([CC, *FLAGS, '-c', LOADER_SRC, '-o', ctl / 'native250-performance-loader.o'])
    assert sha(ctl / 'native250-performance-loader.o') == sha(LOADER_OBJ), 'control loader object differs'
    elf, prx = link_convert(LOADER_OBJ, ctl)
    assert sha(elf) == N274_ELF, ('control link', sha(elf))
    assert sha(prx) == SHIPPED, ('control convert', sha(prx))
    print('PASS controls: loader object, native274 ELF and the shipped win32u.prx', SHIPPED, flush=True)

    src = O / 'src'
    src.mkdir()
    t = LOADER_SRC.read_text()
    a = '"/data/' + OLD + '-win/native250-performance.enable"'
    assert t.count(a) == 1
    (src / LOADER_SRC.name).write_text(t.replace(a, '"/data/blurdev/native250-performance.enable"'))
    blur = O / 'blur100'
    blur.mkdir()
    run([CC, '-ffile-prefix-map=%s=%s' % (src, LOADER_SRC.parent), *FLAGS, '-c', src / LOADER_SRC.name,
         '-o', blur / 'native250-performance-loader.o'])
    belf, bprx = link_convert(blur / 'native250-performance-loader.o', blur)

    def execs(o):
        out = subprocess.check_output(['llvm-objdump-18', '-h', str(o)], text=True)
        r = {}
        for l in out.splitlines():
            f = l.split()
            if len(f) >= 5 and f[0].isdigit() and f[-1] == 'TEXT':
                tmp = O / 'sec.tmp'
                subprocess.check_call(['llvm-objcopy-18', '-O', 'binary', '--only-section=' + f[1], str(o), str(tmp)])
                r[f[1]] = tmp.read_bytes(); tmp.unlink()
        return r
    a_s, b_s = execs(LOADER_OBJ), execs(blur / 'native250-performance-loader.o')
    diff_secs = sorted(s for s in set(a_s) | set(b_s) if a_s.get(s) != b_s.get(s))
    diff = subprocess.run(['diff', '-u', '--label', 'a/research/native-wine/' + LOADER_SRC.name, '--label',
                           'b/research/native-wine/' + LOADER_SRC.name, str(LOADER_SRC), str(src / LOADER_SRC.name)],
                          text=True, capture_output=True).stdout
    (O / 'blur100-win32u.patch').write_text(diff)
    strings = subprocess.check_output(['strings', '-n4', str(bprx)], text=True, errors='replace').splitlines()
    hits = [s for s in strings if OLD in s.lower() or '/data/pc/blur' in s.lower()]
    assert not hits, hits
    nm = lambda p: sorted((l.split()[-1], l.split()[-2]) for l in subprocess.check_output(['llvm-nm-18', '-D', str(p)], text=True).splitlines())
    for p, h in pins.items():
        assert sha(p) == h, ('input changed', p)
    m = dict(task='E4 win32u.prx dev switch path', status='PASS_BUILD_NOT_PS4_TESTED', shipped=SHIPPED,
             control=dict(loader_object_equals_retained=True, elf_equals_native274=True, prx_equals_shipped=True),
             blur100=dict(prx_sha256=sha(bprx), bytes=bprx.stat().st_size, elf_sha256=sha(belf)),
             loader_executable_sections_differing=diff_secs,
             dynamic_symbol_names_and_types_identical=nm(elf) == nm(belf), old_name_strings=0,
             patch_sha256=sha(O / 'blur100-win32u.patch'), input_pins=pins, PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
    print(json.dumps({k: v for k, v in m.items() if k != 'input_pins'}, indent=2))


if __name__ == '__main__':
    main()

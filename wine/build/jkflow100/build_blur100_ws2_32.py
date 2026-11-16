#!/usr/bin/env python3
# Copyright (c) 2026 XxXJoker13XxX. All rights reserved.
"""BLUR 1.00 ws2_32.prx (plan Task E4): names and paths only, on the shipped native250 resolver build.

Shipped ws2_32 5e18f5eb = research/native-wine/native250-resolver-build.sh (resolver.o from
shared-prospero pw_ws2_32_libc.c + native248 objects). Three objects change:
  resolver.o          sceNet pool and resolver names "prospero-win" -> "BLUR" (design 7, ws2_32 names)
  pw_emutls_ws2_32.o  the emutls note prefix wowblur (same source change as blur100-wow)
  pw_wow_log_ws2_32.o the wow log in $BLUR_LOG_DIR (else /data/blurdev/logs; same source as blur100-wow)
Controls (asserted): each unpatched source compiles to the retained object; the recorded link gives
the native250 ELF 6d0ad634 and the conversion the shipped ws2_32.prx 5e18f5eb.
Output: blur100-ws2_32/ (refuses to reuse it). Usage (WSL): python3 build_blur100_ws2_32.py
"""
import hashlib, json, os, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
NW = HERE.parent
B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-ws2_32'
ORB = B / 'orbis-win-9749fcd'
SDK = ORB / 'sdk/orbis-sdk-v1/sdk'
CC = ORB / 'build/ps4-wine-sdk/bin/prospero-clang'
OLDP = B / 'native248-build-wine/prx'
RES = B / 'native250-resolver'
WOW = B / 'blur100-wow/src'
LIBC_SRC = NW / 'shared-prospero-108194e/wine/ps5/pw_ws2_32_libc.c'
EMUTLS = ORB / 'src/native/pw_emutls_ps4.c'
WOWLOG = ORB / 'src/native/pw_wow_log_ps4.c'
SHIPPED = '5e18f5eb371476fce6030ba1b45fdbdd0ffca17481dfc85fdde57a5332a89b7d'
SHIPPED_ELF = '6d0ad634dd71a857ca084ad658288cac01adf4b1483ee28d412d2be81945d5cd'
OLD = 'pros' + 'pero'
RES_FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-D_REENTRANT', '-DPW_PS4_DNS', '-femulated-tls',
             '-I' + str(SDK / 'include')]                               # native250-resolver-build.sh
HELPER_FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Werror', '-fPIC', '-D_REENTRANT']   # prx-ps4.sh lines 167-174
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
env = dict(os.environ, OO_PS4_TOOLCHAIN=str(SDK), PW_SHARED_LIBC='1', PW_PRX_LINK='1', MESA_BUILD=str(B / 'native248-platform'))
env['PATH'] = ':'.join(str(p) for p in (B / 'native248-build-wine/tools', B / 'private-bin', B / 'deps/rootfs/usr/bin')) + ':' + env['PATH']
# native250-resolver-build.sh: $(find stubs -maxdepth 1 -name '*.so' -printf '%p ' | sort) -- one line, so the
# order is find's directory order, not sorted.
_found = subprocess.check_output(['find', str(OLDP / 'stubs'), '-maxdepth', '1', '-name', '*.so', '-printf', '%p '], text=True)
env['PW_PRX_STUBS'] = '-L%s -Wl,--as-needed %s' % (OLDP / 'stubs', _found)


def run(argv, cwd=None):
    p = subprocess.run([str(x) for x in argv], cwd=cwd, env=env, text=True, capture_output=True)
    assert not p.returncode, (str(argv[0])[-30:], p.stdout[-1500:], p.stderr[-2500:])


def build(work, resolver_o, emutls_o, wowlog_o):
    run([CC, '-shared', '-Wl,-Bsymbolic', '-Wl,--eh-frame-hdr', '-Wl,-soname,ws2_32.prx', '-Wl,--warn-unresolved-symbols',
         '-o', work / 'ws2_32.shared.elf', OLDP / 'obj/ws2_32_unixlib_dns.o', resolver_o, emutls_o, wowlog_o,
         OLDP / 'heap/pw_wine_heap_libc.o', OLDP / 'obj/pw_wine_dl_libc.o', OLDP / 'obj/ws2_32_desc.o', '-lSceNetCtl'])
    (work / 'sce_module').mkdir()
    run([ORB / 'scripts/create-fself-repro.sh', SDK / 'bin/linux/create-fself', '-in', work / 'ws2_32.shared.elf',
         '-out', work / 'ws2_32.oelf', '-lib', work / 'sce_module/ws2_32.prx', '-libname', 'ws2_32.prx', '-ptype', 'fake',
         '-library-path', '%s:%s' % (OLDP / 'stubs', SDK / 'lib')])
    return work / 'ws2_32.shared.elf', work / 'sce_module/ws2_32.prx'


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


def exec_diff(a, b):
    x, y = execs(a), execs(b)
    return sorted(s for s in set(x) | set(y) if x.get(s) != y.get(s))


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    wow_m = json.loads((HERE / 'blur100-wow-manifest.json').read_text())
    assert wow_m['status'].startswith('PASS')
    O.mkdir()
    pins = {str(p): sha(p) for p in (LIBC_SRC, EMUTLS, WOWLOG, WOW / 'pw_emutls_ps4.c', WOW / 'pw_wow_log_ps4.c',
                                     RES / 'resolver.o', OLDP / 'obj/pw_emutls_ws2_32.o', OLDP / 'obj/pw_wow_log_ws2_32.o',
                                     RES / 'ws2_32.shared.elf', RES / 'sce_module/ws2_32.prx')}
    assert pins[str(RES / 'sce_module/ws2_32.prx')] == SHIPPED and pins[str(RES / 'ws2_32.shared.elf')] == SHIPPED_ELF

    ctl = O / 'control'
    ctl.mkdir()
    run([CC, *RES_FLAGS, '-c', LIBC_SRC, '-o', ctl / 'resolver.o'])
    run([CC, *HELPER_FLAGS, '-c', EMUTLS, '-o', ctl / 'pw_emutls_ws2_32.o'])
    run([CC, *HELPER_FLAGS, '-c', WOWLOG, '-o', ctl / 'pw_wow_log_ws2_32.o'])
    for name, ref in (('resolver.o', RES / 'resolver.o'), ('pw_emutls_ws2_32.o', OLDP / 'obj/pw_emutls_ws2_32.o'),
                      ('pw_wow_log_ws2_32.o', OLDP / 'obj/pw_wow_log_ws2_32.o')):
        assert sha(ctl / name) == sha(ref), ('control object differs', name)
    elf, prx = build(ctl, RES / 'resolver.o', OLDP / 'obj/pw_emutls_ws2_32.o', OLDP / 'obj/pw_wow_log_ws2_32.o')
    assert sha(elf) == SHIPPED_ELF, ('control ELF', sha(elf))
    assert sha(prx) == SHIPPED, ('control prx', sha(prx))
    print('PASS controls: three objects, native250 ELF and the shipped ws2_32.prx', SHIPPED, flush=True)

    src = O / 'src'
    src.mkdir()
    t = LIBC_SRC.read_text()
    a = '("' + OLD + '-win"'
    assert t.count(a) == 2 and t.count('sceNetResolverCreate("' + OLD + '-win"') == 1
    t = t.replace('sceNetPoolCreate("' + OLD + '-win"', 'sceNetPoolCreate("BLUR"').replace(
        'sceNetResolverCreate("' + OLD + '-win"', 'sceNetResolverCreate("BLUR"')
    assert '"' + OLD + '-win"' not in t
    (src / LIBC_SRC.name).write_text(t)
    blur = O / 'blur100'
    blur.mkdir()
    run([CC, '-ffile-prefix-map=%s=%s' % (src, LIBC_SRC.parent), '-iquote', str(LIBC_SRC.parent), *RES_FLAGS, '-c', src / LIBC_SRC.name, '-o', blur / 'resolver.o'])
    run([CC, '-ffile-prefix-map=%s=%s' % (WOW, EMUTLS.parent), *HELPER_FLAGS, '-c', WOW / 'pw_emutls_ps4.c', '-o', blur / 'pw_emutls_ws2_32.o'])
    run([CC, '-ffile-prefix-map=%s=%s' % (WOW, WOWLOG.parent), *HELPER_FLAGS, '-c', WOW / 'pw_wow_log_ps4.c', '-o', blur / 'pw_wow_log_ws2_32.o'])
    belf, bprx = build(blur, blur / 'resolver.o', blur / 'pw_emutls_ws2_32.o', blur / 'pw_wow_log_ws2_32.o')

    diff = subprocess.run(['diff', '-u', '--label', 'a/shared-prospero-108194e/wine/ps5/pw_ws2_32_libc.c', '--label',
                           'b/shared-prospero-108194e/wine/ps5/pw_ws2_32_libc.c', str(LIBC_SRC), str(src / LIBC_SRC.name)],
                          text=True, capture_output=True).stdout
    diff += '# pw_emutls_ws2_32.o, pw_wow_log_ws2_32.o: the BLUR sources of build_blur100_wow.py (blur100-wow.patch)\n'
    (O / 'blur100-ws2_32.patch').write_text(diff)
    strings = subprocess.check_output(['strings', '-n4', str(bprx)], text=True, errors='replace').splitlines()
    hits = [s for s in strings if OLD in s.lower()]
    assert not hits, hits
    nm = lambda p: sorted((l.split()[-1], l.split()[-2]) for l in subprocess.check_output(['llvm-nm-18', '-D', str(p)], text=True).splitlines())
    for p, h in pins.items():
        assert sha(p) == h, ('input changed', p)
    m = dict(task='E4 ws2_32.prx names and paths', status='PASS_BUILD_NOT_PS4_TESTED', shipped=SHIPPED,
             control=dict(objects_equal_retained=True, elf_equals_native250=True, prx_equals_shipped=True),
             blur100=dict(prx_sha256=sha(bprx), bytes=bprx.stat().st_size, elf_sha256=sha(belf)),
             object_exec_sections_differing=dict(resolver=exec_diff(RES / 'resolver.o', blur / 'resolver.o'),
                                                 emutls=exec_diff(OLDP / 'obj/pw_emutls_ws2_32.o', blur / 'pw_emutls_ws2_32.o'),
                                                 wow_log=exec_diff(OLDP / 'obj/pw_wow_log_ws2_32.o', blur / 'pw_wow_log_ws2_32.o')),
             dynamic_symbol_names_and_types_identical=nm(elf) == nm(belf),
             old_name_strings=0, patch_sha256=sha(O / 'blur100-ws2_32.patch'), input_pins=pins, PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
    print(json.dumps({k: v for k, v in m.items() if k != 'input_pins'}, indent=2))


if __name__ == '__main__':
    main()

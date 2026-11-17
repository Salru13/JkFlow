#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""BLUR 1.00 wineserver.prx (plan Task E4): names and paths only, on the shipped clockfix260 server.

Shipped wineserver 46edbb33 = native266-wineserver-r1 candidate link (receipt.json) with
serverprio260's fd.o, then clockfix260's 3-byte monotonicNow patch and the native266 conversion.
This script repeats that chain into its own folder and changes two objects:
  server/thread.o  the round-robin switch file /data/prospero-win/pw_sched -> /data/blurdev/pw_sched
                   (Wine source native247 server/thread.c, make.log compile command, the copy's path
                   mapped back with -ffile-prefix-map so the recorded source path is unchanged)
  pw_wine_compat.o the BLUR compat object (fallback user, HOME and uname, see build_blur100_ntdll.py)
Controls (asserted): thread.c recompiles to the retained server/thread.o; the unchanged link equals
serverprio260's ELF; the unchanged chain gives the shipped wineserver.prx 46edbb33.
Output: blur100-wineserver/ (refuses to reuse it). Usage (WSL): python3 build_blur100_wineserver.py
"""
import hashlib, json, os, shlex, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'native288-recipes'))
sys.dont_write_bytecode = True
import clockfix260 as clock  # noqa: E402

B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-wineserver'
REF = B / 'native266-wineserver-r1'
PRIO = B / 'native288-serverprio260'
CWD = B / 'native248-build-wine/build'
WSRC = B / 'native247-wine-source'
THREAD_C = WSRC / 'server/thread.c'
THREAD_O = CWD / 'server/thread.o'
COMPAT_OBJ = B / 'native248-build-wine/prx/obj/pw_wine_compat.o'
BLUR_COMPAT = B / 'blur100-ntdll/pw_wine_compat.o'
BLUR_COMPAT_SHA = None   # read from build_blur100_ntdll's manifest
SHIPPED = '46edbb3349d9378a5db8d92330590bc7b38ce1c7841a5bdd8867c485e2c46a8c'
OLD = 'pros' + 'pero'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
ENV = dict(os.environ, PW_PS4_ROOT=str(B / 'orbis-win-9749fcd'), MESA_BUILD=str(B / 'native248-platform'),
           OO_PS4_TOOLCHAIN=str(B / 'orbis-win-9749fcd/sdk/orbis-sdk-v1/sdk'), PW_PRX_LINK='1', PW_SHARED_LIBC='1',
           PW_PRX_STUBS='-L' + str(REF / 'empty-stubs') + ' -Wl,--as-needed')   # native266-wineserver-build.py env
ENV['PATH'] = ':'.join(str(p) for p in (B / 'native248-build-wine/tools', B / 'private-bin', B / 'deps/rootfs/usr/bin')) + ':' + ENV['PATH']


def run(cmd, cwd=CWD):
    p = subprocess.run([str(a) for a in cmd], cwd=cwd, env=ENV, capture_output=True, text=True)
    assert not p.returncode, (str(cmd[0])[-30:], p.stdout[-1500:], p.stderr[-2500:])


def thread_cmd(src, out, prefix_map=None):
    t = (B / 'native248-build-wine/make.log').read_text(errors='replace').replace('\\\n', ' ')
    line = [l for l in t.splitlines() if ' -c -o server/thread.o ' in l][-1]
    cmd = shlex.split(line)
    cmd[cmd.index('-o') + 1] = str(out)
    cmd[cmd.index(str(THREAD_C))] = str(src)
    if prefix_map:
        cmd[1:1] = ['-ffile-prefix-map=%s=%s' % prefix_map]
    return cmd


def link(fd_o, thread_o, compat_o, out):
    cmd = json.loads((REF / 'receipt.json').read_text())
    cmd = {r['name']: r['command'] for r in cmd['commands']}['candidate-link']
    m = {str(REF / 'fd.o'): str(fd_o), 'server/thread.o': str(thread_o), str(COMPAT_OBJ): str(compat_o),
         str(REF / 'candidate.elf'): str(out)}
    new = [m.get(a, a) for a in cmd]
    for k in (str(fd_o), str(thread_o), str(compat_o), str(out)):
        assert k in new, k
    run(new)


def finish(elf, work):
    """clockfix260 patch + native266 conversion, exactly as clockfix260.main does for wineserver."""
    import re
    data = bytearray(Path(elf).read_bytes())
    start, size = clock.symbol_range(elf)
    off = clock.file_offset(bytes(data), start)
    window = bytes(data[off:off + size])
    hits = [m.start() for m in re.finditer(re.escape(clock.OLD) + rb'....\x48\x8b\x35', window, re.S)]
    assert len(hits) == 1, hits
    data[off + hits[0]:off + hits[0] + 3] = clock.NEW
    patched = work / 'candidate.elf'
    patched.write_bytes(data)
    ref = json.loads(clock.REFERENCE.read_text())['conversion_command']
    clock.convert(patched, work / 'candidate.oelf', work / 'wineserver.prx', 'wineserver', ref)
    clock.assert_bss_mapped(patched, work / 'candidate.oelf')
    return work / 'wineserver.prx'


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    ntdll_m = json.loads((HERE / 'blur100-ntdll-manifest.json').read_text())
    assert ntdll_m['status'].startswith('PASS'), ntdll_m['status']
    O.mkdir()
    pins = {str(p): sha(p) for p in (THREAD_C, THREAD_O, COMPAT_OBJ, BLUR_COMPAT, PRIO / 'fd.o', PRIO / 'candidate.elf', REF / 'receipt.json')}

    # controls
    ctl = O / 'control'
    ctl.mkdir()
    run(thread_cmd(THREAD_C, ctl / 'thread.o'))
    assert sha(ctl / 'thread.o') == sha(THREAD_O), 'control thread.o differs from the retained object'
    link(PRIO / 'fd.o', THREAD_O, COMPAT_OBJ, ctl / 'linked.elf')
    assert sha(ctl / 'linked.elf') == sha(PRIO / 'candidate.elf'), 'control link differs from serverprio260'
    assert sha(finish(ctl / 'linked.elf', ctl)) == SHIPPED, 'control chain differs from the shipped wineserver'
    print('PASS controls: thread.o, serverprio260 link and the shipped wineserver.prx', SHIPPED, flush=True)

    # BLUR
    src = O / 'src/server'
    src.mkdir(parents=True)
    t = THREAD_C.read_text(errors='surrogateescape')
    a = '"/data/' + OLD + '-win/pw_sched"'
    assert t.count(a) == 1
    t = t.replace(a, '"/data/blurdev/pw_sched"').replace(' * /data/' + OLD + '-win/pw_sched exists. */', ' * /data/blurdev/pw_sched exists. */')
    (src / 'thread.c').write_text(t, errors='surrogateescape')
    blur = O / 'blur100'
    blur.mkdir()
    run(thread_cmd(src / 'thread.c', blur / 'thread.o', (src, THREAD_C.parent)))
    link(PRIO / 'fd.o', blur / 'thread.o', BLUR_COMPAT, blur / 'linked.elf')
    prx = finish(blur / 'linked.elf', blur)

    diff = subprocess.run(['diff', '-u', '--label', 'a/native247-wine-source/server/thread.c', '--label',
                           'b/native247-wine-source/server/thread.c', str(THREAD_C), str(src / 'thread.c')],
                          text=True, capture_output=True).stdout
    diff += '# pw_wine_compat.o: the BLUR object from build_blur100_ntdll.py (blur100-ntdll.patch)\n'
    (O / 'blur100-wineserver.patch').write_text(diff)

    def exec_secs(o):
        out = subprocess.check_output(['llvm-objdump-18', '-h', str(o)], text=True)
        names = [l.split()[1] for l in out.splitlines() if len(l.split()) >= 5 and l.split()[0].isdigit() and l.split()[-1] == 'TEXT']
        r = {}
        for s in names:
            tmp = O / 'sec.tmp'
            subprocess.check_call(['llvm-objcopy-18', '-O', 'binary', '--only-section=' + s, str(o), str(tmp)])
            r[s] = tmp.read_bytes(); tmp.unlink()
        return r
    a_s, b_s = exec_secs(THREAD_O), exec_secs(blur / 'thread.o')
    thread_diff = sorted(s for s in set(a_s) | set(b_s) if a_s.get(s) != b_s.get(s))
    strings = subprocess.check_output(['strings', '-n4', str(prx)], text=True, errors='replace').splitlines()
    hits = [s for s in strings if OLD in s.lower() and '__wine_' + OLD + '_native_wow64_caps' not in s]
    assert not hits, hits
    nm = lambda p: sorted((l.split()[-1], l.split()[-2]) for l in subprocess.check_output(['llvm-nm-18', '-D', str(p)], text=True).splitlines())
    for p, h in pins.items():
        assert sha(p) == h, ('input changed', p)
    m = dict(task='E4 wineserver.prx names and paths', status='PASS_BUILD_NOT_PS4_TESTED', shipped=SHIPPED,
             control=dict(thread_o_equals_retained=True, link_equals_serverprio260=True, prx_equals_shipped=True),
             blur100=dict(prx_sha256=sha(prx), bytes=prx.stat().st_size, elf_sha256=sha(blur / 'candidate.elf')),
             thread_o_executable_sections_differing=thread_diff,
             compat_object=dict(path=str(BLUR_COMPAT), sha256=sha(BLUR_COMPAT), note='executable sections identical to the retained object (ntdll manifest)'),
             dynamic_symbol_names_and_types_identical=nm(ctl / 'candidate.elf') == nm(blur / 'candidate.elf'),
             old_name_strings=0, patch_sha256=sha(O / 'blur100-wineserver.patch'), input_pins=pins, PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(m, indent=2) + '\n')
    print(json.dumps({k: v for k, v in m.items() if k != 'input_pins'}, indent=2))


if __name__ == '__main__':
    main()

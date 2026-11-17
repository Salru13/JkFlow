#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""BLUR 1.00 WoW64 modules (plan Task E4, decision 20): wowprospero -> wowblur, names and paths only.

Builds four shipping modules from the retained native248 trees, read only:
  wow64.dll        dlls/wow64/syscall.c (patches 0611/0614): the CPU name wowprospero.dll -> wowblur.dll
  wowblur.dll      the PE CPU backend, linked from the unchanged cpu.c with the spec renamed
  wowblur.prx      its Unix side: log prefix and module name wowblur, dev switches in /data/blurdev,
                   wow log in $BLUR_LOG_DIR (else /data/blurdev/logs; the dev root is never created)
  wow64native.prx  the native CPU's Unix side: same wow log change (shared pw_wow_log_ps4.c,
                   pw_emutls_ps4.c note text)

Controls, asserted before anything else is accepted (each module is rebuilt unchanged first):
  C1 wow64.dll  unstripped = native248 build 171511a7, staged (llvm-strip-18 --strip-debug) = shipped 35c7351d
  C2 wowprospero.dll staged = shipped 81e1385e
  C3 wowprospero.prx = shipped 606f1d8d
  C4 wow64native.prx = shipped 29a89108
Engine-logic evidence written to the manifest: for the PE modules every section except the
ones holding the renamed strings is byte-identical; for the PRX modules the dynamic symbol
tables are compared and every changed source line is in the patch file.
Never runs make in a retained tree. Output: blur100-wow/ (refuses to reuse it).
Usage (WSL): python3 build_blur100_wow.py
"""
import hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
B = Path(os.environ['JKFLOW_BUILD_ROOT'])
O = B / 'blur100-wow'
WORK = B / 'native248-build-wine'
BW = WORK / 'build'
SRC = B / 'native247-wine-source'
ROOT = B / 'native248-build-bridge'          # PW_ROOT of native248 (links to the shared prospero source)
GL = B / 'orbis-win-9749fcd'
SDK = GL / 'build/ps4-wine-sdk'
OO = GL / 'sdk/orbis-sdk-v1/sdk'
CC = SDK / 'bin/prospero-clang'
PRX = WORK / 'prx'
STUBS = PRX / 'stubs'
WINEGCC = B / 'wine-build/tools/winegcc/winegcc'
WINEBUILD = B / 'wine-build/tools/winebuild/winebuild'
OLD = 'pros' + 'pero'
DEV = '/data/blurdev'

PINS = {
    'wow64.dll.build': '171511a74ef6b2944ebbc88e6d6ff7b770ddeb2b928b7440d6c10d05339b33f4',
    'wow64.dll': '35c7351d35c830b09d5ca85c7e97047ef7733756d72dd9116adb1849bd7916c9',
    'wowprospero.dll': '81e1385ef0b092f2d7550a14b31b1c67fe29f7dff6c70286e1f52ffc51b512dd',
    'wowprospero.prx': '',   # read from the retained tree in main(), prefix asserted
    'wow64native.prx': '',
}

sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
# GNU ld writes the link time into the COFF TimeDateStamp (and so the PE checksum). Pinned to the
# retained modules' stamps for the controls and the BLUR builds, so only the renamed bytes differ.
EPOCH_WOW64 = 1794052296      # native248 build wow64.dll
EPOCH_CPU = 1794052524        # native248-nativecpu wowprospero.dll
env = dict(os.environ)
env['PATH'] = ':'.join([str(WORK / 'tools'), str(B / 'private-bin'), str(B / 'deps/rootfs/usr/bin'), env['PATH']])  # tools: llvm-* links (native248-build-prx.sh)
env['LLVM_OBJCOPY'] = 'llvm-objcopy-18'
env['PW_SHARED_LIBC'] = '1'
env['OO_PS4_TOOLCHAIN'] = str(OO)
# prx-ps4.sh state at steps 5/6: PW_PRX_LINK=1 exported (line 218); the stubs list was taken before
# step 6 copied wow64native.so into it.
env['PW_PRX_LINK'] = '1'
env['MESA_BUILD'] = str(B / 'native248-platform')   # prospero-clang -isystem $MESA_BUILD/orbis-compat/include (FreeBSD signal.h)
env['PW_PRX_STUBS'] = ' '.join(['-L' + str(STUBS), '-Wl,--as-needed', *map(str, sorted(p for p in STUBS.glob('*.so') if p.name != 'wow64native.so'))])
commands = []


def run(cmd, name, cwd=None, epoch=None):
    e = dict(env, SOURCE_DATE_EPOCH=str(epoch)) if epoch else env
    p = subprocess.run(list(map(str, cmd)), cwd=cwd, env=e, text=True, capture_output=True)
    (O / 'logs').mkdir(exist_ok=True)
    (O / 'logs' / (name + '.log')).write_text(' '.join(map(str, cmd)) + '\n\n' + p.stdout + p.stderr)
    commands.append(dict(name=name, command=list(map(str, cmd)), cwd=str(cwd), exit=p.returncode))
    assert not p.returncode, (name, (p.stdout + p.stderr)[-3000:])
    return p.stdout + p.stderr


def edit(text, pairs, label):
    for a, b, n in pairs:
        c = text.count(a)
        assert c == n, (label, a, c, n)
        text = text.replace(a, b)
    return text


# ------------------------------------------------------------------ PE: wow64.dll
SYSCALL = SRC / 'dlls/wow64/syscall.c'
WOW64_OBJS = ['file', 'process', 'registry', 'security', 'sync', 'syscall', 'system', 'virtual']
SYSCALL_EDITS = [
    ('L"wowprospero.dll"', 'L"wowblur.dll"', 2),
    ('"wowprospero.dll" };', '"wowblur.dll" };', 1),
    ('not wow64native.dll or wowprospero.dll', 'not wow64native.dll or wowblur.dll', 1),
    ('using wowprospero.dll', 'using wowblur.dll', 1),
]


def syscall_cmd(src, out):
    """make-first-pass.log line 13911 (native248), output redirected."""
    log = (WORK / 'make-first-pass.log').read_text(errors='replace').replace('\\\n', ' ')
    line = next(l for l in log.splitlines() if l.startswith('x86_64-w64-mingw32-gcc -c -o dlls/wow64/x86_64-windows/syscall.o '))
    import shlex
    cmd = shlex.split(line)
    cmd[cmd.index('-o') + 1] = str(out)
    i = cmd.index(str(SYSCALL))
    cmd[i] = str(src)
    return cmd


def link_wow64(syscall_o, out):
    objs = [str(BW / 'dlls/wow64/x86_64-windows' / (n + '.o')) if n != 'syscall' else str(syscall_o) for n in WOW64_OBJS]
    return [WINEGCC, '-o', out, '--wine-objdir', '.', '--winebuild', WINEBUILD, '--cc-cmd=x86_64-w64-mingw32-gcc',
            '-b', 'x86_64-w64-mingw32', '-Wl,--wine-builtin', '-shared', SRC / 'dlls/wow64/wow64.spec', '-nodefaultlibs',
            *objs, 'dlls/ntdll/x86_64-windows/libntdll.a', 'libs/winecrt0/x86_64-windows/libwinecrt0.a',
            'libs/compiler-rt/x86_64-windows/libcompiler-rt.a', '-Wl,--build-id']


# ------------------------------------------------------------------ PE: CPU backend
CPU_SRC = ROOT / 'wine/wowprospero/cpu.c'
SPEC = ROOT / 'wine/wowprospero/wowprospero.spec'


def cpu_cmd(out):
    m = ROOT / 'wine/wowprospero'
    return ['x86_64-w64-mingw32-gcc', '-c', '-o', out, CPU_SRC, '-I' + str(m), '-I' + str(BW / 'include'),
            '-I' + str(SRC / 'include'), '-I' + str(SRC / 'include/msvcrt'), '-D_MSVCR_VER=0', '-D__WINESRC__',
            '-D__WINE_PE_BUILD', '-Wall', '-fno-strict-aliasing', '-mcx16', '-mcmodel=small', '-O2', '-g',
            '-fno-builtin', '-fshort-wchar']


def link_cpu(spec, cpu_o, out):
    return [WINEGCC, '-o', out, '--wine-objdir', '.', '--winebuild', WINEBUILD, '--cc-cmd=x86_64-w64-mingw32-gcc',
            '-b', 'x86_64-w64-mingw32', '-Wl,--wine-builtin', '-shared', spec, '-nodefaultlibs', cpu_o,
            'dlls/wow64/x86_64-windows/libwow64.a', 'dlls/ntdll/x86_64-windows/libntdll.a',
            'libs/winecrt0/x86_64-windows/libwinecrt0.a', 'dlls/ntdll/x86_64-windows/libntdll.a',
            'libs/compiler-rt/x86_64-windows/libcompiler-rt.a']


def stage(src, dst):
    shutil.copyfile(src, dst)
    run(['llvm-strip-18', '--strip-debug', dst], 'strip-' + Path(dst).parent.name + '-' + Path(dst).name)
    return dst


# ------------------------------------------------------------------ Unix side (prx-ps4.sh steps 5 and 6)
WOW_DBT = [ROOT / 'src' / n for n in ('pw_x86_engine.c', 'pw_x86_block.c', 'pw_x86_cache.c', 'pw_x86_hostexec.c',
                                      'pw_x86_reencode.c', 'pw_x87.c', 'pw_guest_fp.c', 'pw_vm.c', 'pw_vm_posix.c')]
EMUTLS = GL / 'src/native/pw_emutls_ps4.c'
WOWLOG = GL / 'src/native/pw_wow_log_ps4.c'
WOW_UNIX = ROOT / 'wine/wowprospero/unix.c'
NAT_UNIX = ROOT / 'wine/wow64native/unix.c'


def link_wowprx(name, unix_c, emutls, wowlog, out_dir, dbt=None):
    return [CC, '-shared', '-Wl,-Bsymbolic', '-Wl,-soname,%s.prx' % name, '-femulated-tls',
            '-D__WINESRC__', '-DWINE_UNIX_LIB', '-D_REENTRANT', '-DPW_WOW_PS4_BUDGET',
            '-I' + str(ROOT / 'wine/wowprospero'), '-I' + str(BW / 'include'), '-I' + str(WORK / 'source/include'),
            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'include'),
            unix_c, *(dbt or WOW_DBT), emutls, wowlog, PRX / 'obj/wowprospero_desc.o',
            '-o', out_dir / ('%s.shared.elf' % name)]


def link_natprx(unix_c, emutls, wowlog, out_dir):
    return [CC, '-shared', '-Wl,-Bsymbolic', '-Wl,--eh-frame-hdr', '-Wl,-soname,wow64native.prx', '-femulated-tls',
            '-D__PROSPERO__', '-D__WINESRC__', '-DWINE_UNIX_LIB', '-D_REENTRANT',
            '-I' + str(ROOT / 'wine/wow64native'), '-I' + str(BW / 'include'), '-I' + str(WORK / 'source/include'),
            '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'include'),
            unix_c, emutls, wowlog, PRX / 'obj/pw_wine_dl_libc.o', PRX / 'obj/wow64native_desc.o',
            '-o', out_dir / 'wow64native.shared.elf']


def fself(name, out_dir):
    run([GL / 'scripts/create-fself-repro.sh', OO / 'bin/linux/create-fself', '-in', out_dir / ('%s.shared.elf' % name),
         '-out', out_dir / ('%s.oelf' % name), '-lib', out_dir / ('%s.prx' % name), '-libname', '%s.prx' % name,
         '-ptype', 'fake', '-library-path', '%s:%s' % (STUBS, OO / 'lib')], 'fself-' + out_dir.name + '-' + name)
    return out_dir / ('%s.prx' % name)


UNIX_EDITS = [
    ('"wow' + OLD + ' ', '"wowblur ', None),
    ('"wow' + OLD + ':', '"wowblur:', None),
    ('"/data/' + OLD + '-win/pw_', '"' + DEV + '/pw_', None),
]
EMUTLS_EDITS = [('pw_wow_log("wow' + OLD + ' memory:', 'pw_wow_log("wowblur memory:', 1)]
WOWLOG_OLD = '''#define PW_WOW_LOG_DIR  "/data/''' + OLD + '''-win/logs"
#define PW_WOW_LOG_PATH PW_WOW_LOG_DIR "/wow''' + OLD + '''.log"
'''
WOWLOG_NEW = '''/* BLUR 1.00 (decision 28): the folder comes from the title (BLUR_LOG_DIR); without it the
 * bench dev root /data/blurdev/logs, which is never created here (BLUR never has it). */
#define PW_WOW_LOG_DEV_DIR "''' + DEV + '''/logs"
#define PW_WOW_LOG_NAME    "/wowblur.log"
#include <stdlib.h>
'''
WOWLOG_OPEN_OLD = '''    if ((fd = open(PW_WOW_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
        (void)mkdir("/data/''' + OLD + '''-win", 0777);
        (void)mkdir(PW_WOW_LOG_DIR, 0777);
        (void)chmod(PW_WOW_LOG_DIR, 0777);
        if ((fd = open(PW_WOW_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) return;
    }
'''
WOWLOG_OPEN_NEW = '''    {
        const char *dir = getenv("BLUR_LOG_DIR");
        const int title_dir = dir && dir[0];
        char path[256];
        const int n = snprintf(path, sizeof(path), "%s" PW_WOW_LOG_NAME, title_dir ? dir : PW_WOW_LOG_DEV_DIR);
        if (n <= 0 || n >= (int)sizeof(path)) return;
        if ((fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) {
            if (!title_dir) return;   /* never create the dev root */
            (void)mkdir(dir, 0777);
            (void)chmod(dir, 0777);
            if ((fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666)) < 0) return;
        }
    }
'''


def edit_unix(text, label):
    for a, b, _ in UNIX_EDITS:
        assert text.count(a) > 0, (label, a)
        text = text.replace(a, b)
    # No string literal may still carry the old name (comments may).
    import re as _re
    code = '\n'.join(l for l in text.splitlines() if not l.lstrip().startswith('#include'))   # header names are not strings
    left = [m.group(0) for m in _re.finditer(r'"[^"\n]*"', code) if OLD in m.group(0).lower()]
    assert not left, (label, left[:5])
    return text


def strings_hits(path):
    s = subprocess.check_output(['strings', '-n4', str(path)], text=True, errors='replace')
    w = subprocess.check_output(['strings', '-el', '-n4', str(path)], text=True, errors='replace')
    return [l for l in (s + w).splitlines() if OLD in l.lower() and '__wine_' + OLD + '_native_wow64_caps' not in l]


def dyn(p):
    t = subprocess.check_output(['llvm-readelf-18', '--dyn-syms', '--wide', str(p)], text=True)
    out = set()
    for line in t.splitlines():
        f = line.split()
        if len(f) >= 8 and f[0].rstrip(':').isdigit():
            out.add((f[3], f[4], f[6] == 'UND', f[7]))
    return sorted(out)


def exec_diff(a, b):
    """Executable sections (llvm-objdump -h Type TEXT; ELF and COFF) that differ between two objects."""
    def secs(o):
        out = subprocess.check_output(['llvm-objdump-18', '-h', str(o)], text=True)
        r = []
        for l in out.splitlines():
            f = l.split()
            if len(f) >= 5 and f[0].isdigit() and f[-1] == 'TEXT':
                r.append(f[1])
        return r

    def data(o, s):
        t = O / 'section.tmp'
        subprocess.check_call(['llvm-objcopy-18', '-O', 'binary', '--only-section=' + s, str(o), str(t)])
        d = t.read_bytes()
        t.unlink()
        return d
    if Path(a).read_bytes()[:4] != b'\x7fELF':
        return coff_exec_diff(a, b)
    sa, sb = secs(a), secs(b)
    assert sa, ('no executable sections found', a)
    return dict(executable_sections=len(sa),
                differing=[s for s in sorted(set(sa) | set(sb)) if s not in sa or s not in sb or data(a, s) != data(b, s)])


def coff_exec(o):
    """COFF object: [(name, raw bytes)] of IMAGE_SCN_CNT_CODE sections, in table order."""
    import struct
    d = Path(o).read_bytes()
    nsec, symptr, nsym, optsz = struct.unpack_from('<H', d, 2)[0], *struct.unpack_from('<II', d, 8), struct.unpack_from('<H', d, 16)[0]
    strtab = symptr + nsym * 18
    out = []
    for i in range(nsec):
        h = 20 + optsz + 40 * i
        name = d[h:h + 8].rstrip(b'\0').decode()
        if name.startswith('/'):
            off = strtab + int(name[1:])
            name = d[off:d.index(b'\0', off)].decode()
        rsize, rptr, relptr = struct.unpack_from('<III', d, h + 16)
        nrel = struct.unpack_from('<H', d, h + 32)[0]
        flags = struct.unpack_from('<I', d, h + 36)[0]
        if not flags & 0x20:
            continue
        # COFF x86-64 keeps relocation addends in the code bytes, so a string that moved in .rdata
        # changes them. Mask every relocated field and compare the relocation list (offset, type,
        # target symbol name) instead; the target is the same symbol, only its offset moved.
        code = bytearray(d[rptr:rptr + rsize])
        rels = []
        for r in range(nrel):
            va, sym, typ = struct.unpack_from('<IIH', d, relptr + 10 * r)
            width = {1: 8, 2: 4, 3: 4, 4: 4, 5: 4, 6: 4, 7: 4, 8: 4, 9: 4, 10: 2, 11: 4}.get(typ, 4)
            code[va:va + width] = b'\0' * width
            e = symptr + 18 * sym
            sname = d[e:e + 8]
            if sname[:4] == b'\0\0\0\0':
                off = strtab + struct.unpack_from('<I', sname, 4)[0]
                sname = d[off:d.index(b'\0', off)]
            rels.append((va, typ, sname.rstrip(b'\0').decode()))
        out.append((name, bytes(code) + repr(rels).encode()))
    return out


def coff_exec_diff(a, b):
    sa, sb = coff_exec(a), coff_exec(b)
    assert sa, ('no code sections', a)
    names_a, names_b = [n for n, _ in sa], [n for n, _ in sb]
    diff = sorted({n for (n, x), (m, y) in zip(sa, sb) if n != m or x != y} | set(names_a) ^ set(names_b))
    return dict(executable_sections=len(sa), differing=diff)


def pe_sections(path):
    """name -> sha256 of raw section data (PE)."""
    import struct
    d = Path(path).read_bytes()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    n = struct.unpack_from('<H', d, pe + 6)[0]
    opt = struct.unpack_from('<H', d, pe + 20)[0]
    sec = pe + 24 + opt
    out = {}
    for i in range(n):
        name, vsize, va, rsize, rptr = struct.unpack_from('<8sIIII', d, sec + 40 * i)
        nm = name.rstrip(b'\0').decode()
        if nm.startswith('/'):
            nm = 'strtab' + nm
        out[nm] = (rsize, hashlib.sha256(d[rptr:rptr + rsize]).hexdigest())
    return out


def main():
    if O.exists():
        sys.exit('refusing to reuse ' + str(O))
    PINS['wowprospero.prx'] = sha(PRX / 'sce_module/wowprospero.prx')
    PINS['wow64native.prx'] = sha(PRX / 'sce_module/wow64native.prx')
    assert PINS['wowprospero.prx'].startswith('606f1d8db11ba17e'), PINS['wowprospero.prx']
    assert PINS['wow64native.prx'].startswith('29a891080aa92765'), PINS['wow64native.prx']
    assert sha(BW / 'dlls/wow64/x86_64-windows/wow64.dll') == PINS['wow64.dll.build']
    import struct
    for p, e in ((BW / 'dlls/wow64/x86_64-windows/wow64.dll', EPOCH_WOW64), (B / 'native248-nativecpu/x86_64-windows/wowprospero.dll', EPOCH_CPU)):
        d = p.read_bytes(); assert struct.unpack_from('<I', d, struct.unpack_from('<I', d, 0x3c)[0] + 8)[0] == e, p
    inputs = [SYSCALL, SRC / 'dlls/wow64/wow64.spec', CPU_SRC, SPEC, WOW_UNIX, NAT_UNIX, EMUTLS, WOWLOG,
              PRX / 'obj/wowprospero_desc.o', PRX / 'obj/wow64native_desc.o', PRX / 'obj/pw_wine_dl_libc.o',
              *WOW_DBT, *[BW / 'dlls/wow64/x86_64-windows' / (n + '.o') for n in WOW64_OBJS if n != 'syscall'],
              BW / 'dlls/wow64/x86_64-windows/libwow64.a', BW / 'dlls/ntdll/x86_64-windows/libntdll.a',
              BW / 'libs/winecrt0/x86_64-windows/libwinecrt0.a', BW / 'libs/compiler-rt/x86_64-windows/libcompiler-rt.a']
    pins = {str(p): sha(p) for p in inputs}
    O.mkdir()
    ctl, blur, src = O / 'control', O / 'blur100', O / 'src'
    for d in (ctl, blur, src):
        d.mkdir()

    # ---- controls
    run(syscall_cmd(SYSCALL, ctl / 'syscall.o'), 'cc-syscall-control', BW)
    run(link_wow64(ctl / 'syscall.o', ctl / 'wow64.build.dll'), 'link-wow64-control', BW, EPOCH_WOW64)
    c1_build = sha(ctl / 'wow64.build.dll')
    stage(ctl / 'wow64.build.dll', ctl / 'wow64.dll')
    assert sha(ctl / 'wow64.dll') == PINS['wow64.dll'], ('C1 staged', sha(ctl / 'wow64.dll'), c1_build)
    run(cpu_cmd(ctl / 'wowprospero_cpu.o'), 'cc-cpu-control', BW)
    run(link_cpu(SPEC, ctl / 'wowprospero_cpu.o', ctl / 'wowprospero.build.dll'), 'link-cpu-control', BW, EPOCH_CPU)
    stage(ctl / 'wowprospero.build.dll', ctl / 'wowprospero.dll')
    assert sha(ctl / 'wowprospero.dll') == PINS['wowprospero.dll'], ('C2', sha(ctl / 'wowprospero.dll'))
    run(link_wowprx('wowprospero', WOW_UNIX, EMUTLS, WOWLOG, ctl), 'link-wowprospero-control')
    assert sha(fself('wowprospero', ctl)) == PINS['wowprospero.prx'], ('C3', sha(ctl / 'wowprospero.prx'))
    run(link_natprx(NAT_UNIX, EMUTLS, WOWLOG, ctl), 'link-wow64native-control')
    assert sha(fself('wow64native', ctl)) == PINS['wow64native.prx'], ('C4', sha(ctl / 'wow64native.prx'))
    print('PASS C1-C4 controls reproduce the shipped wow64.dll, wowprospero.dll/.prx, wow64native.prx', flush=True)

    # ---- BLUR sources
    (src / 'syscall.c').write_text(edit(SYSCALL.read_text(errors='surrogateescape'), SYSCALL_EDITS, 'syscall.c'),
                                   errors='surrogateescape')
    shutil.copyfile(SPEC, src / 'wowblur.spec')
    (src / 'unix.c').write_text(edit_unix(WOW_UNIX.read_text(), 'wowprospero/unix.c'))
    dbt = []
    for f in WOW_DBT:   # two DBT units log with the module's name ("wowprospero: ...")
        t = f.read_text()
        n = t.count('"wow' + OLD + ': ')
        if n:
            (src / f.name).write_text(t.replace('"wow' + OLD + ': ', '"wowblur: '))
            dbt.append(src / f.name)
        else:
            dbt.append(f)
    (src / 'pw_emutls_ps4.c').write_text(edit(EMUTLS.read_text(), EMUTLS_EDITS, 'pw_emutls_ps4.c'))
    wl = edit(WOWLOG.read_text(), [(WOWLOG_OLD, WOWLOG_NEW, 1), (WOWLOG_OPEN_OLD, WOWLOG_OPEN_NEW, 1)], 'pw_wow_log_ps4.c')
    (src / 'pw_wow_log_ps4.c').write_text(wl)
    diffs = ''
    for orig, new, label in ((SYSCALL, src / 'syscall.c', 'native247-wine-source/dlls/wow64/syscall.c'),
                             (SPEC, src / 'wowblur.spec', 'shared-prospero-108194e/wine/wowprospero/wowprospero.spec -> wowblur.spec'),
                             (WOW_UNIX, src / 'unix.c', 'shared-prospero-108194e/wine/wowprospero/unix.c'),
                             (EMUTLS, src / 'pw_emutls_ps4.c', 'orbis-win-9749fcd/src/native/pw_emutls_ps4.c'),
                             (WOWLOG, src / 'pw_wow_log_ps4.c', 'orbis-win-9749fcd/src/native/pw_wow_log_ps4.c'),
                             *[(f, src / f.name, 'shared-prospero-108194e/src/' + f.name) for f in WOW_DBT if (src / f.name).exists()]):
        diffs += subprocess.run(['diff', '-u', '--label', 'a/' + label, '--label', 'b/' + label, str(orig), str(new)],
                                text=True, capture_output=True).stdout
    (O / 'blur100-wow.patch').write_text(diffs)

    # ---- BLUR builds
    run(syscall_cmd(src / 'syscall.c', blur / 'syscall.o'), 'cc-syscall-blur100', BW)
    run(link_wow64(blur / 'syscall.o', blur / 'wow64.build.dll'), 'link-wow64-blur100', BW, EPOCH_WOW64)
    stage(blur / 'wow64.build.dll', blur / 'wow64.dll')
    run(link_cpu(src / 'wowblur.spec', ctl / 'wowprospero_cpu.o', blur / 'wowblur.build.dll'), 'link-cpu-blur100', BW, EPOCH_CPU)
    stage(blur / 'wowblur.build.dll', blur / 'wowblur.dll')
    run(link_wowprx('wowblur', src / 'unix.c', src / 'pw_emutls_ps4.c', src / 'pw_wow_log_ps4.c', blur, dbt), 'link-wowblur-blur100')
    fself('wowblur', blur)
    run(link_natprx(NAT_UNIX, src / 'pw_emutls_ps4.c', src / 'pw_wow_log_ps4.c', blur), 'link-wow64native-blur100')
    fself('wow64native', blur)

    # ---- checks
    res = {}
    for name in ('wow64.dll', 'wowblur.dll', 'wowblur.prx', 'wow64native.prx'):
        p = blur / name
        hits = strings_hits(p)
        assert not hits, (name, hits[:10])
        res[name] = dict(sha256=sha(p), bytes=p.stat().st_size, old_name_strings=0)
    pe_cmp = {}
    for new, old in (('wow64.dll', ctl / 'wow64.dll'), ('wowblur.dll', ctl / 'wowprospero.dll')):
        a, b = pe_sections(old), pe_sections(blur / new)
        pe_cmp[new] = dict(identical=sorted(k for k in a if a.get(k) == b.get(k)),
                           changed=sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k)))
    # Relocatable-object evidence: executable sections of every renamed unit, control vs BLUR
    # (addresses are still unresolved there, so a string-only change leaves them identical).
    obj_cmp = {}
    ev = O / 'evidence'
    ev.mkdir()
    obj_cmp['syscall.o'] = exec_diff(ctl / 'syscall.o', blur / 'syscall.o')
    unix_flags = ['-c', '-fPIC', '-femulated-tls', '-D__WINESRC__', '-DWINE_UNIX_LIB', '-D_REENTRANT', '-DPW_WOW_PS4_BUDGET',
                  '-I' + str(ROOT / 'wine/wowprospero'), '-I' + str(BW / 'include'), '-I' + str(WORK / 'source/include'),
                  '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'include')]
    pairs = [(WOW_UNIX, src / 'unix.c'), (EMUTLS, src / 'pw_emutls_ps4.c'), (WOWLOG, src / 'pw_wow_log_ps4.c'),
             *[(f, src / f.name) for f in WOW_DBT if (src / f.name).exists()]]
    for a_src, b_src in pairs:
        a_o, b_o = ev / ('control-' + a_src.name + '.o'), ev / ('blur100-' + b_src.name + '.o')
        run([CC, *unix_flags, a_src, '-o', a_o], 'ev-control-' + a_src.name)
        run([CC, *unix_flags, b_src, '-o', b_o], 'ev-blur100-' + b_src.name)
        obj_cmp[a_src.name] = exec_diff(a_o, b_o)
    for k, v in obj_cmp.items():
        if k != 'pw_wow_log_ps4.c':
            assert not v['differing'], (k, v)
    dyn_cmp = {}
    for new, old in (('wowblur', 'wowprospero'), ('wow64native', 'wow64native')):
        a, b = dyn(ctl / ('%s.shared.elf' % old)), dyn(blur / ('%s.shared.elf' % new))
        dyn_cmp[new] = dict(added=[list(x) for x in sorted(set(b) - set(a))], removed=[list(x) for x in sorted(set(a) - set(b))])
    for p, h in pins.items():
        assert sha(p) == h, ('input changed', p)
    manifest = dict(status='PASS_BUILD_NOT_PS4_TESTED', controls={k: v for k, v in PINS.items()},
                    control_wow64_unstripped=c1_build, outputs=res, pe_sections=pe_cmp, object_exec_sections=obj_cmp, prx_dynamic_symbols=dyn_cmp,
                    patch_sha256=sha(O / 'blur100-wow.patch'), input_pins=pins, commands=commands, PS4_tested=False)
    (O / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k: v for k, v in manifest.items() if k not in ('commands', 'input_pins')}, indent=2))


if __name__ == '__main__':
    main()

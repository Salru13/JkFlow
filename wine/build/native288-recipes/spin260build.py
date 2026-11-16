#!/usr/bin/env python3
"""Build ntdll.prx with the optional bounded event-wait spin (PW_SYNC_SPIN_US).

Evidence: 18.6% of game-thread samples are in NtSetEvent. A SetEvent that finds a
worker already asleep in the server must take the slow path. A short spin before
sleeping keeps the worker in user space, so the setter's fast path succeeds.

Unset PW_SYNC_SPIN_US means no spin. Base is the shipped 0.8.18+ ntdll: upstream
1075 closure plus the clock fix. Controls: unmodified sources must compile to the
admitted .text, and an unmodified relink must reproduce the admitted ELF.
Run in WSL.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True
import clockfix260 as clock  # noqa: E402

NATIVE = HERE.parent
BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
ORBIS = BASE / 'orbis-win-9749fcd'
SDK = ORBIS / 'sdk/orbis-sdk-v1/sdk'
WORK = BASE / 'native248-build-wine/build'
HANDLES = BASE / 'native288-standard-handles'
OUT = BASE / 'native288-spin260'
SERVER_C = BASE / 'native265-shared-r1/server.c'
SERVER_O = BASE / 'native265-shared-r1/server.o'
SYNC_C = BASE / 'native264-assembled-r5/dlls/ntdll/unix/sync.c'
SYNC_O = BASE / 'native264-assembled-r5/objects/runtime_qualified_backend.o'
INC = NATIVE.parent / 'agent/orbis-tls/task258_sync_spin.inc'
# Optional: a replacement loader object (override260build.py) and output names.
DL_O = None
DL_ADMITTED = BASE / 'native248-build-wine/prx/obj/pw_wine_dl.o'
MANIFEST = 'spin260-manifest.json'
ADMITTED_ELF = '279aed0cfa13d7f48f863f4131b094af6dc91c7fd5fa6d4ed1b6a9047d93f881'

ENV = dict(os.environ, PW_PS4_ROOT=str(ORBIS), MESA_BUILD=str(BASE / 'native248-platform'), OO_PS4_TOOLCHAIN=str(SDK),
           PW_PRX_LINK='1', PW_SHARED_LIBC='1', PW_PRX_STUBS='-L' + str(HANDLES / 'empty-stubs') + ' -Wl,--as-needed')
ENV['PATH'] = ':'.join(str(p) for p in (BASE / 'native248-build-wine/tools', BASE / 'private-bin',
                                        BASE / 'deps/rootfs/usr/bin')) + ':' + ENV['PATH']

SERVER_FUNC = '''int server_spin_shared_wait(HANDLE handle, const LARGE_INTEGER *timeout)
{
#ifdef WINE_INPROCESS_SERVER
    return pw258_spin_shared_wait(handle, timeout);
#else
    return 0;
#endif
}

'''
SYNC_OLD = ('    if (!alertable && (ret = server_try_shared_sync(handle, PW_SYNC_WAIT, 0, timeout, NULL)) != STATUS_NOT_IMPLEMENTED)\n'
            '        return ret;\n')
SYNC_ADD = ('\n    {\n        extern int server_spin_shared_wait(HANDLE handle, const LARGE_INTEGER *timeout);\n'
            '        if (!alertable && server_spin_shared_wait( handle, timeout ))\n            return STATUS_SUCCESS;\n    }\n')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, cwd=WORK):
    result = subprocess.run([str(a) for a in command], cwd=cwd, env=ENV, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'failed: {str(command[0])[-40:]}\n{result.stdout[-1500:]}\n{result.stderr[-2500:]}')


def text_digest(obj, tag):
    target = OUT / f'{tag}.text'
    subprocess.run(['llvm-objcopy-18', '-O', 'binary', '--only-section=.text', str(obj), str(target)], check=True)
    return sha(target)


def eol_of(text):
    return '\r\n' if '\r\n' in text else '\n'


def main():
    shutil.rmtree(OUT, ignore_errors=True)  # only this script's own output
    (OUT / 'src').mkdir(parents=True)
    include = INC.read_text()
    unset = 'ticks = pw258_parse_ticks(getenv("PW_SYNC_SPIN_US"));'
    assert include.count(unset) == 1
    # On by default at 50 us; PW_SYNC_SPIN_US=0 turns it off.
    include = include.replace(unset, '{ const char *pw260_value = getenv("PW_SYNC_SPIN_US"); ticks = pw260_value ? pw258_parse_ticks(pw260_value) : (int64_t)50 * PW258_TSC_PER_US; }')
    (OUT / 'src/pw258_sync_spin.inc').write_text(include)
    server_compile = json.loads((BASE / 'native265-shared-r1/compile-receipt.json').read_text())['command']
    receipt = json.loads((BASE / 'native264-assembled-r5/objects/compile-receipt.json').read_text())
    sync_compile = next(o['command'] for o in receipt['objects'] if o['name'] == 'runtime_qualified_backend')

    def build(template, source_old, source_new, output, extra=()):
        command = list(template)
        command[command.index('-o') + 1] = str(output)
        command[command.index(str(source_old))] = str(source_new)
        run(command[:1] + list(extra) + command[1:])

    # Controls
    control_server = OUT / 'control-server.o'
    build(server_compile, SERVER_C, SERVER_C, control_server)
    control_sync = OUT / 'control-sync.o'
    build(sync_compile, SYNC_C, SYNC_C, control_sync)
    same_server = text_digest(control_server, 'cs') == text_digest(SERVER_O, 'as')
    same_sync = text_digest(control_sync, 'cy') == text_digest(SYNC_O, 'ay')
    print('control compile matches admitted: server', same_server, 'sync', same_sync)
    assert same_server and same_sync

    server = SERVER_C.read_text()
    eol = eol_of(server)
    anchor = '/* Called under fd_cache_mutex through all existing mutex cache invalidations. */'
    assert server.count(anchor) == 1
    at = server.index(anchor)
    end_marker = eol + '#endif' + eol
    at_end = server.index(end_marker, at)
    server = server[:at_end] + eol + '#include "pw258_sync_spin.inc"' + server[at_end:]
    func_anchor = 'unsigned int server_try_shared_sync(HANDLE handle, unsigned int operation, unsigned int count,'
    assert server.count(func_anchor) == 1
    server = server.replace(func_anchor, SERVER_FUNC.replace('\n', eol) + func_anchor)
    (OUT / 'src/server.c').write_text(server)
    new_server = OUT / 'server.o'
    build(server_compile, SERVER_C, OUT / 'src/server.c', new_server,
          ['-I' + str(OUT / 'src'), '-iquote', str(BASE / 'native247-wine-source/dlls/ntdll/unix')])

    sync = SYNC_C.read_text()
    eol = eol_of(sync)
    old = SYNC_OLD.replace('\n', eol)
    assert sync.count(old) == 1
    sync = sync.replace(old, old + SYNC_ADD.replace('\n', eol))
    (OUT / 'src/sync.c').write_text(sync)
    new_sync = OUT / 'sync.o'
    build(sync_compile, SYNC_C, OUT / 'src/sync.c', new_sync, ['-iquote', str(SYNC_C.parent)])

    commands = {c['name']: c['command'] for c in json.loads((NATIVE / 'native288-standard-handles-build.json').read_text())['commands']}
    link = commands['candidate-link']
    convert = commands['convert']

    def link_elf(output, server_object, sync_object, dl_object=None):
        command = list(link)
        command[command.index('-o') + 1] = str(output)
        command[command.index(str(SERVER_O))] = str(server_object)
        command[command.index(str(SYNC_O))] = str(sync_object)
        if dl_object is not None:
            command[command.index(str(DL_ADMITTED))] = str(dl_object)
        run(command)

    (OUT / 'control').mkdir()
    control_elf = OUT / 'control/candidate.elf'
    link_elf(control_elf, SERVER_O, SYNC_O)
    print('control relink matches admitted ELF:', sha(control_elf) == ADMITTED_ELF)
    assert sha(control_elf) == ADMITTED_ELF

    elf = OUT / 'candidate.elf'
    link_elf(elf, new_server, new_sync, DL_O)
    data = bytearray(elf.read_bytes())
    start, size = clock.symbol_range(elf)
    offset = clock.file_offset(bytes(data), start)
    import re
    window = bytes(data[offset:offset + size])
    hits = [m.start() for m in re.finditer(re.escape(clock.OLD) + rb'....\x48\x8b\x35', window, re.S)]
    assert len(hits) == 1
    data[offset + hits[0]:offset + hits[0] + 3] = clock.NEW
    elf.write_bytes(data)

    command = list(convert)
    command[command.index('-in') + 1] = str(elf)
    command[command.index('-out') + 1] = str(OUT / 'candidate.oelf')
    command[command.index('-lib') + 1] = str(OUT / 'ntdll.prx')
    run(command)
    clock.assert_bss_mapped(elf, OUT / 'candidate.oelf')
    undefined = subprocess.check_output(['llvm-nm-18', '-u', str(elf)], text=True)
    admitted = subprocess.check_output(['llvm-nm-18', '-u', str(HANDLES / 'candidate.elf')], text=True)
    added = sorted(set(undefined.split()) - set(admitted.split()))
    assert not added, added
    manifest = {'elf': str(elf), 'elf_sha256': sha(elf), 'prx': str(OUT / 'ntdll.prx'), 'prx_sha256': sha(OUT / 'ntdll.prx'),
                'prx_bytes': (OUT / 'ntdll.prx').stat().st_size, 'control_compile_exact': True, 'control_elf_exact': True,
                'clock_fix_applied': True, 'dl_object': str(DL_O) if DL_O else None, 'new_imports': added}
    (HERE / MANIFEST).write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()

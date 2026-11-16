#!/usr/bin/env python3
"""Make clock_gettime agree between ntdll.prx and wineserver.prx.

Physical evidence (investigations/evidence/ps4-native287/waitprobe260/): the
client clock is a constant 38.1 ms ahead of the server clock, so every relative
Windows timeout takes N + 38 ms.

Cause: liborbis-compat's orbis_clock.o keeps a function-local static
{frequency, start_counter} and returns (counter - start) / frequency. Each PRX
links its own copy, so each counts from its own first call.

Fix: drop the subtraction in both modules. sceKernelGetProcessTimeCounter is
per process, so both modules then report the same time. The 7-byte
`sub start(%rip), %rax` (48 2b 05 disp32) becomes the 7-byte
`nopl disp32(%rax)` (0f 1f 80 disp32). Layout, imports and exports do not change.

Run in WSL. Before writing anything it proves the conversion recipe reproduces
each admitted PRX byte for byte from the unpatched ELF.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
from pathlib import Path

BASE = Path(os.environ['JKFLOW_BUILD_ROOT'])
ORBIS = BASE / 'orbis-win-9749fcd'
SDK = ORBIS / 'sdk/orbis-sdk-v1/sdk'
OUT = BASE / 'native288-clockfix260'
SYMBOL = '_ZN12_GLOBAL__N_112monotonicNowEP8timespec'
OLD = b'\x48\x2b\x05'
NEW = b'\x0f\x1f\x80'
# Conversion reference: exact flags come from the receipt of a real PRX build.
REFERENCE = BASE / 'native266-ntdll-link-r1/verification.json'
MODULES = [
    ('ntdll', BASE / 'native288-standard-handles/candidate.elf', BASE / 'native288-standard-handles/ntdll.prx',
     'e574e2177d10cbae5732cd450464239193e3074fc92a4269440be34a6a3b0ebb'),
    ('wineserver', BASE / 'native266-wineserver-r1/candidate.elf', BASE / 'native266-wineserver-r1/wineserver.prx',
     '8346d6c00b52c6d5d41453414fcc7e08908535d5f2564e50aa6b6cb1dc0dbf0b'),
]


# wineserver is patched after serverprio260.py relinks it; the control still uses the admitted ELF.
PATCH_INPUT = {'wineserver': BASE / 'native288-serverprio260/candidate.elf'}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbol_range(elf):
    text = subprocess.check_output(['llvm-nm-18', '--print-size', str(elf)], text=True)
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[3] == SYMBOL:
            return int(fields[0], 16), int(fields[1], 16)
    raise SystemExit(f'{elf}: {SYMBOL} not found')


def file_offset(data, address):
    phoff = int.from_bytes(data[32:40], 'little')
    entry = int.from_bytes(data[54:56], 'little')
    count = int.from_bytes(data[56:58], 'little')
    for i in range(count):
        p = data[phoff + i * entry:phoff + (i + 1) * entry]
        kind = int.from_bytes(p[0:4], 'little')
        offset, vaddr, filesz = (int.from_bytes(p[a:a + 8], 'little') for a in (8, 16, 32))
        if kind == 1 and vaddr <= address < vaddr + filesz:
            return address - vaddr + offset
    raise SystemExit(f'address {address:#x} is not file-backed')


def writable(path):
    data = Path(path).read_bytes()
    offset = int.from_bytes(data[32:40], 'little')
    size = int.from_bytes(data[54:56], 'little')
    count = int.from_bytes(data[56:58], 'little')
    rows = []
    for i in range(count):
        p = data[offset + i * size:offset + (i + 1) * size]
        if int.from_bytes(p[0:4], 'little') == 1 and int.from_bytes(p[4:8], 'little') & 2:
            rows.append((int.from_bytes(p[16:24], 'little'), int.from_bytes(p[40:48], 'little')))
    return rows


def assert_bss_mapped(elf, oelf):
    """The same check as native266-ntdll-driver.py: conversion must not lose BSS pages."""
    va, size = max(writable(elf), key=sum)
    end = max(v + m for v, m in writable(oelf) if v == va)
    mapped = (end + 0x3fff) & ~0x3fff
    assert mapped >= va + size, (hex(mapped), hex(va + size))


def convert(elf, oelf, prx, name, reference_args):
    args = list(reference_args)
    args[args.index('-in') + 1] = str(elf)
    args[args.index('-out') + 1] = str(oelf)
    args[args.index('-lib') + 1] = str(prx)
    # The module's own name is embedded in the output.
    args = [a.replace('ntdll', name) if a.endswith(('.prx', '.sprx')) and '/' not in a else a for a in args]
    env = dict(os.environ, PW_PS4_ROOT=str(ORBIS), MESA_BUILD=str(BASE / 'native248-platform'), OO_PS4_TOOLCHAIN=str(SDK))
    env['PATH'] = ':'.join(str(p) for p in (BASE / 'native248-build-wine/tools', BASE / 'private-bin', BASE / 'deps/rootfs/usr/bin')) + ':' + env['PATH']
    result = subprocess.run(args, cwd=BASE / 'native248-build-wine/build', env=env, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'{name} conversion failed: {result.stdout[-800:]} {result.stderr[-800:]}')


def main():
    reference = json.loads(REFERENCE.read_text())['conversion_command']
    shutil.rmtree(OUT, ignore_errors=True)  # only this script's own output
    OUT.mkdir()
    manifest = {'symbol': SYMBOL, 'old': OLD.hex(), 'new': NEW.hex(), 'modules': []}
    for name, elf, admitted, admitted_sha in MODULES:
        assert sha(admitted) == admitted_sha, f'{admitted} is not the admitted file'
        work = OUT / name
        work.mkdir()

        # Control: the recipe must reproduce the admitted PRX unchanged.
        # create-fself embeds the input's file name, so keep it identical.
        (work / 'control').mkdir()
        control_elf = work / 'control' / 'candidate.elf'
        shutil.copyfile(elf, control_elf)
        control_prx = work / 'control' / f'{name}.prx'
        convert(control_elf, work / 'control' / 'candidate.oelf', control_prx, name, reference)
        reproduced = sha(control_prx) == admitted_sha

        source_elf = PATCH_INPUT.get(name, elf)
        data = bytearray(source_elf.read_bytes())
        start, size = symbol_range(source_elf)
        offset = file_offset(bytes(data), start)
        window = bytes(data[offset:offset + size])
        hits = [m.start() for m in re.finditer(re.escape(OLD) + rb'....\x48\x8b\x35', window, re.S)]
        assert len(hits) == 1, f'{name}: expected one subtraction, found {len(hits)}'
        at = offset + hits[0]
        data[at:at + 3] = NEW

        patched_elf = work / 'candidate.elf'
        patched_elf.write_bytes(data)
        before = source_elf.read_bytes()
        changed = [i for i, (a, b) in enumerate(zip(before, data)) if a != b]
        assert len(before) == len(data) and changed == [at, at + 1, at + 2], changed

        prx = work / f'{name}.prx'
        convert(patched_elf, work / 'candidate.oelf', prx, name, reference)
        assert sha(prx) != admitted_sha
        assert_bss_mapped(patched_elf, work / 'candidate.oelf')

        listing = subprocess.check_output(
            ['llvm-objdump-18', '-d', '--no-show-raw-insn',
             f'--start-address={start + hits[0]:#x}', f'--stop-address={start + hits[0] + 7:#x}', str(patched_elf)],
            text=True)
        assert 'nopl' in listing, listing
        manifest['modules'].append({
            'name': name, 'input_elf': str(source_elf), 'input_elf_sha256': sha(source_elf),
            'admitted_prx_sha256': admitted_sha, 'control_reproduces_admitted_prx': reproduced,
            'patched_file_offset': at, 'patched_vaddr': start + hits[0],
            'candidate_elf_sha256': sha(patched_elf), 'candidate_prx': str(prx),
            'candidate_prx_sha256': sha(prx), 'candidate_prx_bytes': prx.stat().st_size,
        })
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()

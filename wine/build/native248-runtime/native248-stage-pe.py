"""Stage debug-stripped PE modules without changing compiled or baseline inputs."""
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

compiled, providers, out = map(Path, sys.argv[1:4])
if out.exists():
    raise SystemExit('output must not exist')
required = {'i386-windows': ('ntdll', 'win32u', 'winevulkan'),
            'x86_64-windows': ('ntdll', 'win32u', 'winevulkan', 'wow64')}
for arch, names in required.items():
    for name in names:
        if not (compiled / arch / (name + '.dll')).is_file():
            raise SystemExit(f'missing {arch}/{name}.dll')
for name in ('wow64native', 'wowprospero'):
    if not (providers / 'x86_64-windows' / (name + '.dll')).is_file():
        raise SystemExit('missing provider ' + name)
strip = shutil.which('llvm-strip-18')
if not strip:
    raise SystemExit('llvm-strip-18 required')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def pe(path, machine):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('not PE: ' + str(path))
    pos = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pos:pos+4] != b'PE\0\0' or struct.unpack_from('<H', data, pos+4)[0] != machine:
        raise ValueError('PE machine mismatch: ' + str(path))
    if not struct.unpack_from('<H', data, pos+22)[0] & 0x2000:
        raise ValueError('missing DLL characteristic: ' + str(path))
    return b'Wine builtin DLL' in data[:pos]
out.mkdir(parents=True)
records = []
for arch, machine in [('i386-windows', 0x14c), ('x86_64-windows', 0x8664)]:
    target = out / arch
    target.mkdir()
    files = {p.name: p for p in (compiled / arch).glob('*.dll')}
    for p in (providers / arch).glob('*.dll'):
        if p.name in files:
            raise ValueError('duplicate module ' + p.name)
        files[p.name] = p
    for name, src in sorted(files.items()):
        builtin = pe(src, machine)
        dst = target / name
        shutil.copyfile(src, dst)
        subprocess.run([strip, '--strip-debug', str(dst)], check=True)
        if pe(dst, machine) != builtin:
            raise ValueError('builtin marker changed: ' + str(dst))
        records.append({'file': f'{arch}/{name}', 'source': str(src),
                        'source_sha256': digest(src), 'sha256': digest(dst),
                        'size': dst.stat().st_size, 'wine_builtin': builtin})
(out / 'stage-receipt.json').write_text(json.dumps(records, indent=2) + '\n')
print(json.dumps({'modules': len(records), 'bytes': sum(r['size'] for r in records), 'output': str(out)}))

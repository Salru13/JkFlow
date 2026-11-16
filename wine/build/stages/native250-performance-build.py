#!/usr/bin/env python3
"""Relink only a private win32u PRX with the unchanged upstream RADV profiler.
Never builds or replaces Mesa, Wine PE modules, the active PRXs, or a package.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--bootstrap', type=Path, default=Path(os.environ['JKFLOW_BUILD_ROOT']))
ap.add_argument('--output', type=Path)
ap.add_argument('--compile-only', action='store_true')
args = ap.parse_args()
here = Path(__file__).resolve().parent
boot = args.bootstrap.resolve()
out = (args.output or boot / 'native250-performance/prx').resolve()
old = boot / 'native248-build-wine/prx'
if out == old.resolve() or old.resolve() in out.parents:
    raise SystemExit('Refusing to write inside the active PRX build')
orbis = boot / 'orbis-win-9749fcd'
pw = here / 'shared-prospero-108194e'
sdk = orbis / 'sdk/orbis-sdk-v1/sdk'
cc = orbis / 'build/ps4-wine-sdk/bin/prospero-clang'
vkinclude = orbis / 'sdk/orbis-sdk-v1/mesa/include'
obj = out / 'obj'
obj.mkdir(parents=True, exist_ok=True)
(out / 'sce_module').mkdir(exist_ok=True)
env = dict(os.environ, PW_SHARED_LIBC='1', PW_PRX_LINK='1', OO_PS4_TOOLCHAIN=str(sdk))
env['LLVM_OBJCOPY'] = str(orbis / 'build/ps4-wine-sdk/bin/llvm-objcopy')
stubs = old / 'stubs'
env['PW_PRX_STUBS'] = ' '.join(['-L' + str(stubs), '-Wl,--as-needed'] +
                             [str(p) for p in sorted(stubs.glob('*.so'))])


def run(argv, **kwargs):
    subprocess.run([str(v) for v in argv], env=env, check=True, **kwargs)


flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC',
         '-I' + str(vkinclude), '-I' + str(pw / 'wine/ps5')]
units = [
    (pw / 'wine/ps5/pw_vk_radv_profile.c', []),
    (pw / 'wine/ps5/pw_vk_radv_profile_wrap.c',
     ['-Dfprintf=pw_native250_profile_fprintf', '-Dgetenv=pw_native250_profile_getenv']),
    (here / 'native250-performance-loader.c', []),
    (here / 'native250-performance-log.c', []),
]
objects = []
for source, extra in units:
    target = obj / (source.stem + '.o')
    run([cc, *flags, *extra, '-c', source, '-o', target])
    objects.append(target)
run(['python3', pw / 'tools/gen_prx_descriptor.py', obj / 'win32u_desc.c',
     '__wine_unix_lib_init', 'pw_vk_radv_profile_set_frame_hook'])
run([cc, *flags, '-c', obj / 'win32u_desc.c', '-o', obj / 'win32u_desc.o'])
objects.append(obj / 'win32u_desc.o')
if args.compile_only:
    print('Native250 profiler SDK objects: PASS (no relink)')
    raise SystemExit(0)

# Same make.log object list and forwarding shims used by prx-ps4.sh, without
# rebuilding unrelated modules. The official Vulkan driver remains untouched.
text = (boot / 'native248-build-wine/make.log').read_text().replace('\\\n', ' ')
links = [line for line in text.splitlines() if '-o dlls/win32u/win32u.so ' in line]
if not links:
    raise SystemExit('No recorded win32u object list')
wine_objects = [arg for arg in shlex.split(links[-1]) if arg.endswith('.o') and not arg.startswith('/')]
if not wine_objects:
    raise SystemExit('Empty win32u object list')
elf = out / 'win32u.shared.elf'
with (out / 'win32u.link.log').open('w') as log:
    run([cc, '-shared', '-Wl,-Bsymbolic', '-Wl,--eh-frame-hdr', '-Wl,-soname,win32u.prx',
         '-Wl,--warn-unresolved-symbols', '-Wl,--wrap=dlopen', '-Wl,--wrap=dlsym',
         '-o', elf, *wine_objects, old / 'heap/pw_wine_heap_libc.o',
         old / 'obj/pw_wine_dl_libc.o', *objects],
        cwd=boot / 'native248-build-wine/build', stdout=log, stderr=subprocess.STDOUT)
    run([orbis / 'scripts/create-fself-repro.sh', sdk / 'bin/linux/create-fself',
         '-in', elf, '-out', out / 'win32u.oelf', '-lib', out / 'sce_module/win32u.prx',
         '-libname', 'win32u.prx', '-ptype', 'fake', '-library-path', str(stubs) + ':' + str(sdk / 'lib')],
        stdout=log, stderr=subprocess.STDOUT)
files = [source for source, _ in units] + [old / 'sce_module/win32u.prx',
         old / 'sce_module/libvulkan.prx', elf, out / 'sce_module/win32u.prx']
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
(out / 'provenance.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('Native250 private win32u profiling PRX:', out / 'sce_module/win32u.prx')

#!/usr/bin/env python3
"""override260build.py: the current ntdll (spin260) with one change in its Unix-module loader.

pw_wine_dl first looks for <name>.prx in /data/prospero-win/override. When the file exists it is
loaded from there; when it is absent nothing changes (one stat, no load attempt); when the console
refuses it the packaged path is used. This lets a native module such as winevulkan.prx be replaced
over FTP instead of through a package.

Controls: the unmodified pw_wine_dl.c compiles to the admitted object byte for byte, and the
unmodified relink reproduces the admitted ntdll ELF (inside spin260build).
"""
import hashlib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import spin260build as spin  # noqa: E402

BASE = spin.BASE
SRC = BASE / 'prospero-win-108194e/wine/ps5/pw_wine_dl.c'
CC = BASE / 'orbis-win-9749fcd/build/ps4-wine-sdk/bin/prospero-clang'
OUT = BASE / 'native288-override260'
FLAGS = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-c']
OVERRIDE_DIR = '/data/prospero-win/override'

INCLUDE_OLD = '#include <string.h>\n'
INCLUDE_NEW = ('#include <string.h>\n#include <sys/stat.h>\n'
               '#ifndef PW_WINE_DL_OVERRIDE_DIR\n#define PW_WINE_DL_OVERRIDE_DIR "%s"\n#endif\n' % OVERRIDE_DIR)
OPEN_OLD = '    int bare=!strchr(path,\'/\');\n'
OPEN_NEW = (OPEN_OLD +
            '    /* A module placed in the override directory wins; an absent one costs one stat. */\n'
            '    {struct stat st;\n'
            '     if(prx_path(path,PW_WINE_DL_OVERRIDE_DIR,candidate) && !stat(candidate,&st))handle=try_load(candidate);}\n')
GUARD_OLD = '    if(!bare && prx_path(path,NULL,candidate))handle=try_load(candidate);\n'
GUARD_NEW = '    if(handle<0 && !bare && prx_path(path,NULL,candidate))handle=try_load(candidate);\n'


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def patched_source():
    s = SRC.read_text()
    for old, new in ((INCLUDE_OLD, INCLUDE_NEW), (OPEN_OLD, OPEN_NEW), (GUARD_OLD, GUARD_NEW)):
        assert s.count(old) == 1, old
        s = s.replace(old, new)
    return s


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'src').mkdir(exist_ok=True)
    src = OUT / 'src/pw_wine_dl.c'
    src.write_text(patched_source())
    subprocess.run(['bash', str(HERE / 'tests/run_override_test.sh'), str(src)], check=True)

    control = OUT / 'control-dl.o'
    subprocess.run([str(CC), *FLAGS, str(SRC), '-o', str(control)], check=True, cwd=SRC.parent)
    assert sha(control) == sha(spin.DL_ADMITTED), 'control compile differs from the admitted loader object'
    print('control loader compile matches admitted object')
    candidate = OUT / 'pw_wine_dl.o'
    subprocess.run([str(CC), *FLAGS, '-iquote', str(SRC.parent), str(src), '-o', str(candidate)], check=True)

    spin.OUT = OUT / 'ntdll'
    spin.DL_O = candidate
    spin.MANIFEST = 'override260-manifest.json'
    spin.main()


if __name__ == '__main__':
    main()

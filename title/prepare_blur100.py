#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright 2026 XxXJoker13XxX and contributors
"""Isolated title trees for BLUR 1.00 r4 (plan Task T1), in WSL.

    JKFLOW_BUILD_ROOT=<build root> python3 -I prepare_blur100.py control  -> <build root>/blur-launcher-r4-control
    JKFLOW_BUILD_ROOT=<build root> python3 -I prepare_blur100.py r4       -> <build root>/blur-launcher-r4
    python3 -I prepare_blur100.py r4 NAME  -> the same tree in a scratch folder <bootstrap>/NAME
    python3 -I prepare_blur100.py base NAME -> r4 before title-blur100.patch, the patch's base/ side;
        the patch is  diff -ruN base/<f> r4/<f>  over the R4_SHAS files (base, r4 = two trees)

JKFLOW_BUILD_ROOT (required) is the build root that holds the base title tree
native298-package277 and the engine seed. Small source folders are copied,
everything else is a symlink to the same read-only inputs of
native298-package277. Nothing there or in any other tree is written. The
launcher sources come from ../launcher in this repository (src/ and the baked
build/gen/ files of launcher/tests/run.sh or launcher/tools/bake_assets.sh).

"control" changes nothing but the output folder: its eboot.bin and
relocated_wine_app.prx must equal native298's (0.8.33).

"r4" is the BLUR 1.00 title:
  - ids as r2/r3: TITLE_ID BLUR00001, TITLE BLUR, PKG_VERSION 01.00,
    PW_BUILD_ID BLUR-1.00, -DPW_TITLE_ID for the app and the relocated PRX;
  - the launcher UI (launcher/src after L1-L10, and the baked font/logo);
  - wine64_main.c = title-r1.patch, prod-audit/title-prod.patch (dc1a0e57),
    then title-blur100.patch (also native245_seed.[ch], the menu/keyboard
    default titles, the wowblur names, the PE-runtime switch of
    build-app-ps4.sh, the dead start page strings, pw_present.c's
    one-pass 32-bit present of owner finding 34, and audit finding 4:
    the PS4 local name made portable by blur100-display-name.[ch] instead
    of the system-keyboard name page, which is no longer compiled; and
    audit finding 7: 48 env lines before the title's last ones,
    pw_wine_start.h's PW_WINE_START_MAX_ENV 52; and owner option A
    2026-11-17: the uncalled start page pw_blur_start_render, its blur_logo
    and the pw_blur_logo.h include are removed, so the title prx holds no
    copy of the Blur logo bitmap);
  - the engine seed of E5 (N245_SEED_DIR/N245_SEED_END below) and its PE
    runtime union (PW_PELIB, CPU backend wowblur.dll) for the stage checks;
  - build-launcher.sh ends with postlink_clean.py run (audit findings 6+10:
    no build-host path or dev env-file name in the title prx, proved).
"""
import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

B = Path(os.environ.get('JKFLOW_BUILD_ROOT') or sys.exit('set JKFLOW_BUILD_ROOT to the build root'))
OLD = B / 'native298-package277'
HERE = Path(__file__).resolve().parent
LAUNCHER = HERE.parent / 'launcher'             # launcher/ of this repository
OLD_MAIN_SHA = 'd4f56484184ee7c8a2f3f22ed71ef6f0f20ba01e0aa416ac6eb028e131cf43ed'
R1_MAIN_SHA = '831a3c12b905795106292e2b22921d57355254ac2e62521749f05cd2de37cc7b'
R2_MAIN_SHA = 'dc1a0e57a947094136a442061adda09d5b7dbb7629cde13e142d323e001b02df'
TITLE_ID = 'BLUR00001'
BUILD_ID = 'BLUR-1.00'
SEED_DIR = B / 'blur100-paths6/seed-build/seed'   # finding 6: build-host paths removed (was blur100-seed/seed, END 4f9bef01)
SEED_END = 'ed7b2bdb557d3d3e49c2ca72b027fc28cdb6ec7d9d689d49204c5d23e93925e7'
PELIB = B / 'blur100-paths6/seed-build/runtime'
# Every file title-blur100.patch writes, as it must read afterwards.
R4_SHAS = {
    'orbis/scripts/build-app-ps4.sh': '4615733285cfc0e1282360eddc20fc14d77b48f7ffa644e8c050b0094a3f2c0a',
    'orbis/src/native/pw_wow_log_ps4.c': 'b6b12cd81e3440dc490899771cb0d64d028aab1f189dd01e66944f146d06c1f8',
    'prospero/native/blur100-display-name-orbis.c': 'd1656bc9eee3f03b1535049e2ff43b8a03ab5de378d57e391306dacf96390b60',
    'prospero/native/blur100-display-name.c': '72f22d1b26acb024e59d67215420177c0a8bb232534de3d81910c7e1a2a7234f',
    'prospero/native/blur100-display-name.h': '0827d6fa0f98941d5a1b6e2fccdd647aad2ac168167561d057bca69dc3c386f8',
    'prospero/native/native245_seed.c': '7b8e857c878f9cfff86ec4c2457d8d18ad8640ac469c52da551b20f6cd52352d',
    'prospero/native/native245_seed.h': 'e63b4542a3446c03fc20eea6164c6c508b36c78f2844dbd2f4914137b758d2dc',
    'prospero/native/pw_diagnostics.c': 'e7a5407b6ec8898296265a2031942d4fafac25b9c0351e7f4fdf287289de727e',
    'prospero/native/pw_diagnostics.h': 'c134a56c808816856c02145f90ecfdb28685df7bdad71b645d0ebb4208cc6f56',
    'prospero/native/pw_osk_ps5.c': '2304e36943608ea0c80821e8d6eed18ad3c6004bf4012393b9c5bfdd8cee3c81',
    'prospero/native/wine64_main.c': 'c82f7688cc07bbf32aa08a070a6574c0ea0e3f1f21aadf5d91cf1a6ad77a3d16',
    'prospero/src/pw_game_menu.c': '6fbce3fe1e99f65af9c6d2868307106bb094e2b67cb4f88323473f49be452225',
    'prospero/src/pw_game_profile.c': 'c2074468c2147d54de6aa1838d92c3cc102d0ee4d12deeaae372773bddaba7d6',
    'prospero/src/pw_launcher_render.c': '918d2edf535c6b32979cccc07bfe9f1e1e0625484e5719d2859ecd3e95ebeb92',
    'prospero/src/pw_present.c': '9f5baf9904a657191464aa9900222e3749c2174142025fc5b74a0532226dea99',
    'prospero/src/pw_profile_catalog.c': 'dd0652a0a5602660d331cd55480893b2aa16514dbdd12883f87e58cf66d77ea3',
    'prospero/src/pw_wine_start.h': '282ff066b306d9767eb8643665d9f805c4295d31da3b624e141243860ec75536',
    'prospero/src/pw_x86_block.c': '4ce6b186b37415cf2188f86dd16e36f118c5e8d3c8f7c48fdc8ffcd84f33c90c',
    'prospero/src/pw_x86_engine.c': '3542a4b2e9a326988dd562d2823b6023dec7b27637e0fe360a821f0f3e0418a5',
}


def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def link(s, t):
    t.parent.mkdir(parents=True, exist_ok=True)
    t.symlink_to(s.resolve(), target_is_directory=s.is_dir())


def copy(s, t):
    t.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(s, t)


def replace_once(text, old, new, what):
    assert text.count(old) == 1, (what, text.count(old))
    return text.replace(old, new)


def main():
    variant = sys.argv[1]
    assert variant in ('control', 'r4', 'base')
    D = B / ('blur-launcher-r4-control' if variant == 'control' else 'blur-launcher-r4')
    if len(sys.argv) > 2:                       # scratch tree: a plain folder name under B
        assert '/' not in sys.argv[2] and sys.argv[2] not in ('', '.', '..', D.name), sys.argv[2]
        D = B / sys.argv[2]
    assert variant != 'base' or len(sys.argv) > 2, 'base needs a scratch NAME'
    assert not D.exists(), D
    assert sha(OLD / 'prospero/native/wine64_main.c') == OLD_MAIN_SHA
    D.mkdir()
    (D / 'tmp').mkdir()
    for name in ['prospero/src', 'prospero/native', 'orbis/src/native']:
        src, dst = OLD / name, D / name
        dst.mkdir(parents=True, exist_ok=True)
        for f in src.iterdir():
            if f.is_dir():
                link(f, dst / f.name)
            else:
                copy(f, dst / f.name)
    for parent, exclude in [('prospero', {'src', 'native'}), ('orbis', {'src', 'scripts', 'build'}),
                            ('orbis/src', {'native'})]:
        for f in (OLD / parent).iterdir():
            if f.name not in exclude:
                link(f, D / parent / f.name)
    link(OLD / 'orbis/build/ps4-wine-sdk', D / 'orbis/build/ps4-wine-sdk')
    shutil.copytree(OLD / 'orbis/scripts', D / 'orbis/scripts', symlinks=True)
    shell = (OLD / 'build-launcher.sh').read_text()
    shell = replace_once(shell, 'out=$base/native298-package277\n', 'out=$base/%s\n' % D.name, 'out')
    if variant in ('r4', 'base'):
        shell = replace_once(shell, 'export TITLE_ID=GLDS10061 TITLE=Blur PW_BUILD_ID=GLDS10061-native298 PKG_VERSION=01.08\n',
                             'export TITLE_ID=%s TITLE=JkFlow PW_BUILD_ID=%s PKG_VERSION=01.00\n' % (TITLE_ID, BUILD_ID), 'ids')
        assert (SEED_DIR / 'seed245/blur-native248/seed.idx').read_bytes()[-65:-1].decode() == SEED_END
        lines = [l for l in shell.splitlines(keepends=True) if l.startswith('export N245_SEED_DIR=')]
        assert len(lines) == 1, lines
        shell = shell.replace(lines[0], 'export N245_SEED_DIR="%s" N245_SEED_END=%s\n'
                              'export PW_PELIB="%s" PW_WOW_CPU_DLL=wowblur.dll\n' % (SEED_DIR, SEED_END, PELIB))
        app = D / 'orbis/scripts/build-app-ps4.sh'
        assert not app.is_symlink()
        text = app.read_text()
        text = replace_once(text, 'CFL="-std=gnu11 -O2 -fPIC -DPW_BUILD_ID=\\"${PW_BUILD_ID:-unknown-local}\\" ',
                            'CFL="-std=gnu11 -O2 -fPIC -DPW_BUILD_ID=\\"${PW_BUILD_ID:-unknown-local}\\" -DPW_TITLE_ID=\\"${TITLE_ID}\\" ',
                            'CFL')
        app.write_text(text)
        reloc = D / 'orbis/scripts/build-relocatable-wine-ps4.sh'
        assert not reloc.is_symlink()
        text = reloc.read_text()
        text = replace_once(text, '"$CC" -std=gnu11 -O2 -fPIC -DPW_RELOCATABLE_WINE -DPW_BUILD_ID="\\"$PW_BUILD_ID\\"" \\\n',
                            '"$CC" -std=gnu11 -O2 -fPIC -DPW_RELOCATABLE_WINE -DPW_BUILD_ID="\\"$PW_BUILD_ID\\"" -DPW_TITLE_ID="\\"$TITLE_ID\\"" \\\n',
                            'reloc CFLAGS')
        reloc.write_text(text)
        gen = LAUNCHER / 'build/gen'
        for f in sorted((LAUNCHER / 'src').glob('*.[ch]')) + [gen / 'bl_font_data.c', gen / 'bl_font.h', gen / 'bl_logo.c']:
            assert not (D / 'prospero/src' / f.name).exists(), f.name
            copy(f, D / 'prospero/src' / f.name)
        main_c = D / 'prospero/native/wine64_main.c'
        for p in [HERE / 'r1/title-r1.patch', HERE / 'prod-audit/title-prod.patch']:
            subprocess.run(['patch', '-p1', '--no-backup-if-mismatch', '-i', str(p)], cwd=main_c.parent, check=True)
            if p.name == 'title-r1.patch':
                assert sha(main_c) == R1_MAIN_SHA, sha(main_c)
        assert sha(main_c) == R2_MAIN_SHA, sha(main_c)
        if variant == 'base':                   # the base/ side of title-blur100.patch
            (D / 'build-launcher.sh').write_text(shell)
            print(D)
            return
        subprocess.run(['patch', '-p1', '--no-backup-if-mismatch', '-i', str(HERE / 'title-blur100.patch')],
                       cwd=D, check=True)
        for rel, want in R4_SHAS.items():
            assert not (D / rel).is_symlink(), rel
            assert sha(D / rel) == want, (rel, sha(D / rel))
        # Audit findings 6+10: every build ends with the post-link clean of the
        # title prx (postlink_clean.py, copied into the tree so the tree builds
        # on its own); a failed control or proof fails the build.
        copy(HERE / 'postlink_clean.py', D / 'postlink_clean.py')
        build_line = 'bash "$out/orbis/scripts/build-relocatable-wine-ps4.sh"\n'
        shell = replace_once(shell, build_line, build_line + 'python3 -I "$out/postlink_clean.py" run "$out"\n',
                             'post-link clean')
    (D / 'build-launcher.sh').write_text(shell)
    print(D)


if __name__ == '__main__':
    main()

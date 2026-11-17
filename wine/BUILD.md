# How the JkFlow 1.00 Wine PS4 runtime was built

The runtime was not built in one pass from one tree. It is the order-284
runtime (one full cross-build of the patched Wine tree, 2026-11-07) plus a
series of later stages, each of which recompiled one or a few source files
and relinked the module from the retained objects of the stage before it.
The last stage of every module ("jkflow100") changed only names and
developer paths. Every stage script checks, before it changes anything, that
its unchanged inputs reproduce the previous shipped module byte for byte
(its "control"); the JkFlow 1.00 stage receipts record that these controls
passed for every module below.

The patches in `patches/` describe the source of all stages as one tree per
upstream repository (`patches/series.txt`). The scripts in `build/` are the
stage scripts as they were run; build-host paths are replaced by
`$JKFLOW_BUILD_ROOT` (the build root that held all trees), `$JKFLOW_PROSPERO_WIN`
(the patched prospero-win tree) and `$JKFLOW_PRIVATE_REPO` (the private
project repository, whose research notes are not published), and the internal
folder of the native288 recipes in that repository is called
`native288-recipes` here.
They read the retained objects and pinned compile-command records of the
earlier stages, so they document the build; they do not rebuild it from an
empty folder.

## Developer code in this source

The patches are the source of what ships, so they still contain developer
and diagnostic code: profiling counters, environment switches (`PW_*`,
`WINE_PS5_*`), and switch files and log fallbacks under `/data/blurdev/`
(module override folder in the ntdll loader, `pw_sched` in wineserver, the
win32u performance switch, the winevulkan capture file, the WoW64 profiling
flags). The shipped binaries cannot reach those files: the post-link step
paths7 below rewrites every `/data/blurdev/` path in the shipped modules to
`/dev/null/bld/` (and the env files to `/dev/null/...`, paths6b), so every
open of them fails as if the file were absent. The environment switches are
read only from the process environment, which the title sets.

## Toolchain

See `UPSTREAM.md` for versions. In short: orbis-win `scripts/fetch-sdk.sh`
(SDK bundle `orbis-sdk-v1`), `scripts/prepare-ps4-toolchain.sh` and
`scripts/sync-toolchain.sh` (the `prospero-clang` wrapper, `libwineshim.a`,
`libps4stubs.a`, `module_param.o` in `build/ps4-wine-sdk`); Ubuntu clang 18
and LLVM 18 tools for the PS4 side (`native248-build-wine/tools` holds
`llvm-ar`, `llvm-nm`, `llvm-objcopy`, `llvm-readelf`, `llvm-size` links to
the `-18` tools); `gcc-mingw-w64` 13 (posix) for the PE side, called through
two wrapper scripts (`private-bin/{i686,x86_64}-w64-mingw32-gcc`) that run
the unpacked Ubuntu packages under `deps/rootfs`.

## Stages

1. **Order-284 runtime** (`build/native248-runtime/`). `native248-build-wine.sh`
   runs `driver-ps4.sh` (prospero-win `tools/build_wine_ps5.sh` with its root
   set to the patched prospero-win tree): it copies Wine 490f6d5, applies
   the prospero-win series, configures Wine for `x86_64-unknown-freebsd11`
   with `prospero-clang` and builds the Unix side. `native248-repair-platform.sh`
   and `native248-resume-wine.sh` recompiled the objects invalidated by the
   platform-header repair and finished the build; `native248-build-prx.sh`
   links the PS4 modules (ntdll, win32u, wineserver, winevulkan, ws2_32,
   opengl32, crypt32, dwrite, wineps5, xinput1_3, libfreetype, wowprospero,
   wow64native) with the orbis-win PRX scripts and `create-fself`;
   `native248-build-pe.sh` and `native248-build-nativecpu.sh` build the PE
   modules that the patches change; `native248-stage-pe.py` strips their
   debug data (`llvm-strip --strip-debug`). The source tree of this build is
   Wine + prospero-win series + `patches/wine/0001` (no file of that tree was
   modified after the build).
2. **Later stages** (`build/stages/`, `build/native288-recipes/`), in the order
   of `patches/wine/0002`-`0014`:
   - ntdll: native264 (`native264-assemble.py`, `-compile.py`, `-link.py`,
     `-runtime-build.py`: the event/alert backend and libkernel qualification
     files), native265 (`native265-shared-*`), native266 (`native266-link-ntdll.py`),
     cs-sampler (`native285-cs-sampler-*`, `native286-cs-sampler-*`: signal_x86_64.c,
     thread.c), standard handles (`native288-standard-handles-*`: env.c),
     spin260 / override260 (`spin260build.py`, `override260build.py`:
     server.c, sync.c, the loader `pw_wine_dl.c`), clockfix260 (below).
   - wineserver: native266 (`native266-wineserver-build.py`), serverprio260
     (`serverprio260.py`: fd.c), clockfix260.
   - win32u: native250 (`native250-performance-build.py`: the performance
     switch loader object), native274 (`native274-generate.py`,
     `native274-win32u-build.py`: vulkan.c).
   - ws2_32: native250 resolver (`native250-resolver-build.sh`: pw_ws2_32_libc.c).
   - winevulkan: native269 (`native269-integration-build.py`: x86_64
     `winevulkan.dll`), native270 (`native270-decode-*`: the codec generator
     and decoder), async/asynccb/parwork260 (`asyncbuild260.py`,
     `asynccb260build.py`, `parwork260build.py` at the version that built the
     shipped module), direct260 (`direct260build.py`: i386 `winevulkan.dll`).
   - dsound (i386 `dsound.dll`): `native286-dsound259-build.py`, `-r3-build.py`.
3. **JkFlow 1.00** (`build/jkflow100/`): `build_blur100_{ntdll,wineserver,
   win32u,ws2_32,winevulkan,wow,seed}.py`. Names (`prospero-win` -> `BLUR`,
   `wowprospero` -> `wowblur`) and developer paths (`/data/prospero-win/...`
   -> `/data/blurdev/...`) only; each records its controls and that the
   dynamic symbol tables did not change.

### clockfix260 (binary edit, ntdll.prx and wineserver.prx)

`build/native288-recipes/clockfix260.py`: liborbis-compat's clock helper
(SDK bundle, MIT) returns the time since its own first call, so ntdll and
wineserver disagreed by tens of milliseconds. In the linked ELF of both
modules the 7-byte `sub start(%rip),%rax` (`48 2b 05 disp32`) of that helper
becomes the 7-byte `nopl disp32(%rax)` (`0f 1f 80 disp32`); layout, imports
and exports do not change. This is a binary edit of SDK object code, not a
Wine source change.

## Post-link steps

All of them replace bytes in place with an equal-length string and change
nothing else; a prx is then converted again with the build's own
`create-fself-repro.sh ... -ptype fake`, which changes only the 32-byte SELF
digest of the new `.oelf`.

| Step | Tool | What | Applied to |
|---|---|---|---|
| paths6 | `tools/paths_clean.py paths6 elf\|pe` | build-host directory prefixes (from a private path map, `POSTLINK_PATH_MAP`) and `/home/runner/` -> `/build/ci-rt/`; PE CheckSum recomputed when it was set | 6 prx link ELFs (libvulkan, ntdll, opengl32, win32u, wineserver, ws2_32), the 520 PE files listed with post-link `paths6` in `runtime-files.tsv`, the seed registry text |
| paths6b | `tools/paths_clean.py paths6b elf` | developer env files (`/data/orbis-env.txt` and three more) and `/data/blurdev/{radv.env,dev.conf}` -> `/dev/null/...` | the link ELF of every prx in `sce_module/` except the title and dxvknative |
| paths7 | `title/postlink_devpaths.py` (in the title folder of this repository) | every `/data/blurdev/` path -> `/dev/null/bld/`, `/data/orbis-watchdog.txt` -> `/dev/null/orbis-wdog.txt`, with a code-reference check per string | ntdll, wineserver, win32u, ws2_32, winevulkan, wow64native, wowblur, libvulkan |
| postlink_clean | `title/postlink_clean.py` | the same path clean for the title prx | `relocated_wine_app.prx` |

The original paths6/paths6b tool was a one-off and was not kept;
`tools/paths_clean.py` re-implements it from its receipts. Checked against
the receipts on the build host: paths6 reproduces all 520 PE files and all 6
ELFs byte for byte; paths6b reproduces 13 of the 14 module ELFs (the Mesa
`libvulkan` ELF differs; see the gaps below).

## Modules

SHA-256 as shipped in the package (`sce_module/` and `win/wine/lib/wine/`).
"Source" names the patches whose result was compiled into the module, on top
of the prospero-win series and `wine/0001` (W = `patches/wine/`, P =
`patches/prospero-win/`, O = `patches/orbis-win/`). Every prx also links
prospero-win `wine/ps5` shims and orbis-win `src/native` helpers, and the
SDK bundle's liborbis-compat and stubs.

| Module (package path) | SHA-256 shipped | Source (patches) | Build scripts | Checks | Status |
|---|---|---|---|---|---|
| `sce_module/ntdll.prx` | `1b06ca727c73317f29719e869185e29ffbc830773d8fa1ad625f068db934b65b` | W0001-W0005; P0001, P0003 (`pw_wine_dl.c`), P0004 (`pw_wine_compat.c`) | native248 runtime, native264-266, cs-sampler, standard-handles, spin260, override260, clockfix260, `build_blur100_ntdll.py` | controls; DWARF 5842/5842; source pins 2/2; frozen; paths6 + paths6b reproduced | partial: every debug-built unit matches; the prospero-win shim objects (no debug data) only by source pin; not rebuilt |
| `sce_module/wineserver.prx` | `e1e793dc843e60ec42cd58d1eef16c12e335865c5571532a2765edd4824a684d` | W0001, W0006, W0007; P0004 | native266, serverprio260, clockfix260, `build_blur100_wineserver.py` | controls; DWARF 6779/6779; source pin 1/1; paths6 + paths6b reproduced | partial (as ntdll) |
| `sce_module/win32u.prx` | `1efd3932f9c6e1b2ac9c1dc39c38c7af043070bb48fc0df6b082050d0a6f8899` | W0001, W0008, W0009 | native248 runtime, native250, native274, `build_blur100_win32u.py` | controls; DWARF 13237/13237; source pin 1/1; frozen; paths6 + paths6b reproduced | partial (as ntdll) |
| `sce_module/ws2_32.prx` | `35b8e2e89535dc8fb7cedbaac701c618189856f21505176d220aa61245e3e66d` | W0001; P0001, P0004 (`pw_ws2_32_libc.c`); O0002 | native248 runtime, `native250-resolver-build.sh`, `build_blur100_ws2_32.py` | controls; DWARF 138/138; source pins 5/5; paths6 + paths6b reproduced | partial (as ntdll) |
| `sce_module/winevulkan.prx` | `7654dd62eb74ade6bb95d557380dccee238686666b0036c70ee6dd9367048ab5` | W0001, W0012; P0002 (generated `pw_vk_generated.c` `ff37856c...`) | native248 runtime, native270, asyncbuild/asynccb/parwork260, `build_blur100_winevulkan.py` | controls; DWARF 15027/15027; paths6b reproduced | partial (as ntdll) |
| `sce_module/wow64native.prx` | `f2704f07ec7dd0fe8cdc62c4491741dbefb327d6b24f98558f6fdc1ef3a648d7` | P0001 (`wine/wow64native/unix.c`, `src/`); O0002 | native248 runtime, `build_blur100_wow.py` | controls; source pins (17 for the WoW64 build); paths6b reproduced | partial: no debug data, source tied by pins only |
| `sce_module/wowblur.prx` | `6ba57d93dffa26c6177bc3ba7380d8ea6671937f302a56457d9adef047e0b6e6` | P0001 (`src/pw_x86_*`), P0005; O0002 | native248 runtime, `build_blur100_wow.py` | controls; source pins; paths6b reproduced | partial (as wow64native) |
| `win/wine/lib/wine/x86_64-windows/wow64.dll` | `8540553a075b1b083b2a0e4e4502b4b865550a05ac069a21be52128e4c61e478` | W0001, W0014 | `build_blur100_wow.py` (strip-debug of `wow64.build.dll`) | controls; DWARF 2943/2943 (unstripped build) | partial: source tied by DWARF; not rebuilt |
| `win/wine/lib/wine/x86_64-windows/wowblur.dll` | `4f4cb082673e9e5c44c7f48f5cc212c84781bcf5dc227153f25bff42545f497b` | P0001 (`wine/wowprospero/cpu.c`), P0005 (`wowblur.spec`) | `build_blur100_wow.py` | controls; DWARF 176/176 | partial (as wow64.dll) |
| `win/wine/lib/wine/i386-windows/winevulkan.dll` | `7ba220027012496c381f2193048acf62b0ddc9cb160f3e96e0a5ea77f79d46d5` | W0001, W0011 (incl. `direct260_unixshim.c`) | `direct260build.py` | DWARF 8747/8747 on the unstripped DLL; `llvm-strip --strip-debug` of it gives the shipped bytes | partial: everything after compilation reproduced |
| `win/wine/lib/wine/x86_64-windows/winevulkan.dll` | `874106f3c9cd01b7fcd683f3b598f2343ffb118f81c923ca7e972a07856b9ec8` | W0001, W0010 | `native269-integration-build.py` | DWARF 8429/8429; strip-debug gives the shipped bytes | partial (as i386) |
| `win/wine/lib/wine/i386-windows/dsound.dll` | `8f1199a6a33bbf2072f914712972eb751945d49299a11cd64bb884a1007123e2` | W0001, W0013 | `native286-dsound259-build.py`, `-r3-build.py` | DWARF 4221/4221; strip-debug gives the as-built `2b4ddf87...`; paths6 reproduced from it | partial (as i386 winevulkan) |
| `sce_module/opengl32.prx`, `crypt32.prx`, `dwrite.prx` | `834fd994555104b2fd20b09c27f4e26effbca020fd2fbdcc7917dae4422dbf56`, `0cf12294294980650622d04d53d077977420e2b41aa5655a0a109dd61be05a3e`, `70b0f8b53e03bfaab14a05fbcd0be4a6034736231713eb4ce4de7badfa90af0a` | W0001 tree (order-284 runtime) | native248 runtime | DWARF 22397/22397, 103/103, 149/149; frozen; paths6 (opengl32) + paths6b reproduced | partial (as ntdll) |
| `sce_module/wineps5.prx`, `xinput1_3.prx` | `77ed4f8c547adc42b8940c0bf77d6791736214ea3ba4d65e4e79c36db0e698dd`, `febeacc05842f3e73bb963b85b62eafafa9e7a55503e4758a240661628bc5355` | W0001 tree, P0001 (`wine/ps5`) | native248 runtime | frozen; paths6b reproduced | partial: no debug data; source by build recipe and file times only |
| `sce_module/libfreetype.prx` | `4a7f64abfc1448ec803ea7e834905812b3e473ac4056614f9728e7485b55d5c5` | FreeType 2.13.3, unchanged (FTL) | orbis-win `scripts/freetype-prx-ps4.sh` | paths6b reproduced | upstream, unchanged |
| `sce_module/libvulkan.prx` | `9b00fc046864ae67e9b31865b82dd579994dc5b39950c445ad7e635b1f8ceb32` | Mesa (MIT), see `mesa/` | not in this folder | paths6b NOT reproduced by `tools/paths_clean.py` | out of scope (MIT) |
| 65 PE files "ours: order-284 PE build" | per file in `runtime-files.tsv` | W0001 tree | `native248-build-pe.sh`, `native248-stage-pe.py` | frozen (no PE source changed after the PE build); paths6 reproduced (520/520) | partial: not DWARF-checked, not rebuilt |
| 457 upstream prebuilt PE files | per file | upstream (UPSTREAM.md); our only change is paths6 | none | paths6 reproduced | upstream commit not recorded (gap 2) |
| 1233 other `win/wine` files (incl. the 76 `x86_64-unix/nls` copies) | per file | upstream, unchanged | none | hashes listed | upstream, origin not recorded (gap 2) |
| `seed245/blur-native248/` (Wine prefix; `seed.idx` `2c5f2df3426a6de98df89f1c08a251d9b20cd68ee4c5240961ff6d4082b34dab`, END `ed7b2bdb557d3d3e49c2ca72b027fc28cdb6ec7d9d689d49204c5d23e93925e7`) | as listed | made by Wine from the runtime above + `build_blur100_seed.py` | `build_blur100_seed.py`, paths6 registry clean | the build's own control (its receipt) | not verified here |

The shipped prx files are the paths7 outputs of ntdll, wineserver, win32u,
ws2_32, winevulkan, wow64native, wowblur and libvulkan, and the paths6b
outputs of the others.

Check levels used in the table:
- **controls**: the stage scripts' own controls (unchanged inputs reproduce
  the previous shipped module byte for byte; the changed objects differ only
  where the source change is).
- **DWARF**: for each function and variable that the module's (unstripped)
  link ELF or DLL records with a declaring file and line, the published
  source has that name on that line. This ties the published files to the
  compiled ones; it is not a rebuild.
- **frozen**: no source file of the tree was modified after the build that
  used it (file times).
- **source pin**: the build receipt pins the source file by SHA-256 and the
  published file has that hash.
- **post-link reproduced**: `tools/paths_clean.py` / `postlink_devpaths.py`
  give the shipped bytes from the link output.
No module was rebuilt from these patches in a clean tree for this release.

## Checks run for this folder

Run on the build host on 2026-11-16:

1. **Series reproduces the build trees.** Fresh archives of the three upstream
   commits, the patches applied in `series.txt` order: prospero-win and
   orbis-win equal the assembled trees file for file; Wine + prospero-win
   series + `wine/0001` + the three generated files equals the order-284
   build tree (`diff -r`, no difference); with `wine/0002`-`0014` it equals
   the assembled final tree.
2. **Frozen trees.** No file of the Wine tree, the prospero-win tree or the
   orbis-win tree was modified after the order-284 build finished, except
   `wine/ps5/pw_ws2_32_libc.c` (rebuilt by the native250 resolver stage, whose
   receipt pins exactly the published file) and title-only files.
3. **Source pins.** The 26 Wine-side source files pinned by SHA-256 in the
   JkFlow 1.00 stage receipts (and 6 Mesa ones) all still have the pinned
   hash; the patches were made from these files.
4. **DWARF declarations.** 0 mismatches in 88,188 checked declarations over
   13 modules (8 prx link ELFs, 5 unstripped DLLs); declarations named by a
   macro on their line (debug channels, `DECL_HANDLER`) are skipped.
   Negative control: the same check of ntdll against the sources *before*
   the stage patches finds 127 mismatches, so the check does see a wrong file.
5. **Post-link.** `tools/paths_clean.py`: paths6 520/520 PE files and 6/6 ELFs,
   paths6b 13/14 ELFs (all Wine modules) byte for byte as in the receipts.
6. **Strip.** `llvm-strip-18 --strip-debug` of the unstripped winevulkan.dll
   (i386, x86_64) and dsound.dll gives the shipped / as-built bytes.

No module was compiled again for this folder.

## Title build entry

The title (`eboot.bin`, `relocated_wine_app.prx`) is described in `title/`.
`title/prepare_blur100.py r4` starts from the base title tree
`native298-package277`: its four source folders (`prospero/native`,
`prospero/src`, `orbis/scripts`, `orbis/src/native`) are upstream prospero-win
`108194e` and orbis-win `9749fcd` with `patches/title-base/0001-title-base.patch`
(`patches/series.txt`, part 4; checked with `diff -r`, one unpublished file
named there); its other folders are links to the trees of parts 1-2. The
base tree's own build entry is `build/title/build-launcher-base.sh.example`.
The title's build entry `build-launcher.sh` is written into the prepared
title tree by `title/prepare_blur100.py r4`;
`build/title/build-launcher.sh.example` is that script with the build-host
paths replaced. Inputs:

| Variable | Where it comes from |
|---|---|
| `JKFLOW_BUILD_ROOT` | the build root holding all trees below |
| `PATH` += `native248-build-wine/tools`, `private-bin`, `deps/rootfs/usr/bin` | LLVM 18 tool links, the mingw wrapper scripts, the unpacked Ubuntu build packages (see Toolchain) |
| `MESA_BUILD=native248-platform` | the SDK bundle's orbis-compat headers (prospero-clang adds `-isystem $MESA_BUILD/orbis-compat/include`) |
| `PW_ROOT=blur-launcher-r4/prospero` | the title's prospero-win tree, prepared by `prepare_blur100.py` (title sources patched by `title/*.patch`) |
| `PW_PS4_ROOT=orbis-win-9749fcd` | orbis-win after `patches/orbis-win/` (its `build/ps4-wine-sdk` holds `prospero-clang` and `libwineshim.a`, made by `scripts/prepare-ps4-toolchain.sh`) |
| `PW_MODULE_STUBS_DIR=native245-out/pkg-stage/sce_module` | an earlier runtime package's `sce_module/` prx files, used only as link stubs (their export tables) |
| `N245_SEED_DIR=blur100-paths6/seed-build/seed`, `N245_SEED_END` | the Wine prefix seed after paths6 (`build/jkflow100/build_blur100_seed.py`, then the paths6 registry clean); the title embeds its END hash |
| `PW_PELIB=blur100-paths6/seed-build/runtime` | the PE runtime the seed pins (the paths6 PE files) |
| `PW_WOW_CPU_DLL=wowblur.dll` | the WoW64 CPU backend name |
| `TITLE_ID`, `TITLE`, `PW_BUILD_ID`, `PKG_VERSION` | fixed product values |
| `POSTLINK_PATH_MAP` | private path map for `postlink_clean.py run` (`title/postlink-path-map.example`) |

## Gaps

1. **No clean rebuild.** The stage scripts relink retained objects and read
   compile-command records of earlier stages that are not published (they
   live in the private research tree and hold build-host paths). A recipient
   builds from the patched trees with Wine's own Makefile, prospero-win
   `tools/build_wine_ps5.sh` and the orbis-win PRX scripts; byte-identical
   output is not claimed.
2. **Upstream prebuilt PE files.** The 457 prebuilt PE files come from the
   upstream maintainer's ABI-v4 runtime build (2026-11-04); its exact
   prospero-win/Wine commit is not recorded. The 1233 other unchanged
   `win/wine` files (1157 plus the same 76 NLS tables again in
   `x86_64-unix/nls/`) have no recorded origin either. Both are listed by
   path and hash in `runtime-files.tsv`.
3. **Modules without debug data** (wineps5, xinput1_3, wow64native, wowblur,
   and the prospero-win shim objects inside every prx) are tied to the
   published source only by source pins and file times.
4. **clockfix260** is a binary edit of SDK (orbis-compat) object code in ntdll
   and wineserver; the edit and its script are published, there is no source
   form.
5. **paths6b of libvulkan** (Mesa) is not reproduced by `tools/paths_clean.py`;
   Mesa is MIT and outside this folder. paths7 was not re-run here; its tool
   is `title/postlink_devpaths.py`.
6. **Seed.** The prefix seed was not rebuilt or checked here.
7. **Build scripts** are copies with build-host paths and internal tool names
   replaced; they are not byte-identical to what ran. Like everything in this
   repository they are LGPL-2.1-or-later unless a file says otherwise.
8. **Developer code** stays in the published source (see above).
9. **Title base.** One file of the base title tree, the Blur logo bitmap
   `prospero/src/pw_blur_logo.h` (Activision artwork, not ours to license),
   is not published; `patches/series.txt` part 4 names it with its hash.

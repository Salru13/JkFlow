# Third-party components

This title runs Blur on the PS4 through the components below. Full licence
texts are in `LICENSES/`. Where to get the source for each component, with our
changes, is listed in `SOURCES.txt`. The JkFlow launcher is LGPL-2.1-or-later
(Copyright (c) 2026 XxXJoker13XxX and contributors); the V7 and S7 DLLs are
separate files, Copyright (c) 2026 XxXJoker13XxX, all rights reserved. Our
native DXVK (`dxvknative.prx` and `d3d9.c988.dll`) is free to use with credit
("Native DXVK by XxXJoker13XxX"). All are described in `LICENSE`.

Blur itself is not part of this package. The player supplies their own copy.

JkFlow contains no files from Activision or AMAX. Patch for online play downloads blur_12.zip and amax_client_files.zip from amax-emu.com straight to your console. These files belong to their owners, and their terms apply.

Every copyright line below is copied from the component's own source or
licence file at the version in this package; the file in the last column
gives the full notice.

| Component | What it does here | Licence | Copyright | Text |
|---|---|---|---|---|
| Wine 11.17 (commit 490f6d5dcb), including its fonts | Runs Blur's Windows code (`win/wine/`) | LGPL-2.1-or-later | Copyright (c) 1993-2026 the Wine project authors (see `LICENSES/Wine-AUTHORS.txt`) | `LICENSES/Wine-LICENSE.txt`, `LICENSES/LGPL-2.1.txt`, `LICENSES/Wine-AUTHORS.txt` |
| Code Wine imports: Capstone, LLVM libc++/libc++abi/libunwind/compiler-rt, FAudio, FFmpeg 8.1.1 (subset), FluidSynth, GSM 06.10, ICU, libjpeg, JPEG XR, Little CMS 2, OpenLDAP, mpg123, musl 1.2.3, libpng, SQLite 3.53.2, SymCrypt, LibTIFF, vkd3d, libxml2, libxslt, zlib 1.3.2, Bitstream Vera Sans (Wine's Tahoma), Unicode data and the other imports listed in Wine's NOTICES.md | Built into the Wine modules in `win/wine/lib/wine/` | Each its own: BSD, MIT, zlib, ISC-style, Unicode, OpenLDAP, LGPL-2.1-or-later, Apache-2.0 WITH LLVM-exception, public domain | Each project's own lines, for example (C) 1995-2026 Jean-loup Gailly and Mark Adler (zlib), Copyright (c) 1995-2026 The PNG Reference Library Authors (libpng), Copyright (c) 2011-2024 Ethan Lee, Luigi Auriemma, and the MonoGame Team (FAudio) | `LICENSES/Wine-bundled-libraries.txt`, `LICENSES/Wine-NOTICES.md`, `LICENSES/LGPL-2.1.txt` |
| Wine PS4 runtime (based on the Wine console ports named in `SOURCES.txt`) | Wine on the console: loader, system calls, video and pad (`sce_module/*.prx`) | LGPL-2.1-or-later | the authors of Wine and of the console ports; each source file names its holders | `LICENSES/LGPL-2.1.txt` |
| DXVK 1.10.3, the base of our native renderer (`dxvknative.prx`) and its client (`d3d9.c988.dll`) | Turns Direct3D 9 into Vulkan | zlib/libpng | Copyright (c) 2017-2021 Philip Rebohle, Copyright (c) 2019-2021 Joshua Ashton | `LICENSES/DXVK-zlib.txt` |
| Code inside DXVK 1.10.3: OpenVR header, SPIR-V headers, Vulkan headers, SHA-1 | Part of `dxvknative.prx` and `d3d9.c988.dll` | BSD-3-Clause (OpenVR), MIT-style Khronos (SPIR-V), Apache-2.0 (Vulkan headers), public domain (SHA-1) | Copyright (c) 2015, Valve Corporation; Copyright (c) 2014-2019 The Khronos Group Inc.; Copyright 2015-2022 The Khronos Group Inc.; SHA-1 by Steve Reid, 100% Public Domain | `LICENSES/DXVK-bundled-notices.txt`, `LICENSES/Apache-2.0.txt` |
| mingw-w64 11.0.1 runtime (CRT, winpthreads) | Windows C runtime linked into `d3d9.c988.dll` | ZPL-2.1 and the other terms listed in its notice | Copyright 2009-2016 the mingw-w64 project, and the other holders listed in its notice | `LICENSES/mingw-w64-runtime.txt` |
| Mesa / RADV 26.3.0-devel | The Vulkan driver for the console's GPU (`libvulkan.prx`) | MIT (individual files may differ; see their SPDX lines) | Advanced Micro Devices, Valve Corporation, Intel Corporation, Red Hat, Collabora, Google, Igalia and the other holders, each line as written in `LICENSES/Mesa-MIT.txt` | `LICENSES/Mesa-MIT.txt` |
| Code inside the Vulkan driver: xxHash, Berkeley SoftFloat 3e, Fuchsia radix sort, C11 threads emulation, zlib 1.3.1, BLAKE3 1.8.2, Khronos and Android headers | Part of `libvulkan.prx` | BSD-2-Clause, BSD-3-Clause, BSD-style, BSL-1.0, Zlib, CC0-1.0 (BLAKE3), Apache-2.0, SGI-B-2.0 (header only) | Copyright (C) 2012-2023 Yann Collet; Copyright 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018 The Regents of the University of California; Copyright 2019 The Fuchsia Authors; Copyright yohhoy 2012; Copyright (C) 1995-2024 Jean-loup Gailly and Mark Adler; Copyright 2015 The Android Open Source Project; Copyright 2017-2020 The Khronos Group Inc. | `LICENSES/Mesa-other-licenses.txt`, `LICENSES/Apache-2.0.txt` |
| Nunito 3.602 | Launcher font | SIL Open Font License 1.1 | Copyright 2014 The Nunito Project Authors (https://github.com/googlefonts/nunito) | `LICENSES/Nunito-OFL-1.1.txt` |
| FreeType 2.13.3 | Font rendering inside Wine (`libfreetype.prx`) | FreeType License (FTL) | Copyright (C) 1996-2024 by David Turner, Robert Wilhelm, and Werner Lemberg. Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved. | `LICENSES/FreeType-FTL.txt`, `LICENSES/FreeType-notices.txt` |
| LLVM runtime 11.0.0 (libc++, libc++abi, libunwind, compiler-rt builtins) | C++ runtime of the native modules | Apache-2.0 WITH LLVM-exception | Copyright (c) 2009-2019 by the contributors listed in CREDITS.TXT | `LICENSES/LLVM-Apache-2.0-with-LLVM-exception.txt` |
| musl libc and crt1 (OpenOrbis musl, commit 2b0bb8ce) | C runtime of eboot.bin and the native modules | MIT | Copyright © 2005-2020 Rich Felker, et al. | `LICENSES/musl-MIT.txt` |
| orbis-compat (commit 31407841) | PS4 compatibility layer and module entry of the native modules; the PS4 DRM backend of the Vulkan driver | MIT (two named files differ, see the licence file) | Copyright (c) 2026 Mikołaj Mikołajczyk | `LICENSES/orbis-compat-MIT.txt` |
| puff 2.3 (zlib contrib) | The launcher's DEFLATE decoder follows its design | Zlib | Copyright (C) 2002-2013 Mark Adler | `LICENSES/zlib-puff.txt` |
| AMAX online client: downloaded by the player from amax-emu.com, not included | Online play through the AMAX community servers | the AMAX team's terms (amax-emu.com) | the AMAX team | not part of this package |

## Thanks

Thanks to plastikman for orbis-win and his PS4 Wine port (github.com/plastikman). JkFlow uses modified parts of them, under their licence (LGPL-2.1-or-later), and was inspired by their approach. Thanks also to mpereiraesaa for the original Wine console port (github.com/mpereiraesaa).

## Our own components

| File | What it does | Licence |
|---|---|---|
| the JkFlow launcher (in `sce_module/relocated_wine_app.prx`) | Start screen, Patch, settings, help, loader | LGPL-2.1-or-later (`LICENSE`, `LICENSES/LGPL-2.1.txt`); source: `SOURCES.txt` |
| `blur-files/amax-dxvk/dxvk.dll`, installed as `amax/dxvk.dll` (V7 adaptive pacer, SHA-256 `55ba8a0d64012ae44de2247c3cd07a51d3bec5a7c62fd6a74a8cd0776da63908`) | Loaded by the AMAX client as its renderer; frame pacing; loads S7 | All rights reserved (`LICENSE`) |
| `blur-files/amax-dxvk/d3d9_blur_dxvk.dll`, installed as `amax/d3d9_blur_dxvk.dll` (S7 shader fixes, SHA-256 `0748b79592f7e26fbd044389386ca9c9f8d330bb6298a0dbf817f3a64868fb3b`) | Six composite shader replacements; loads `d3d9.c988.dll` | All rights reserved (`LICENSE`) |
| Native DXVK: our changes in `sce_module/dxvknative.prx` | The native renderer: draws Blur with the console's Vulkan driver | Free to use with credit "Native DXVK by XxXJoker13XxX" (`LICENSES/JkFlow-Native-DXVK.txt`); the DXVK base stays zlib/libpng |
| Native DXVK: our changes in `blur-files/d3d9.c988.dll` | Colour fix and the native renderer client | Free to use with credit "Native DXVK by XxXJoker13XxX" (`LICENSES/JkFlow-Native-DXVK.txt`); the DXVK base stays zlib/libpng |

## Notes

- No GPL-3.0 code is linked into this package. Only the layout of the OpenOrbis linker script was used, which writes the string `/libexec/ld-elf.so.1` into eboot.bin.
- The LGPL source for the Wine runtime and the title with our changes: see `SOURCES.txt`. What is not yet complete is listed under "Gaps" in `wine/BUILD.md` of the source repository.

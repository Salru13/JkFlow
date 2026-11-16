# Wine PS4 runtime (JkFlow 1.00)

This folder is the corresponding source of the Wine-based PS4 runtime shipped
in JkFlow 1.00 (LGPL-2.1-or-later): the Unix-side modules in `sce_module/`
(ntdll, wineserver, win32u, ws2_32, winevulkan, the WoW64 modules and the
other Wine prx files), the Windows-side Wine DLLs in `win/wine/` and the Wine
prefix seed.

| Path | What |
|---|---|
| `UPSTREAM.md` | The exact upstream commits (Wine, the Wine console port, its PS4 port), the SDK bundle and toolchain, and the files that are listed by hash instead of copied (generated sources, prebuilt upstream PE files). |
| `patches/` | Our changes as unified diffs against those commits, one series per tree, in apply order (`patches/series.txt`). New files are included as diffs. |
| `build/` | The build scripts of every build stage of the shipped modules, as they were run (build-host paths replaced by `$JKFLOW_BUILD_ROOT`). |
| `tools/paths_clean.py` | The post-link path clean of the Wine modules (steps paths6 and paths6b). |
| `BUILD.md` | How each shipped module was built, the post-link steps, and a table of every module with its SHA-256 in the package, its source patches and how far the published source has been checked against it. |
| `runtime-files.tsv` | Every file of the package's `win/wine/` tree with its SHA-256 and origin. |

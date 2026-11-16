# JkFlow title integration

The JkFlow 1.00 title (title ID BLUR00001) is the relocatable Wine PS4 app
(`eboot.bin` + `sce_module/relocated_wine_app.prx`) with the JkFlow launcher
linked in. This folder holds the recipe that prepares the title tree, the
post-link cleans and the package-input script.

## Files

| File | What |
|---|---|
| `prepare_blur100.py` | Prepares the title tree in the build root: copies the small source folders of the base tree, links the rest, sets the title IDs, copies `launcher/src` and the baked launcher files (`launcher/build/gen`), applies `r1/title-r1.patch`, `prod-audit/title-prod.patch` and `title-blur100.patch` and checks every patched file against its SHA-256. The build script it writes ends with `postlink_clean.py run`. |
| `title-blur100.patch`, `r1/title-r1.patch`, `prod-audit/title-prod.patch` | The title source changes, applied in that order (r1, then prod-audit, then blur100). |
| `postlink_clean.py` | Post-link clean of the title prx: build-host path prefixes and developer env-file names are replaced by equal-length neutral ones, with a byte-level proof against the build's own conversion. `selftest` runs on synthetic bytes. |
| `postlink-path-map.example` | Format of the build-host path map that `postlink_clean.py run`/`scan` read from `POSTLINK_PATH_MAP`. |
| `postlink_devpaths.py` | The same method for the Wine modules: developer switch paths are made unopenable (`/dev/null/...`). |
| `make_package_inputs.sh` | Collects the pinned package inputs (title, Wine modules, seed, launcher files, licence texts) into a delta folder and a composed view, and runs `launcher/tools/scan_names.py` over both with `scan-allow.txt`. |
| `scan-allow.txt` | The approved exceptions for the name scan, each with its reason. |
| `tests/run_title_tests.sh` | Host tests of the title sources against a prepared tree. |

## Steps (Linux)

```bash
export JKFLOW_BUILD_ROOT=<build root>          # holds native298-package277 and the engine seed
bash launcher/tests/run.sh                      # bakes launcher/build/gen and runs the launcher tests
python3 -I title/prepare_blur100.py r4          # -> $JKFLOW_BUILD_ROOT/blur-launcher-r4
export POSTLINK_PATH_MAP=<path map file>        # see title/postlink-path-map.example
bash "$JKFLOW_BUILD_ROOT/blur-launcher-r4/build-launcher.sh"
bash title/tests/run_title_tests.sh "$JKFLOW_BUILD_ROOT/blur-launcher-r4"
JKFLOW_STATE_CACHE=<dxvk-native.dxvk-cache> AMAX_ZIP=<amax_client_files.zip> BLUR12_ZIP=<blur_12.zip> \
    bash title/make_package_inputs.sh
```

`make_package_inputs.sh` only hashes the two zips (to make sure no AMAX or
Activision file is shipped); it never copies them. The base title tree, the
build script `build-launcher.sh` of the base tree and the Wine runtime are not
in this folder; the Wine PS4 runtime source is described in `wine/README.md`.

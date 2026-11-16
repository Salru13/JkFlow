#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
base="${JKFLOW_BUILD_ROOT:?}"
work=$base/native248-build-wine
src=$base/native247-wine-source
orbis=$base/orbis-win-9749fcd
export MESA_BUILD=$base/native248-platform
[[ ! -e $MESA_BUILD ]] || { echo 'platform overlay already exists; inspect before resuming'; exit 1; }
mkdir -p "$MESA_BUILD/orbis-compat/include"
# Preserve the original SDK and compatibility tree; only the local signal
# header is copied and patched. Other inputs remain immutable references.
for entry in "$orbis/mesa-build"/*; do
    [[ ${entry##*/} == orbis-compat ]] || ln -s "$entry" "$MESA_BUILD/${entry##*/}"
done
for entry in "$orbis/mesa-build/orbis-compat"/*; do
    [[ ${entry##*/} == include ]] || ln -s "$entry" "$MESA_BUILD/orbis-compat/${entry##*/}"
done
for entry in "$orbis/mesa-build/orbis-compat/include"/*; do
    if [[ ${entry##*/} == signal.h ]]; then cp "$entry" "$MESA_BUILD/orbis-compat/include/signal.h"
    else ln -s "$entry" "$MESA_BUILD/orbis-compat/include/${entry##*/}"; fi
done
git -C "$MESA_BUILD" apply -p2 "$repo/research/native-wine/shared-orbis-9749fcd/src/wine-platform-patches-ps4/0002-orbis-compat-use-freebsd-signal-numbers.patch"
for patch in 1069-ntdll-log-native-libkernel-resolver-failure.patch 1070-ntdll-report-native-signal-delivery-state.patch; do
    git -C "$src" apply --check "$repo/research/native-wine/shared-prospero-108194e/wine/patches/$patch"
    git -C "$src" apply "$repo/research/native-wine/shared-prospero-108194e/wine/patches/$patch"
done
export PATH="$work/tools:$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
export M4="$base/deps/rootfs/usr/bin/m4" BISON_PKGDATADIR="$base/deps/rootfs/usr/share/bison"
export OO_PS4_TOOLCHAIN="$orbis/sdk/orbis-sdk-v1/sdk"
printf '#include <signal.h>\n#if SIG_BLOCK != 1 || SIG_UNBLOCK != 2 || SIG_SETMASK != 3 || SIGUSR1 != 30 || SIGWINCH != 28\n#error incorrect Orbis signal ABI\n#endif\n' > "$work/signal-abi.c"
"$orbis/build/ps4-wine-sdk/bin/prospero-clang" -E "$work/signal-abi.c" > "$work/signal-abi.i"
# External SDK header changes are not tracked by Wine's makedep. Invalidate
# only derived Unix objects inside this isolated build, never PE or sources.
python3 - "$work/build" "$work/platform-invalidated-objects.json" <<'PY'
from pathlib import Path
import sys,json
root=Path(sys.argv[1]).resolve()
files=[p for p in root.rglob('*.o') if not any(c.endswith('-windows') for c in p.relative_to(root).parts)]
assert files and all(p.resolve().is_relative_to(root) for p in files)
Path(sys.argv[2]).write_text(json.dumps([str(p) for p in files],indent=2))
for p in files: p.unlink()
print('Invalidated Unix objects:',len(files))
PY
cp "$work/make.log" "$work/make-before-platform-repair.log"
for target in dlls/ntdll/ntdll.so dlls/win32u/win32u.so server/wineserver dlls/winevulkan/winevulkan.so dlls/opengl32/opengl32.so dlls/ws2_32/ws2_32.so dlls/crypt32/crypt32.so dlls/dwrite/dwrite.so; do
    make -C "$work/build" -j4 LDFLAGS="-L$work/ps5lib -lunwind -Wl,--warn-unresolved-symbols" "$target" >> "$work/make.log" 2>&1
done
export PW_ROOT=$base/native248-build-bridge
bash "$orbis/scripts/prx-ps4.sh" > "$work/platform-prx.log" 2>&1
bash "$orbis/scripts/wineps5-prx-ps4.sh" >> "$work/platform-prx.log" 2>&1
bash "$orbis/scripts/freetype-prx-ps4.sh" >> "$work/platform-prx.log" 2>&1
echo 'Orbis signal ABI probe and Unix/PRX rebuild complete.'

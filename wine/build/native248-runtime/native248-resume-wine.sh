#!/usr/bin/env bash
# Resume only this isolated Wine build; preserve first-pass logs and source.
set -euo pipefail
base="${JKFLOW_BUILD_ROOT:?}"
work=$base/native248-build-wine
src=$base/native247-wine-source
[[ $(realpath "$work") == "$base/native248-build-wine" ]]
[[ -f "$work/driver.sh" && -f "$work/build/Makefile" ]]
python3 - "$work/driver.sh" "$work/driver-ps4.sh" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
old='LDFLAGS="$objects $base"'
assert s.count(old)==1
s=s.replace(old, 'LDFLAGS="$base"')
Path(sys.argv[2]).write_text(s)
PY
if [[ -f "$work/make.log" && ! -f "$work/make-first-pass.log" ]]; then
    cp "$work/make.log" "$work/make-first-pass.log"
fi
export PATH="$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
export M4="$base/deps/rootfs/usr/bin/m4"
export BISON_PKGDATADIR="$base/deps/rootfs/usr/share/bison"
export OO_PS4_TOOLCHAIN="$base/orbis-win-9749fcd/sdk/orbis-sdk-v1/sdk"
export PROSPERO_FREETYPE_TARBALL="$base/native248-build-deps/freetype-2.13.3.tar.xz"
bash "$work/driver-ps4.sh" --source "$src" --work "$work" --host-tools "$base/wine-build" --sdk "$base/orbis-win-9749fcd/build/ps4-wine-sdk" --jobs "${JOBS:-4}"

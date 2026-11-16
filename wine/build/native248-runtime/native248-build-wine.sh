#!/usr/bin/env bash
# Generate and execute an isolated derivative of the upstream build, preserving
# the source owner's already patched checkout. Invoke only after owner signals.
set -euo pipefail
src=${1:?patched source}; upstream=${2:?shared prospero root}; work=${3:?new absolute work directory}
base="${JKFLOW_BUILD_ROOT:?}"
sdk=$base/orbis-win-9749fcd/build/ps4-wine-sdk
[[ $work = /* && ! -e $work ]] || { echo 'work must be a new absolute directory' >&2; exit 1; }
[[ $(git -C "$src" rev-parse HEAD) == 490f6d5dcbb2a5047345b8af88d114bbcaad69a8 ]] || exit 1
[[ -f "$src/dlls/ntdll/pw_qpc_clock.h" && -f "$src/dlls/ntdll/pw_title_menu.h" && -f "$src/dlls/win32u/pw_key_shared.h" ]] || { echo 'source owner must stage Vulkan and shared headers first' >&2; exit 1; }
mkdir -p "$work"
python3 - "$upstream/tools/build_wine_ps5.sh" "$work/driver.sh" "$upstream" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
old='root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)'
assert s.count(old)==1
s=s.replace(old, 'root='+__import__('shlex').quote(sys.argv[3]))
needle='f"{sdk}/bin/llvm-nm"'
assert s.count(needle) == 2
s=s.replace(needle, '(shutil.which("llvm-nm-18") or shutil.which("llvm-nm") or "nm")')
# The PS4 compiler already adds libwineshim.a to these intermediate ELF links.
# Final PRXs are separately linked by prx-ps4.sh with PW_PRX_LINK=1 and the
# current source objects; do not double-link upstream heap/dmem here.
needle='LDFLAGS="$objects $base"'
assert s.count(needle) == 1
s=s.replace(needle, 'LDFLAGS="$base"')
a=s.index('tree=$work/source\n')
b=s.index('\ncflags=${CFLAGS:', a)
s=s[:a]+'tree=$source_dir\n# Source owner staged Vulkan and headers; never checkout, clean, patch or copy here.\n'+s[b:]
Path(sys.argv[2]).write_text(s)
PY
export PATH="$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
export BISON_PKGDATADIR="$base/deps/rootfs/usr/share/bison"
export M4="$base/deps/rootfs/usr/bin/m4"
export OO_PS4_TOOLCHAIN="$base/orbis-win-9749fcd/sdk/orbis-sdk-v1/sdk"
export PROSPERO_FREETYPE_TARBALL="$base/native248-build-deps/freetype-2.13.3.tar.xz"
[[ $(sha256sum "$PROSPERO_FREETYPE_TARBALL" | cut -d' ' -f1) == 0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289 ]] || exit 1
bash "$work/driver.sh" --source "$src" --work "$work" --host-tools "$base/wine-build" --sdk "$sdk" --jobs "${JOBS:-4}"

#!/usr/bin/env bash
# Run PS4 PRX linkers against an existing successful isolated Wine build.
set -euo pipefail
src=${1:?patched Wine source}; upstream=${2:?shared prospero root}; work=${3:?completed Wine work}; bridge=${4:?new scaffold directory}
base="${JKFLOW_BUILD_ROOT:?}"
orbis=$base/orbis-win-9749fcd
[[ -f $work/build/Makefile && -s $work/make.log && -d $work/prx ]] || { echo 'Wine build incomplete' >&2; exit 1; }
[[ ! -e $bridge && ! -e $work/source ]] || { echo 'bridge and work/source must not already exist' >&2; exit 1; }
mkdir -p "$bridge/.deps"
for component in "$upstream"/*; do ln -s "$component" "$bridge/${component##*/}"; done
ln -s "$work" "$bridge/.deps/wine-ps5"
ln -s "$src" "$work/source"
export PW_ROOT="$bridge"
mkdir -p "$work/tools"
for tool in llvm-nm llvm-readelf llvm-objcopy llvm-ar llvm-size; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        resolved=$(command -v "$tool-18")
        ln -s "$resolved" "$work/tools/$tool"
    fi
done
export PATH="$work/tools:$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
export BISON_PKGDATADIR="$base/deps/rootfs/usr/share/bison"
export M4="$base/deps/rootfs/usr/bin/m4"
bash "$orbis/scripts/prx-ps4.sh"
bash "$orbis/scripts/wineps5-prx-ps4.sh"
bash "$orbis/scripts/freetype-prx-ps4.sh"
echo "Inspect $work/prx/sce_module and link logs; package/runtime compatibility remains unverified."

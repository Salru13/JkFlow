#!/usr/bin/env bash
# Build the PE half of ABI-v4 from the same shared source as prx-ps4.sh.
# Upstream's preliminary Linux .so link is intentionally inapplicable here.
set -euo pipefail
source_dir=${1:?patched Wine source}
build_dir=${2:?completed Wine build}
upstream=${3:?shared prospero source}
output_dir=${4:?new native CPU output}
base="${JKFLOW_BUILD_ROOT:?}"
export PATH="$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
[[ ! -e "$output_dir" && -f "$build_dir/Makefile" ]]
[[ $(git -C "$source_dir" rev-parse HEAD) == 490f6d5dcbb2a5047345b8af88d114bbcaad69a8 ]]
grep -q '^#define PW_NATIVE_ABI_VERSION 4[[:space:]]*$' "$upstream/wine/wow64native/wow64native.h"
make -C "$build_dir" dlls/wow64/x86_64-windows/libwow64.a
mkdir -p "$output_dir/x86_64-windows"
module=$upstream/wine/wow64native
out=$(realpath "$output_dir")
x86_64-w64-mingw32-gcc -c -o "$out/x86_64-windows/cpu.o" "$module/cpu.c" \
    -I"$module" -I"$build_dir/include" -I"$source_dir/include" -I"$source_dir/include/msvcrt" \
    -D_MSVCR_VER=0 -D__WINESRC__ -D__WINE_PE_BUILD -Wall -fno-strict-aliasing \
    -mcx16 -mcmodel=small -O2 -g -fno-builtin -fshort-wchar
(cd "$build_dir" && "$base/wine-build/tools/winegcc/winegcc" -o "$out/x86_64-windows/wow64native.dll" \
    --wine-objdir . --winebuild "$base/wine-build/tools/winebuild/winebuild" --cc-cmd=x86_64-w64-mingw32-gcc -b x86_64-w64-mingw32 \
    -Wl,--wine-builtin -shared "$module/wow64native.spec" -nodefaultlibs \
    -Wl,--image-base,0x7a400000 "$out/x86_64-windows/cpu.o" \
    dlls/wow64/x86_64-windows/libwow64.a dlls/ntdll/x86_64-windows/libntdll.a \
    libs/winecrt0/x86_64-windows/libwinecrt0.a dlls/ntdll/x86_64-windows/libntdll.a \
    libs/compiler-rt/x86_64-windows/libcompiler-rt.a)
sha256sum "$module/cpu.c" "$module/unix.c" "$module/wow64native.h" "$out/x86_64-windows/wow64native.dll"
# Keep the alternative CPU provider paired too; the Blur profile selects native.
module=$upstream/wine/wowprospero
x86_64-w64-mingw32-gcc -c -o "$out/x86_64-windows/wowprospero_cpu.o" "$module/cpu.c" \
    -I"$module" -I"$build_dir/include" -I"$source_dir/include" -I"$source_dir/include/msvcrt" \
    -D_MSVCR_VER=0 -D__WINESRC__ -D__WINE_PE_BUILD -Wall -fno-strict-aliasing \
    -mcx16 -mcmodel=small -O2 -g -fno-builtin -fshort-wchar
(cd "$build_dir" && "$base/wine-build/tools/winegcc/winegcc" -o "$out/x86_64-windows/wowprospero.dll" \
    --wine-objdir . --winebuild "$base/wine-build/tools/winebuild/winebuild" --cc-cmd=x86_64-w64-mingw32-gcc -b x86_64-w64-mingw32 \
    -Wl,--wine-builtin -shared "$module/wowprospero.spec" -nodefaultlibs \
    "$out/x86_64-windows/wowprospero_cpu.o" dlls/wow64/x86_64-windows/libwow64.a \
    dlls/ntdll/x86_64-windows/libntdll.a libs/winecrt0/x86_64-windows/libwinecrt0.a \
    dlls/ntdll/x86_64-windows/libntdll.a libs/compiler-rt/x86_64-windows/libcompiler-rt.a)
sha256sum "$module/cpu.c" "$module/unix.c" "$out/x86_64-windows/wowprospero.dll"

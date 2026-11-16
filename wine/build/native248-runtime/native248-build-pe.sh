#!/usr/bin/env bash
# Configure and compile patched PE modules without modifying the source checkout.
# Usage: bash native248-build-pe.sh preflight|build PATCHED_SOURCE OUTPUT_DIR
set -euo pipefail
mode=${1:?preflight or build}; source_dir=${2:?patched Wine source}; out=${3:?new output directory}
base="${JKFLOW_BUILD_ROOT:?}"
sdk=$base/orbis-win-9749fcd/build/ps4-wine-sdk
host=$base/wine-build
export PATH="$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
export BISON_PKGDATADIR="$base/deps/rootfs/usr/share/bison"
export OO_PS4_TOOLCHAIN="$base/orbis-win-9749fcd/sdk/orbis-sdk-v1/sdk"
[[ $mode == preflight || $mode == build ]] || { echo 'invalid mode' >&2; exit 2; }
[[ $(git -C "$source_dir" rev-parse HEAD) == 490f6d5dcbb2a5047345b8af88d114bbcaad69a8 ]] || { echo 'wrong Wine base' >&2; exit 1; }
for f in "$sdk/bin/prospero-clang" "$sdk/bin/llvm-ar" "$host/tools/winebuild/winebuild" "$base/private-bin/i686-w64-mingw32-gcc" "$base/private-bin/x86_64-w64-mingw32-gcc"; do [[ -x $f ]] || { echo "missing $f" >&2; exit 1; }; done
for tool in make python3 bison flex; do command -v "$tool" >/dev/null || { echo "missing host tool: $tool" >&2; exit 1; }; done
[[ -f "$source_dir/dlls/ntdll/pw_qpc_clock.h" && -f "$source_dir/dlls/ntdll/pw_title_menu.h" && -f "$source_dir/dlls/win32u/pw_key_shared.h" ]] || { echo 'missing staged project headers: stage_vk_batch.py and the three upstream header copies must be performed by the source owner first' >&2; exit 1; }
printf 'SDK=%s\nHOST_TOOLS=%s\nSOURCE=%s\nOUTPUT=%s\n' "$sdk" "$host" "$source_dir" "$out"
[[ $mode == build ]] || exit 0
[[ ! -e $out ]] || { echo 'output must be a new, nonexistent directory' >&2; exit 1; }
mkdir -p "$out/build"
# This PE-only configuration intentionally excludes FreeType and TLS. Do not
# use its Unix objects or infer that the PRX/runtime is complete.
(cd "$out/build" && "$source_dir/configure" --host=x86_64-unknown-freebsd11 --build=x86_64-pc-linux-gnu \
 --enable-archs=i386,x86_64 --disable-tests --without-x --without-freetype \
 --without-fontconfig --without-alsa --without-pulse --without-dbus --without-gstreamer \
 --without-sdl --without-udev --without-usb --without-v4l2 --without-wayland \
 --without-opengl --without-oss --without-pcap --without-pcsclite --without-sane \
 --without-krb5 --without-gphoto --without-netapi --without-inotify --without-capi \
 --without-cups --without-gssapi --without-ffmpeg --without-gnutls \
 --with-wine-tools="$host" CC="$sdk/bin/prospero-clang" CFLAGS='-g -O2' > "$out/configure.log" 2>&1)
modules='ntdll win32u xinput1_1 xinput1_2 xinput1_3 xinput1_4 xinputuap quartz winevulkan kernelbase gdi32 xaudio2_0 xaudio2_1 xaudio2_2 xaudio2_3 xaudio2_4 xaudio2_5 xaudio2_6 xaudio2_7 xaudio2_8 xaudio2_9 xactengine2_0 xactengine2_4 xactengine2_7 xactengine2_9 xactengine3_0 xactengine3_1 xactengine3_2 xactengine3_3 xactengine3_4 xactengine3_5 xactengine3_6 xactengine3_7'
for arch in i386 x86_64; do
  arch_modules=$modules; [[ $arch != x86_64 ]] || arch_modules="$arch_modules wow64"
  for module in $arch_modules; do
    target="dlls/$module/$arch-windows/$module.dll"
    make -C "$out/build" -j"${JOBS:-4}" "$target" >> "$out/make.log" 2>&1 || { echo "failed $target; see $out/make.log" >&2; exit 1; }
    [[ -s "$out/build/$target" ]] || { echo "missing $target" >&2; exit 1; }
  done
done
printf 'PE targets completed; output: %s/build/dlls\n' "$out"
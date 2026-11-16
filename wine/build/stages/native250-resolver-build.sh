#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
base="${JKFLOW_BUILD_ROOT:?}"
orbis=$base/orbis-win-9749fcd
old=$base/native248-build-wine/prx
export PATH="$base/native248-build-wine/tools:$base/private-bin:$base/deps/rootfs/usr/bin:$PATH"
out=$base/native250-resolver
mkdir -p "$out/sce_module"
sdk=$orbis/sdk/orbis-sdk-v1/sdk
cc=$orbis/build/ps4-wine-sdk/bin/prospero-clang
export OO_PS4_TOOLCHAIN="$sdk" PW_SHARED_LIBC=1 PW_PRX_LINK=1 MESA_BUILD="$base/native248-platform"
export PW_PRX_STUBS="-L$old/stubs -Wl,--as-needed $(find "$old/stubs" -maxdepth 1 -name '*.so' -printf '%p ' | sort)"
"$cc" -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -D_REENTRANT -DPW_PS4_DNS -femulated-tls \
  -I"$sdk/include" -c "$repo/research/native-wine/shared-prospero-108194e/wine/ps5/pw_ws2_32_libc.c" \
  -o "$out/resolver.o"
"$cc" -shared -Wl,-Bsymbolic -Wl,--eh-frame-hdr -Wl,-soname,ws2_32.prx \
  -Wl,--warn-unresolved-symbols -o "$out/ws2_32.shared.elf" \
  "$old/obj/ws2_32_unixlib_dns.o" "$out/resolver.o" \
  "$old/obj/pw_emutls_ws2_32.o" "$old/obj/pw_wow_log_ws2_32.o" \
  "$old/heap/pw_wine_heap_libc.o" "$old/obj/pw_wine_dl_libc.o" "$old/obj/ws2_32_desc.o" \
  -lSceNetCtl > "$out/link.log" 2>&1
"$orbis/scripts/create-fself-repro.sh" "$sdk/bin/linux/create-fself" \
  -in "$out/ws2_32.shared.elf" -out "$out/ws2_32.oelf" -lib "$out/sce_module/ws2_32.prx" \
  -libname ws2_32.prx -ptype fake -library-path "$old/stubs:$sdk/lib" >> "$out/link.log" 2>&1
sha256sum "$repo/research/native-wine/shared-prospero-108194e/wine/ps5/pw_ws2_32_libc.c" \
  "$old/sce_module/ws2_32.prx" "$out/ws2_32.shared.elf" "$out/sce_module/ws2_32.prx" > "$out/hashes.sha256"
cat "$out/hashes.sha256"

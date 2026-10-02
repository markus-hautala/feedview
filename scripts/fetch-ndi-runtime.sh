#!/usr/bin/env bash
# Downloads the official NDI(R) runtime library so it can be bundled with FeedView.
#   Linux -> <out>/libndi.so.6      (from the NDI SDK for Linux)
#   macOS -> <out>/libndi.dylib     (from the NDI redistributable runtime for Apple)
# Licence texts found in the download are copied next to the library.
#
# The URLs can be overridden with NDI_SDK_LINUX_URL / NDI_RUNTIME_MAC_URL.
# By downloading you accept the NDI SDK licence: https://ndi.link/ndisdk_license
set -euo pipefail

OUT="${1:?usage: fetch-ndi-runtime.sh <output-dir>}"
mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

copy_licences() {
  find "$TMP" -type f \( -iname '*licen*.txt' -o -iname '*licen*.pdf' \) -print0 |
    while IFS= read -r -d '' f; do cp "$f" "$OUT/NDI-$(basename "$f")"; done
}

case "$(uname -s)" in
  Linux)
    URL="${NDI_SDK_LINUX_URL:-https://downloads.ndi.tv/SDK/NDI_SDK_Linux/Install_NDI_SDK_v6_Linux.tar.gz}"
    curl -fsSL --retry 5 -o "$TMP/sdk.tar.gz" "$URL"
    tar -xzf "$TMP/sdk.tar.gz" -C "$TMP"
    installer="$(find "$TMP" -maxdepth 1 -name 'Install_NDI_SDK_*_Linux.sh' -print -quit)"
    [ -n "$installer" ] || { echo "SDK installer script not found in archive" >&2; exit 1; }
    # The script shows the licence and asks for "y"; it unpacks into "NDI SDK for Linux".
    (cd "$TMP" && { yes || true; } | PAGER=cat sh "$installer" > /dev/null)
    lib="$(find "$TMP" -path '*x86_64-linux-gnu*' -name 'libndi.so.6*' -type f -print -quit)"
    [ -n "$lib" ] || { echo "libndi.so.6 not found in the SDK" >&2; exit 1; }
    cp "$lib" "$OUT/libndi.so.6"
    nm -D --defined-only "$OUT/libndi.so.6" | grep ' NDIlib_recv_create_v3$' > /dev/null
    ;;
  Darwin)
    URL="${NDI_RUNTIME_MAC_URL:-https://ndi.link/NDIRedistV6Apple}"
    curl -fsSL --retry 5 -o "$TMP/runtime.pkg" "$URL"
    pkgutil --expand-full "$TMP/runtime.pkg" "$TMP/pkg"
    lib="$(find "$TMP/pkg" -name 'libndi.dylib' -print -quit)"
    [ -n "$lib" ] || { echo "libndi.dylib not found in the package" >&2; exit 1; }
    cp -L "$lib" "$OUT/libndi.dylib"
    lipo -info "$OUT/libndi.dylib"
    nm -gU "$OUT/libndi.dylib" | grep ' _NDIlib_recv_create_v3$' > /dev/null
    ;;
  *)
    echo "Use scripts/fetch-ndi-runtime.ps1 on Windows" >&2
    exit 1
    ;;
esac

copy_licences
echo "NDI runtime ready in $OUT:"
ls -l "$OUT"
